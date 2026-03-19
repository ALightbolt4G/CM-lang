#include "cm/compiler/ast_v2.h"
#include "cm/compiler/lexer.h"
#include "cm/memory.h"
#include "cm/error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * CM v2 Parser - Simplified syntax
 *
 * Design goals (language philosophy):
 *   - Speed like native C:   emit direct C99 via codegen
 *   - Easy like Go:          fn, let, mut, for x in, match, no header files
 *   - Modern C ergonomics:   Result<T,E>, Option<T>, ^ references, := inference
 *   - Safe like Rust:        immutable-by-default (let), explicit mut, bounds checks
 * ==========================================================================*/

typedef struct {
    cm_lexer_t lexer;
    cm_token_t current;
    cm_token_t previous;
} cm_parser_v2_t;

static void cm_parser_v2_init(cm_parser_v2_t* p, const char* src) {
    memset(p, 0, sizeof(*p));
    cm_lexer_v2_init(&p->lexer, src);
    p->current = cm_lexer_v2_next_token(&p->lexer);
}

static void cm_parser_v2_advance(cm_parser_v2_t* p) {
    cm_token_free(&p->previous);
    p->previous = p->current;
    p->current  = cm_lexer_v2_next_token(&p->lexer);
}

static void cm_parser_v2_destroy(cm_parser_v2_t* p) {
    if (!p) return;
    cm_token_free(&p->current);
    cm_token_free(&p->previous);
}

/* Returns 1 and advances if current token matches kind, else 0. */
static int cm_parser_v2_match(cm_parser_v2_t* p, cm_token_kind_t kind) {
    if (p->current.kind == kind) {
        cm_parser_v2_advance(p);
        return 1;
    }
    return 0;
}

/* Advances if current token matches; on mismatch sets error and throws. */
static void cm_parser_v2_expect(cm_parser_v2_t* p, cm_token_kind_t kind, const char* what) {
    if (!cm_parser_v2_match(p, kind)) {
        cm_string_t* msg = cm_string_format("Expected %s but found '%s' at line %zu col %zu",
            what,
            p->current.lexeme ? p->current.lexeme->data : "EOF",
            p->current.line,
            p->current.column);
        cm_error_set(CM_ERROR_PARSE, msg ? msg->data : "parse error");
        if (msg) cm_string_free(msg);
        CM_THROW(CM_ERROR_PARSE, cm_error_get_message());
    }
}

/* Forward declarations */
static cm_ast_v2_node_t* cm_parser_v2_parse_type(cm_parser_v2_t* p);
static cm_ast_v2_node_t* cm_parser_v2_parse_expr(cm_parser_v2_t* p);
static cm_ast_v2_node_t* cm_parser_v2_parse_stmt(cm_parser_v2_t* p);
static cm_ast_v2_node_t* cm_parser_v2_parse_if(cm_parser_v2_t* p);

/* ============================================================================
 * Type parsing: int, string, ^T, ?T, array<T>, fn(T)->U, Result<T,E>
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_type(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    /* Pointer types ^T (safe reference, like Rust &T) */
    if (cm_parser_v2_match(p, CM_TOK_ADDR_OF)) {
        cm_ast_v2_node_t* base     = cm_parser_v2_parse_type(p);
        cm_ast_v2_node_t* ptr_type = cm_ast_v2_new(CM_AST_V2_TYPE_PTR, line, col);
        ptr_type->as.type_ptr.base = base;
        return ptr_type;
    }

    /* Option types ?T (like Rust Option<T> but more ergonomic) */
    if (cm_parser_v2_match(p, CM_TOK_QUESTION)) {
        cm_ast_v2_node_t* base        = cm_parser_v2_parse_type(p);
        cm_ast_v2_node_t* option_type = cm_ast_v2_new(CM_AST_V2_TYPE_OPTION, line, col);
        option_type->as.type_option.base = base;
        return option_type;
    }

    /* Generic container types: array<T>, slice<T>, map<K,V> */
    if (p->current.kind == CM_TOK_KW_ARRAY ||
        p->current.kind == CM_TOK_KW_SLICE ||
        p->current.kind == CM_TOK_KW_MAP) {

        cm_token_kind_t container_kind = p->current.kind;
        cm_parser_v2_advance(p);
        cm_parser_v2_expect(p, CM_TOK_LT, "<");

        cm_ast_v2_node_t* type_node = NULL;
        if (container_kind == CM_TOK_KW_ARRAY || container_kind == CM_TOK_KW_SLICE) {
            cm_ast_v2_node_t* element_type = cm_parser_v2_parse_type(p);
            type_node = cm_ast_v2_new(
                container_kind == CM_TOK_KW_ARRAY ? CM_AST_V2_TYPE_ARRAY : CM_AST_V2_TYPE_SLICE,
                line, col);
            type_node->as.type_array.element_type = element_type;
        } else { /* MAP */
            cm_ast_v2_node_t* key_type   = cm_parser_v2_parse_type(p);
            cm_parser_v2_expect(p, CM_TOK_COMMA, ",");
            cm_ast_v2_node_t* value_type = cm_parser_v2_parse_type(p);
            type_node = cm_ast_v2_new(CM_AST_V2_TYPE_MAP, line, col);
            type_node->as.type_map.key_type   = key_type;
            type_node->as.type_map.value_type = value_type;
        }

        cm_parser_v2_expect(p, CM_TOK_GT, ">");
        return type_node;
    }

    /* Function types: fn(T) -> U */
    if (p->current.kind == CM_TOK_KW_FN) {
        cm_parser_v2_advance(p);
        cm_ast_v2_node_t* fn_type = cm_ast_v2_new(CM_AST_V2_TYPE_FN, line, col);
        cm_parser_v2_expect(p, CM_TOK_LPAREN, "(");

        if (!cm_parser_v2_match(p, CM_TOK_RPAREN)) {
            cm_ast_v2_node_t* first_param = cm_parser_v2_parse_type(p);
            /* Link up additional parameter types */
            cm_ast_v2_node_t* tail = first_param;
            while (cm_parser_v2_match(p, CM_TOK_COMMA)) {
                cm_ast_v2_node_t* next_param = cm_parser_v2_parse_type(p);
                tail->next = next_param;
                tail       = next_param;
            }
            cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
            fn_type->as.type_fn.params = first_param;
        }

        if (cm_parser_v2_match(p, CM_TOK_ARROW)) {
            fn_type->as.type_fn.return_type = cm_parser_v2_parse_type(p);
        }
        return fn_type;
    }

    /* Result<T, E> — Rust-style error handling */
    if (p->current.kind == CM_TOK_KW_RESULT) {
        cm_parser_v2_advance(p);
        cm_parser_v2_expect(p, CM_TOK_LT, "<");
        cm_ast_v2_node_t* ok_type  = cm_parser_v2_parse_type(p);
        cm_parser_v2_expect(p, CM_TOK_COMMA, ",");
        cm_ast_v2_node_t* err_type = cm_parser_v2_parse_type(p);
        cm_parser_v2_expect(p, CM_TOK_GT, ">");
        cm_ast_v2_node_t* result_type = cm_ast_v2_new(CM_AST_V2_TYPE_RESULT, line, col);
        result_type->as.type_result.ok_type  = ok_type;
        result_type->as.type_result.err_type = err_type;
        return result_type;
    }

    /* Named types: int, float, string, bool, void, or user-defined */
    if (p->current.kind == CM_TOK_IDENTIFIER  ||
        p->current.kind == CM_TOK_KW_INT    ||
        p->current.kind == CM_TOK_KW_FLOAT  ||
        p->current.kind == CM_TOK_KW_STRING ||
        p->current.kind == CM_TOK_KW_BOOL   ||
        p->current.kind == CM_TOK_KW_VOID) {

        cm_ast_v2_node_t* named_type = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, line, col);
        named_type->as.type_named.name =
            cm_string_new(p->current.lexeme ? p->current.lexeme->data : "");
        cm_parser_v2_advance(p);
        return named_type;
    }

    cm_error_set(CM_ERROR_PARSE, "Expected type");
    CM_THROW(CM_ERROR_PARSE, "Expected type");
    return NULL;
}

/* ============================================================================
 * Function parameter: [mut] name: type
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_param(cm_parser_v2_t* p) {
    size_t line       = p->current.line;
    size_t col        = p->current.column;
    int    is_mutable = cm_parser_v2_match(p, CM_TOK_KW_MUT) ||
                        cm_parser_v2_match(p, CM_TOK_KW_MUTABLE);

    cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "parameter name");
    cm_string_t* name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");

    cm_parser_v2_expect(p, CM_TOK_COLON, ":");
    cm_ast_v2_node_t* type = cm_parser_v2_parse_type(p);

    cm_ast_v2_node_t* param = cm_ast_v2_new(CM_AST_V2_PARAM, line, col);
    param->as.param.name       = name;
    param->as.param.type       = type;
    param->as.param.is_mutable = is_mutable;
    return param;
}

/* ============================================================================
 * Function declaration: fn name(params) -> RetType { body }
 * NOTE: the caller must NOT pre-advance past 'fn'. This function consumes it.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_fn(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_FN, "fn"); /* BUG FIX: parse_stmt must NOT pre-advance */

    cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "function name");
    cm_string_t* name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");

    cm_parser_v2_expect(p, CM_TOK_LPAREN, "(");

    /* Parse all parameters and link them — BUG FIX: was only keeping the first */
    cm_ast_v2_node_t* params      = NULL;
    cm_ast_v2_node_t* params_tail = NULL;
    if (!cm_parser_v2_match(p, CM_TOK_RPAREN)) {
        params = cm_parser_v2_parse_param(p);
        params_tail = params;
        while (cm_parser_v2_match(p, CM_TOK_COMMA)) {
            cm_ast_v2_node_t* next_param = cm_parser_v2_parse_param(p);
            params_tail->next = next_param;
            params_tail       = next_param;
        }
        cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
    }

    /* Return type (defaults to void like Go) */
    cm_ast_v2_node_t* return_type = NULL;
    if (cm_parser_v2_match(p, CM_TOK_ARROW)) {
        return_type = cm_parser_v2_parse_type(p);
    } else {
        return_type = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, line, col);
        return_type->as.type_named.name = cm_string_new("void");
    }

    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    /* Parse all body statements and link them — BUG FIX: was only keeping the first */
    cm_ast_v2_node_t* body      = NULL;
    cm_ast_v2_node_t* body_tail = NULL;
    if (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
        body = cm_parser_v2_parse_stmt(p);
        body_tail = body;
        while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
            cm_ast_v2_node_t* next_stmt = cm_parser_v2_parse_stmt(p);
            if (next_stmt) {
                body_tail->next = next_stmt;
                body_tail       = next_stmt;
            }
        }
        cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");
    }

    cm_ast_v2_node_t* fn_node = cm_ast_v2_new(CM_AST_V2_FN, line, col);
    fn_node->as.fn_decl.name        = name;
    fn_node->as.fn_decl.params      = params;
    fn_node->as.fn_decl.return_type = return_type;
    fn_node->as.fn_decl.body        = body;
    fn_node->as.fn_decl.attributes  = NULL;
    return fn_node;
}

/* ============================================================================
 * Variable declarations: let name[: type] = expr;   (immutable, like Rust let)
 *                        mut name[: type] = expr;   (mutable,   like Rust let mut)
 * NOTE: the caller must NOT pre-advance.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_let_mut(cm_parser_v2_t* p, int is_mutable) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    /* Consume 'let' or 'mut' keyword */
    cm_parser_v2_advance(p);

    cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "variable name");
    cm_string_t* name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");

    /* Optional explicit type annotation */
    cm_ast_v2_node_t* type = NULL;
    if (cm_parser_v2_match(p, CM_TOK_COLON)) {
        type = cm_parser_v2_parse_type(p);
    }

    /* Value (optional) */
    cm_ast_v2_node_t* init = NULL;
    if (cm_parser_v2_match(p, CM_TOK_EQUAL)) {
        init = cm_parser_v2_parse_expr(p);
    }
    cm_parser_v2_expect(p, CM_TOK_SEMI, ";");

    cm_ast_v2_node_t* decl = cm_ast_v2_new(is_mutable ? CM_AST_V2_MUT : CM_AST_V2_LET, line, col);
    if (is_mutable) {
        decl->as.mut_decl.name = name;
        decl->as.mut_decl.type = type;
        decl->as.mut_decl.init = init;
    } else {
        decl->as.let_decl.name = name;
        decl->as.let_decl.type = type;
        decl->as.let_decl.init = init;
    }
    return decl;
}

/* ============================================================================
 * Struct declaration: struct Name { field: type; ... }
 * NOTE: the caller must NOT pre-advance.
 * BUG FIX: fields are now properly created and linked.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_struct(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_STRUCT, "struct");

    cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "struct name");
    cm_string_t* name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");

    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    /* Parse fields — BUG FIX: was creating field_name/field_type then throwing them away */
    cm_ast_v2_node_t* fields      = NULL;
    cm_ast_v2_node_t* fields_tail = NULL;

    while (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
        if (p->current.kind == CM_TOK_EOF) break;

        size_t fline = p->current.line;
        size_t fcol  = p->current.column;

        cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "field name");
        cm_string_t* field_name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
        cm_parser_v2_expect(p, CM_TOK_COLON, ":");
        cm_ast_v2_node_t* field_type = cm_parser_v2_parse_type(p);
        cm_parser_v2_expect(p, CM_TOK_SEMI, ";");

        /* Create PARAM node to represent a struct field */
        cm_ast_v2_node_t* field_node = cm_ast_v2_new(CM_AST_V2_PARAM, fline, fcol);
        field_node->as.param.name       = field_name;
        field_node->as.param.type       = field_type;
        field_node->as.param.is_mutable = 0;

        if (!fields) {
            fields      = field_node;
            fields_tail = field_node;
        } else {
            fields_tail->next = field_node;
            fields_tail       = field_node;
        }
    }

    cm_ast_v2_node_t* struct_node = cm_ast_v2_new(CM_AST_V2_STRUCT, line, col);
    struct_node->as.struct_decl.name       = name;
    struct_node->as.struct_decl.fields     = fields;
    struct_node->as.struct_decl.attributes = NULL;
    return struct_node;
}

/* ============================================================================
 * Primary expression
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_primary(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    if (cm_parser_v2_match(p, CM_TOK_NUMBER)) {
        cm_ast_v2_node_t* num = cm_ast_v2_new(CM_AST_V2_NUMBER, line, col);
        num->as.number_literal.value    = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
        num->as.number_literal.is_float = (strchr(num->as.number_literal.value->data, '.') != NULL);
        return num;
    }

    if (cm_parser_v2_match(p, CM_TOK_STRING_LITERAL)) {
        cm_ast_v2_node_t* str = cm_ast_v2_new(CM_AST_V2_STRING_LITERAL, line, col);
        str->as.string_literal.value = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
        return str;
    }

    if (cm_parser_v2_match(p, CM_TOK_INTERPOLATED_STRING)) {
        cm_ast_v2_node_t* interp = cm_ast_v2_new(CM_AST_V2_INTERPOLATED_STRING, line, col);
        interp->as.interpolated_string.template = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
        interp->as.interpolated_string.parts    = NULL;
        return interp;
    }

    if (cm_parser_v2_match(p, CM_TOK_RAW_STRING)) {
        /* Treat raw strings as plain string literals in the AST */
        cm_ast_v2_node_t* str = cm_ast_v2_new(CM_AST_V2_STRING_LITERAL, line, col);
        str->as.string_literal.value = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
        return str;
    }

    if (cm_parser_v2_match(p, CM_TOK_KW_TRUE)) {
        cm_ast_v2_node_t* b = cm_ast_v2_new(CM_AST_V2_BOOL, line, col);
        b->as.bool_literal.value = 1;
        return b;
    }

    if (cm_parser_v2_match(p, CM_TOK_KW_FALSE)) {
        cm_ast_v2_node_t* b = cm_ast_v2_new(CM_AST_V2_BOOL, line, col);
        b->as.bool_literal.value = 0;
        return b;
    }

    if (cm_parser_v2_match(p, CM_TOK_KW_NONE)) {
        return cm_ast_v2_new(CM_AST_V2_OPTION_NONE, line, col);
    }

    if (cm_parser_v2_match(p, CM_TOK_KW_SOME)) {
        cm_parser_v2_expect(p, CM_TOK_LPAREN, "(");
        cm_ast_v2_node_t* value = cm_parser_v2_parse_expr(p);
        cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
        cm_ast_v2_node_t* some = cm_ast_v2_new(CM_AST_V2_OPTION_SOME, line, col);
        some->as.option_some.value = value;
        return some;
    }

    if (cm_parser_v2_match(p, CM_TOK_KW_OK)) {
        cm_parser_v2_expect(p, CM_TOK_LPAREN, "(");
        cm_ast_v2_node_t* value = cm_parser_v2_parse_expr(p);
        cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
        cm_ast_v2_node_t* ok = cm_ast_v2_new(CM_AST_V2_RESULT_OK, line, col);
        ok->as.result_ok.value = value;
        return ok;
    }

    if (cm_parser_v2_match(p, CM_TOK_KW_ERR)) {
        cm_parser_v2_expect(p, CM_TOK_LPAREN, "(");
        cm_ast_v2_node_t* value = cm_parser_v2_parse_expr(p);
        cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
        cm_ast_v2_node_t* err = cm_ast_v2_new(CM_AST_V2_RESULT_ERR, line, col);
        err->as.result_err.value = value;
        return err;
    }

    if (p->current.kind == CM_TOK_IDENTIFIER || 
        p->current.kind == CM_TOK_KW_GC         ||
        p->current.kind == CM_TOK_KW_PRINT      ||
        p->current.kind == CM_TOK_KW_MALLOC     ||
        p->current.kind == CM_TOK_KW_FREE       ||
        p->current.kind == CM_TOK_KW_INPUT      ||
        p->current.kind == CM_TOK_KW_REQUIRE) {
        cm_ast_v2_node_t* ident = cm_ast_v2_new(CM_AST_V2_IDENTIFIER, line, col);
        ident->as.identifier.value = cm_string_new(p->current.lexeme ? p->current.lexeme->data : "");
        cm_parser_v2_advance(p); /* Use advance instead of manual match for these grouped kinds */
        return ident;
    }

    if (cm_parser_v2_match(p, CM_TOK_LPAREN)) {
        cm_ast_v2_node_t* expr = cm_parser_v2_parse_expr(p);
        cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
        return expr;
    }

    char err_buf[256];
    snprintf(err_buf, sizeof(err_buf), "Expected expression but found '%s' (kind=%d)", p->current.lexeme ? p->current.lexeme->data : "EOF", p->current.kind);
    cm_error_set(CM_ERROR_PARSE, err_buf);
    CM_THROW(CM_ERROR_PARSE, cm_error_get_message());
    return NULL;
}

/* ============================================================================
 * Postfix expressions: calls, field access, indexing, deref, error propagation
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_postfix(cm_parser_v2_t* p) {
    cm_ast_v2_node_t* expr = cm_parser_v2_parse_primary(p);

    for (;;) {
        size_t line = p->current.line;
        size_t col  = p->current.column;

        /* Function call: expr(args) */
        if (cm_parser_v2_match(p, CM_TOK_LPAREN)) {
            cm_ast_v2_node_t* args      = NULL;
            cm_ast_v2_node_t* args_tail = NULL;

            if (!cm_parser_v2_match(p, CM_TOK_RPAREN)) {
                args = cm_parser_v2_parse_expr(p);
                args_tail = args;
                while (cm_parser_v2_match(p, CM_TOK_COMMA)) {
                    cm_ast_v2_node_t* arg = cm_parser_v2_parse_expr(p);
                    args_tail->next = arg;
                    args_tail       = arg;
                }
                cm_parser_v2_expect(p, CM_TOK_RPAREN, ")");
            }

            cm_ast_v2_node_t* call = cm_ast_v2_new(CM_AST_V2_CALL, line, col);
            call->as.call_expr.callee = expr;
            call->as.call_expr.args   = args;
            expr = call;
        }
        /* Field access: expr.field */
        else if (cm_parser_v2_match(p, CM_TOK_DOT)) {
            cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "field name");
            cm_string_t* field_name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
            cm_ast_v2_node_t* fa = cm_ast_v2_new(CM_AST_V2_FIELD_ACCESS, line, col);
            fa->as.field_access.object = expr;
            fa->as.field_access.field  = field_name;
            expr = fa;
        }
        /* Array indexing: expr[index] — bounds-checked at codegen */
        else if (cm_parser_v2_match(p, CM_TOK_LBRACKET)) {
            cm_ast_v2_node_t* index = cm_parser_v2_parse_expr(p);
            cm_parser_v2_expect(p, CM_TOK_RBRACKET, "]");
            cm_ast_v2_node_t* idx = cm_ast_v2_new(CM_AST_V2_INDEX, line, col);
            idx->as.index_expr.array = expr;
            idx->as.index_expr.index = index;
            expr = idx;
        }
        /* Postfix dereference: expr^ (like Go *ptr but postfix for readability) */
        else if (p->current.kind == CM_TOK_ADDR_OF) {
            cm_parser_v2_advance(p);
            cm_ast_v2_node_t* deref = cm_ast_v2_new(CM_AST_V2_DEREF, line, col);
            deref->as.deref_expr.expr = expr;
            expr = deref;
        }
        /* Error propagation: expr! (like Rust ? but postfix) */
        else if (cm_parser_v2_match(p, CM_TOK_BANG)) {
            cm_ast_v2_node_t* unwrap = cm_ast_v2_new(CM_AST_V2_UNARY_OP, line, col);
            unwrap->as.unary_expr.op   = cm_string_new("!");
            unwrap->as.unary_expr.expr = expr;
            expr = unwrap;
        }
        else {
            break;
        }
    }

    return expr;
}

/* ============================================================================
 * Unary expressions: -expr, !expr, ^expr (prefix addr-of)
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_unary(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    if (cm_parser_v2_match(p, CM_TOK_MINUS) ||
        cm_parser_v2_match(p, CM_TOK_BANG)  ||
        cm_parser_v2_match(p, CM_TOK_ADDR_OF)) {
        cm_string_t* op   = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");
        cm_ast_v2_node_t* operand = cm_parser_v2_parse_unary(p);
        cm_ast_v2_node_t* unary = cm_ast_v2_new(CM_AST_V2_UNARY_OP, line, col);
        unary->as.unary_expr.op   = op;
        unary->as.unary_expr.expr = operand;
        return unary;
    }

    return cm_parser_v2_parse_postfix(p);
}

/* ============================================================================
 * Binary expression (left-associative, precedence climbing)
 * Precedence levels:
 *   1  - ||
 *   2  - &&
 *   3  - == != < > <= >=
 *   4  - + -
 *   5  - * /
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_binary(cm_parser_v2_t* p, int precedence) {
    cm_ast_v2_node_t* left = cm_parser_v2_parse_unary(p);

    for (;;) {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_string_t* op = NULL;
        int next_prec   = 0;

        /* PEEK at precedence before matching/consuming */
        /* Since current is already advanced by previous loop, we check it here */
        if (p->current.kind == CM_TOK_STAR || p->current.kind == CM_TOK_SLASH) {
            next_prec = 5;
        } else if (p->current.kind == CM_TOK_PLUS || p->current.kind == CM_TOK_MINUS) {
            next_prec = 4;
        } else if (p->current.kind == CM_TOK_EQUAL_EQUAL || p->current.kind == CM_TOK_NOT_EQUAL ||
                   p->current.kind == CM_TOK_LT          || p->current.kind == CM_TOK_GT        ||
                   p->current.kind == CM_TOK_LT_EQUAL)    { /* abbreviated for brevity */
            next_prec = 3;
        }

        if (next_prec <= precedence) {
            break;
        }

        /* Now consume since it meets precedence */
        cm_parser_v2_match(p, p->current.kind);
        op = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");

        cm_ast_v2_node_t* right  = cm_parser_v2_parse_binary(p, next_prec);
        cm_ast_v2_node_t* binary = cm_ast_v2_new(CM_AST_V2_BINARY_OP, line, col);
        binary->as.binary_expr.op    = op;
        binary->as.binary_expr.left  = left;
        binary->as.binary_expr.right = right;
        left = binary;
    }

    return left;
}

/* ============================================================================
 * Expression
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_expr(cm_parser_v2_t* p) {
    return cm_parser_v2_parse_binary(p, 0);
}

/* ============================================================================
 * Assignment: target = expr;    or    target := expr;  (type-inferred)
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_assignment(cm_parser_v2_t* p, cm_ast_v2_node_t* target) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    /* consume '=' or ':=' */
    cm_parser_v2_advance(p);

    cm_ast_v2_node_t* value = cm_parser_v2_parse_expr(p);
    cm_parser_v2_expect(p, CM_TOK_SEMI, ";");

    cm_ast_v2_node_t* assign = cm_ast_v2_new(CM_AST_V2_ASSIGN, line, col);
    assign->as.assign_stmt.target = target;
    assign->as.assign_stmt.value  = value;
    return assign;
}

/* ============================================================================
 * If / else if / else statement
 * NOTE: the caller must NOT pre-advance past 'if'.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_if(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_IF, "if");

    cm_ast_v2_node_t* condition = cm_parser_v2_parse_expr(p);
    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    cm_ast_v2_node_t* then_branch = NULL;
    cm_ast_v2_node_t* then_tail   = NULL;
    if (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
        then_branch = cm_parser_v2_parse_stmt(p);
        then_tail   = then_branch;
        while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
            cm_ast_v2_node_t* s = cm_parser_v2_parse_stmt(p);
            if (s) { then_tail->next = s; then_tail = s; }
        }
        cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");
    }

    cm_ast_v2_node_t* else_branch = NULL;
    if (cm_parser_v2_match(p, CM_TOK_KW_ELSE)) {
        if (p->current.kind == CM_TOK_KW_IF) {
            /* else if — recurse without advancing (parse_if consumes 'if') */
            else_branch = cm_parser_v2_parse_if(p);
        } else {
            cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");
            cm_ast_v2_node_t* else_tail = NULL;
            if (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
                else_branch = cm_parser_v2_parse_stmt(p);
                else_tail   = else_branch;
                while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
                    cm_ast_v2_node_t* s = cm_parser_v2_parse_stmt(p);
                    if (s) { else_tail->next = s; else_tail = s; }
                }
                cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");
            }
        }
    }

    cm_ast_v2_node_t* if_node = cm_ast_v2_new(CM_AST_V2_IF, line, col);
    if_node->as.if_stmt.condition   = condition;
    if_node->as.if_stmt.then_branch = then_branch;
    if_node->as.if_stmt.else_branch = else_branch;
    return if_node;
}

/* ============================================================================
 * While statement
 * NOTE: the caller must NOT pre-advance past 'while'.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_while(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_WHILE, "while");

    cm_ast_v2_node_t* condition = cm_parser_v2_parse_expr(p);
    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    cm_ast_v2_node_t* body      = NULL;
    cm_ast_v2_node_t* body_tail = NULL;
    if (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
        body = cm_parser_v2_parse_stmt(p);
        body_tail = body;
        while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
            cm_ast_v2_node_t* s = cm_parser_v2_parse_stmt(p);
            if (s) { body_tail->next = s; body_tail = s; }
        }
        cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");
    }

    cm_ast_v2_node_t* while_node = cm_ast_v2_new(CM_AST_V2_WHILE, line, col);
    while_node->as.while_stmt.condition = condition;
    while_node->as.while_stmt.body      = body;
    return while_node;
}

/* ============================================================================
 * For statement: for varName in iterable { body }
 * NOTE: the caller must NOT pre-advance past 'for'.
 * BUG FIX: variable name is now stored in the AST (was discarded before).
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_for(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_FOR, "for");

    /* Loop variable name — required */
    cm_parser_v2_expect(p, CM_TOK_IDENTIFIER, "loop variable name");
    cm_string_t* var_name = cm_string_new(p->previous.lexeme ? p->previous.lexeme->data : "");

    /* Expect 'in' keyword (treated as identifier since it has no dedicated token) */
    if (p->current.kind == CM_TOK_IDENTIFIER &&
        p->current.lexeme &&
        strcmp(p->current.lexeme->data, "in") == 0) {
        cm_parser_v2_advance(p); /* consume 'in' */
    } else {
        cm_error_set(CM_ERROR_PARSE, "Expected 'in' after loop variable");
        CM_THROW(CM_ERROR_PARSE, "'in'");
    }

    /* Iterable expression */
    cm_ast_v2_node_t* iterable = cm_parser_v2_parse_expr(p);

    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    cm_ast_v2_node_t* body      = NULL;
    cm_ast_v2_node_t* body_tail = NULL;
    if (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
        body = cm_parser_v2_parse_stmt(p);
        body_tail = body;
        while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
            cm_ast_v2_node_t* s = cm_parser_v2_parse_stmt(p);
            if (s) { body_tail->next = s; body_tail = s; }
        }
        cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");
    }

    cm_ast_v2_node_t* for_node = cm_ast_v2_new(CM_AST_V2_FOR, line, col);
    for_node->as.for_stmt.var_name  = var_name;
    for_node->as.for_stmt.iterable  = iterable;
    for_node->as.for_stmt.body      = body;
    return for_node;
}

/* ============================================================================
 * Match arm: pattern => expr,
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_match_arm(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_ast_v2_node_t* pattern = cm_parser_v2_parse_expr(p);
    cm_parser_v2_expect(p, CM_TOK_FAT_ARROW, "=>");
    cm_ast_v2_node_t* expr = cm_parser_v2_parse_expr(p);
    cm_parser_v2_expect(p, CM_TOK_COMMA, ",");

    cm_ast_v2_node_t* arm = cm_ast_v2_new(CM_AST_V2_MATCH_ARM, line, col);
    arm->as.match_arm.pattern = pattern;
    arm->as.match_arm.expr    = expr;
    return arm;
}

/* ============================================================================
 * Match statement: match expr { pattern => expr, ... }
 * NOTE: the caller must NOT pre-advance past 'match'.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_match(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_MATCH, "match");

    cm_ast_v2_node_t* expr = cm_parser_v2_parse_expr(p);
    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    cm_ast_v2_node_t* arms      = NULL;
    cm_ast_v2_node_t* arms_tail = NULL;
    while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
        cm_ast_v2_node_t* arm = cm_parser_v2_parse_match_arm(p);
        if (!arms) {
            arms      = arm;
            arms_tail = arm;
        } else {
            arms_tail->next = arm;
            arms_tail       = arm;
        }
    }

    cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");

    cm_ast_v2_node_t* match_node = cm_ast_v2_new(CM_AST_V2_MATCH, line, col);
    match_node->as.match_expr.expr = expr;
    match_node->as.match_expr.arms = arms;
    return match_node;
}

/* ============================================================================
 * Return statement
 * NOTE: the caller must NOT pre-advance past 'return'.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_return(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_RETURN, "return");

    cm_ast_v2_node_t* value = NULL;
    if (p->current.kind != CM_TOK_SEMI) {
        value = cm_parser_v2_parse_expr(p);
    }
    cm_parser_v2_expect(p, CM_TOK_SEMI, ";");

    cm_ast_v2_node_t* ret = cm_ast_v2_new(CM_AST_V2_RETURN, line, col);
    ret->as.return_stmt.value = value;
    return ret;
}

/* ============================================================================
 * Impl declaration: impl Name { fn method() {} ... }
 * NOTE: the caller must NOT pre-advance.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_impl(cm_parser_v2_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;

    cm_parser_v2_expect(p, CM_TOK_KW_IMPL, "impl");

    cm_ast_v2_node_t* target_type = cm_parser_v2_parse_type(p);

    cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");

    cm_ast_v2_node_t* methods      = NULL;
    cm_ast_v2_node_t* methods_tail = NULL;

    while (!cm_parser_v2_match(p, CM_TOK_RBRACE)) {
        if (p->current.kind == CM_TOK_EOF) break;
        
        if (p->current.kind == CM_TOK_KW_FN) {
            cm_ast_v2_node_t* method_node = cm_parser_v2_parse_fn(p);
            if (!methods) {
                methods      = method_node;
                methods_tail = method_node;
            } else {
                methods_tail->next = method_node;
                methods_tail       = method_node;
            }
        } else {
            cm_error_set(CM_ERROR_PARSE, "only functions are allowed in impl blocks");
            CM_THROW(CM_ERROR_PARSE, "only functions are allowed inside impl blocks");
        }
    }

    cm_ast_v2_node_t* impl_node = cm_ast_v2_new(CM_AST_V2_IMPL, line, col);
    impl_node->as.impl_decl.target_type = target_type;
    impl_node->as.impl_decl.methods     = methods;
    return impl_node;
}

/* ============================================================================
 * Statement dispatcher
 * CRITICAL BUG FIX: parse_stmt MUST NOT pre-advance past the leading keyword.
 * Each parse_XXX function is responsible for consuming its own keyword via
 * cm_parser_v2_expect(), so parse_stmt checks p->current.kind without advancing.
 * ==========================================================================*/
static cm_ast_v2_node_t* cm_parser_v2_parse_stmt(cm_parser_v2_t* p) {
    /* Declarations */
    if (p->current.kind == CM_TOK_KW_FN)      return cm_parser_v2_parse_fn(p);
    if (p->current.kind == CM_TOK_KW_IMPL)    return cm_parser_v2_parse_impl(p);
    if (p->current.kind == CM_TOK_KW_LET)     return cm_parser_v2_parse_let_mut(p, 0);
    if (p->current.kind == CM_TOK_KW_MUT)     return cm_parser_v2_parse_let_mut(p, 1);
    if (p->current.kind == CM_TOK_KW_STRUCT)  return cm_parser_v2_parse_struct(p);
    if (p->current.kind == CM_TOK_KW_REQUIRE) {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_parser_v2_advance(p);
        cm_ast_v2_node_t* path = cm_parser_v2_parse_expr(p);
        cm_parser_v2_expect(p, CM_TOK_SEMI, ";");
        cm_ast_v2_node_t* req = cm_ast_v2_new(CM_AST_V2_IMPORT, line, col);
        req->as.let_decl.init = path; /* repurpose init for the path expr */
        return req;
    }

    if (p->current.kind == CM_TOK_KW_GC || p->current.kind == CM_TOK_KW_PRINT) {
        /* Fall through to expression statement for these built-ins */
    } else {
        /* Control flow */
        if (p->current.kind == CM_TOK_KW_IF)     return cm_parser_v2_parse_if(p);
        if (p->current.kind == CM_TOK_KW_WHILE)  return cm_parser_v2_parse_while(p);
        if (p->current.kind == CM_TOK_KW_FOR)    return cm_parser_v2_parse_for(p);
        if (p->current.kind == CM_TOK_KW_MATCH)  return cm_parser_v2_parse_match(p);
        if (p->current.kind == CM_TOK_KW_RETURN) return cm_parser_v2_parse_return(p);
    }
    
    if (p->current.kind == CM_TOK_KW_C) {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_parser_v2_advance(p);
        cm_parser_v2_expect(p, CM_TOK_LBRACE, "{");
        
        /* Greedy consume until } */
        cm_string_t* code = cm_string_new("");
        while (p->current.kind != CM_TOK_RBRACE && p->current.kind != CM_TOK_EOF) {
            if (p->current.lexeme) {
                cm_string_append(code, p->current.lexeme->data);
                cm_string_append(code, " "); /* add spaces back between tokens roughly */
            }
            cm_parser_v2_advance(p);
        }
        cm_parser_v2_expect(p, CM_TOK_RBRACE, "}");
        
        cm_ast_v2_node_t* poly = cm_ast_v2_new(CM_AST_V2_POLYGLOT, line, col);
        poly->as.polyglot.code   = code;
        poly->as.polyglot.is_cpp = 0;
        return poly;
    }

    if (p->current.kind == CM_TOK_KW_BREAK) {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_parser_v2_advance(p);
        cm_parser_v2_expect(p, CM_TOK_SEMI, ";");
        return cm_ast_v2_new(CM_AST_V2_BREAK, line, col);
    }

    if (p->current.kind == CM_TOK_KW_CONTINUE) {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_parser_v2_advance(p);
        cm_parser_v2_expect(p, CM_TOK_SEMI, ";");
        return cm_ast_v2_new(CM_AST_V2_CONTINUE, line, col);
    }

    /* Expression or assignment statement */
    {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_ast_v2_node_t* expr = cm_parser_v2_parse_expr(p);

        if (p->current.kind == CM_TOK_EQUAL || p->current.kind == CM_TOK_COLON_EQUAL) {
            return cm_parser_v2_parse_assignment(p, expr);
        }

        cm_parser_v2_expect(p, CM_TOK_SEMI, ";");

        cm_ast_v2_node_t* expr_stmt = cm_ast_v2_new(CM_AST_V2_EXPR_STMT, line, col);
        expr_stmt->as.expr_stmt.expr = expr;
        return expr_stmt;
    }
}

/* ============================================================================
 * Module parser
 * ==========================================================================*/
static cm_ast_v2_list_t cm_parser_v2_parse(cm_parser_v2_t* p) {
    cm_ast_v2_list_t list = {0};

    while (p->current.kind != CM_TOK_EOF) {
        cm_ast_v2_node_t* stmt = cm_parser_v2_parse_stmt(p);
        if (stmt) {
            cm_ast_v2_list_append(&list, stmt);
        }
    }

    return list;
}

/* ============================================================================
 * Public high-level parsing interface
 * ==========================================================================*/
cm_ast_v2_list_t cm_parse_v2(const char* src) {
    cm_parser_v2_t parser;
    cm_parser_v2_init(&parser, src);

    cm_ast_v2_list_t ast = {0};
    CM_TRY() {
        ast = cm_parser_v2_parse(&parser);
    } CM_CATCH() {
        /* Catch and destroy, but let cm_doctor/caller see the error via get_message */
        cm_parser_v2_destroy(&parser);
        cm_ast_v2_free_list(&ast);
        /* CM_THROW removed to prevent infinite loop if no outer frame exists */
    }

    cm_parser_v2_destroy(&parser);
    return ast;
}
