#include "cm/compiler/ast_v2.h"
#include "cm/memory.h"
#include "cm/error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * CM v2 Codegen - Generate hardened C99 code
 *
 * Language design goals implemented here:
 *   - Native C speed:     emit straight C99 with zero overhead abstractions
 *   - Go ease:            simple, readable generated code
 *   - Rust safety:        bounds-checked indexing, safe-deref, Result/Option types
 *   - Modern C clarity:   use const-correct types, no 'auto' (C99 compat)
 * ==========================================================================*/

typedef struct {
    const char* method_name;
    const char* type_name;
} cm_method_info_t;

typedef struct {
    cm_string_t* output;
    int          indent_level;
    cm_string_t* current_function;
    int          temp_counter;
    cm_method_info_t methods[512];
    int          method_count;
} cm_codegen_v2_t;

static void cm_codegen_v2_init(cm_codegen_v2_t* cg) {
    memset(cg, 0, sizeof(*cg));
    cg->output           = cm_string_new("");
    cg->indent_level     = 0;
    cg->current_function = cm_string_new("");
    cg->temp_counter     = 0;
}

static void cm_codegen_v2_destroy(cm_codegen_v2_t* cg) {
    /* Note: cg->output is returned to caller, do NOT free it here */
    if (cg->current_function) cm_string_free(cg->current_function);
}

static void cm_codegen_v2_indent(cm_codegen_v2_t* cg) {
    int i;
    for (i = 0; i < cg->indent_level; i++) {
        cm_string_append(cg->output, "    ");
    }
}

static void cm_codegen_v2_newline(cm_codegen_v2_t* cg) {
    cm_string_append(cg->output, "\n");
}

/* Name mangling: prefix all user identifiers with cm_ to avoid C keyword conflicts.
 * BUG FIX: was using a static buffer — unsafe for nested calls.
 * Now returns a freshly allocated cm_string_t; caller must free. */
static cm_string_t* cm_codegen_v2_mangle(const char* name) {
    return cm_string_format("cm_%s", name ? name : "unknown");
}

/* Map CM type node to a C99 type string (static, short-lived). */
static const char* cm_codegen_v2_type_to_c(cm_ast_v2_node_t* type) {
    if (!type) return "void";

    switch (type->kind) {
        case CM_AST_V2_TYPE_NAMED:
            if (!type->as.type_named.name) return "void";
            if (strcmp(type->as.type_named.name->data, "int")    == 0) return "int";
            if (strcmp(type->as.type_named.name->data, "float")  == 0) return "double";
            if (strcmp(type->as.type_named.name->data, "string") == 0) return "cm_string_t*";
            if (strcmp(type->as.type_named.name->data, "bool")   == 0) return "int";
            if (strcmp(type->as.type_named.name->data, "void")   == 0) return "void";
            return type->as.type_named.name->data; /* user-defined struct name */

        case CM_AST_V2_TYPE_PTR:    return "cm_safe_ptr_t*";
        case CM_AST_V2_TYPE_OPTION: return "cm_option_t";
        case CM_AST_V2_TYPE_RESULT: return "cm_result_t";
        case CM_AST_V2_TYPE_ARRAY:  return "cm_array_t*";
        case CM_AST_V2_TYPE_SLICE:  return "cm_slice_t";
        case CM_AST_V2_TYPE_MAP:    return "cm_map_t*";
        default:                    return "void";
    }
}

/* Forward declaration */
static void cm_codegen_v2_generate_expr(cm_codegen_v2_t* cg, cm_ast_v2_node_t* expr);
static void cm_codegen_v2_generate_stmt(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt);

/* ============================================================================
 * Expression generators
 * ==========================================================================*/

static void cm_codegen_v2_generate_binary_op(cm_codegen_v2_t* cg, cm_ast_v2_node_t* binary) {
    cm_string_append(cg->output, "(");
    cm_codegen_v2_generate_expr(cg, binary->as.binary_expr.left);
    cm_string_append(cg->output, " ");
    cm_string_append(cg->output, binary->as.binary_expr.op->data);
    cm_string_append(cg->output, " ");
    cm_codegen_v2_generate_expr(cg, binary->as.binary_expr.right);
    cm_string_append(cg->output, ")");
}

static void cm_codegen_v2_generate_unary_op(cm_codegen_v2_t* cg, cm_ast_v2_node_t* unary) {
    const char* op = unary->as.unary_expr.op->data;

    if (strcmp(op, "!") == 0) {
        /* Error propagation operator:
         * CM:  result!
         * C99: cm_result_unwrap_or_return(result)
         * This is safe-like-Rust: propagates error up the call stack. */
        cm_string_append(cg->output, "cm_result_unwrap_or_return(");
        cm_codegen_v2_generate_expr(cg, unary->as.unary_expr.expr);
        cm_string_append(cg->output, ")");
    } else {
        cm_string_append(cg->output, "(");
        cm_string_append(cg->output, op);
        cm_codegen_v2_generate_expr(cg, unary->as.unary_expr.expr);
        cm_string_append(cg->output, ")");
    }
}

static void cm_codegen_v2_generate_call(cm_codegen_v2_t* cg, cm_ast_v2_node_t* call) {
    if (call->as.call_expr.callee && call->as.call_expr.callee->kind == CM_AST_V2_FIELD_ACCESS) {
        /* Method call: obj.method(...) -> cm_OOP_method((obj), ...) */
        cm_ast_v2_node_t* obj = call->as.call_expr.callee->as.field_access.object;
        cm_string_t* method_name = call->as.call_expr.callee->as.field_access.field;
        
        /* Look up if method is an impl method */
        const char* target_type = NULL;
        int match_count = 0;
        for (int i = 0; i < cg->method_count; i++) {
            if (strcmp(cg->methods[i].method_name, method_name->data) == 0) {
                target_type = cg->methods[i].type_name;
                match_count++;
            }
        }
        
        if (match_count == 1) {
            cm_string_append(cg->output, "cm_");
            cm_string_append(cg->output, target_type);
            cm_string_append(cg->output, "_");
            cm_string_append(cg->output, method_name->data);
            cm_string_append(cg->output, "(");
            
            /* Pass the object as the first argument (self) */
            /* If object needs to be cast or referenced, do it here. For now, pass directly. */
            cm_codegen_v2_generate_expr(cg, obj);
            
            cm_ast_v2_node_t* arg = call->as.call_expr.args;
            if (arg) {
                cm_string_append(cg->output, ", ");
            }
            while (arg) {
                cm_codegen_v2_generate_expr(cg, arg);
                if (arg->next) cm_string_append(cg->output, ", ");
                arg = arg->next;
            }
            cm_string_append(cg->output, ")");
            return;
        } else if (match_count > 1) {
            cm_string_append(cg->output, "/* ERROR: AMBIGUOUS METHOD CALL: ");
            cm_string_append(cg->output, method_name->data);
            cm_string_append(cg->output, " */ ");
            /* Fallthrough to normal generation just to prevent total crash */
        }
        /* If match_count == 0, it falls through to standard field access generation */
    }

    cm_codegen_v2_generate_expr(cg, call->as.call_expr.callee);
    cm_string_append(cg->output, "(");

    cm_ast_v2_node_t* arg = call->as.call_expr.args;
    while (arg) {
        cm_codegen_v2_generate_expr(cg, arg);
        if (arg->next) cm_string_append(cg->output, ", ");
        arg = arg->next;
    }

    cm_string_append(cg->output, ")");
}

static void cm_codegen_v2_generate_field_access(cm_codegen_v2_t* cg, cm_ast_v2_node_t* access) {
    cm_codegen_v2_generate_expr(cg, access->as.field_access.object);
    cm_string_append(cg->output, "->");
    cm_string_append(cg->output, access->as.field_access.field->data);
}

/* BUG FIX: bounds check is now emitted as a STATEMENT before the expression,
 * not inline inside it (which produced invalid C). The index expr is now pure. */
static void cm_codegen_v2_generate_index_stmt(cm_codegen_v2_t* cg, cm_ast_v2_node_t* index) {
    /* This helper emits the safety check as a standalone statement. */
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "CM_BOUNDS_CHECK(");
    cm_codegen_v2_generate_expr(cg, index->as.index_expr.array);
    cm_string_append(cg->output, ", ");
    cm_codegen_v2_generate_expr(cg, index->as.index_expr.index);
    cm_string_append(cg->output, ");");
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_index_expr(cm_codegen_v2_t* cg, cm_ast_v2_node_t* index) {
    /* Pure expression form — bounds check must be emitted separately as a stmt. */
    cm_codegen_v2_generate_expr(cg, index->as.index_expr.array);
    cm_string_append(cg->output, "[");
    cm_codegen_v2_generate_expr(cg, index->as.index_expr.index);
    cm_string_append(cg->output, "]");
}

static void cm_codegen_v2_generate_deref(cm_codegen_v2_t* cg, cm_ast_v2_node_t* deref) {
    /* Safe dereference — macro checks for NULL before deref (Rust-safety principle) */
    cm_string_append(cg->output, "CM_SAFE_DEREF(");
    cm_codegen_v2_generate_expr(cg, deref->as.deref_expr.expr);
    cm_string_append(cg->output, ")");
}

static void cm_codegen_v2_generate_addr_of(cm_codegen_v2_t* cg, cm_ast_v2_node_t* addr_of) {
    cm_string_append(cg->output, "CM_SAFE_ADDR_OF(");
    cm_codegen_v2_generate_expr(cg, addr_of->as.addr_of_expr.expr);
    cm_string_append(cg->output, ")");
}

/* Interpolated string: "hello {name}, age {age}"
 * BUG FIX: was calling generate_expr(parts) but parts was always NULL.
 * Now properly parses the template to extract variable names inline. */
static void cm_codegen_v2_generate_interpolated_string(cm_codegen_v2_t* cg, cm_ast_v2_node_t* interp) {
    const char* tmpl = interp->as.interpolated_string.template
                     ? interp->as.interpolated_string.template->data
                     : "";

    /* Build format string and collect variable names in one pass */
    cm_string_t* fmt_str  = cm_string_new("\"");
    cm_string_t* var_args = cm_string_new("");
    int first_arg = 1;

    const char* p = tmpl;
    while (*p) {
        if (*p == '{') {
            const char* end = strchr(p + 1, '}');
            if (end && end > p + 1) {
                size_t var_len = (size_t)(end - p - 1);
                char var_buf[256];
                if (var_len < sizeof(var_buf) - 1) {
                    memcpy(var_buf, p + 1, var_len);
                    var_buf[var_len] = '\0';

                    /* %s for everything — simple and correct for string types */
                    cm_string_append(fmt_str, "%s");

                    if (!first_arg) cm_string_append(var_args, ", ");
                    first_arg = 0;

                    /* Mangle the variable name, same as the rest of codegen */
                    cm_string_t* mangled = cm_codegen_v2_mangle(var_buf);
                    cm_string_append(var_args, mangled->data);
                    cm_string_free(mangled);
                }
                p = end + 1;
                continue;
            }
        }
        if (*p == '"') {
            cm_string_append(fmt_str, "\\\"");
        } else if (*p == '\n') {
            cm_string_append(fmt_str, "\\n");
        } else if (*p == '\t') {
            cm_string_append(fmt_str, "\\t");
        } else if (*p == '\\') {
            cm_string_append(fmt_str, "\\\\");
        } else {
            char buf[2] = {*p, '\0'};
            cm_string_append(fmt_str, buf);
        }
        p++;
    }
    cm_string_append(fmt_str, "\"");

    cm_string_append(cg->output, "cm_string_format(");
    cm_string_append(cg->output, fmt_str->data);
    if (var_args->length > 0) {
        cm_string_append(cg->output, ", ");
        cm_string_append(cg->output, var_args->data);
    }
    cm_string_append(cg->output, ")");

    cm_string_free(fmt_str);
    cm_string_free(var_args);
}

static void cm_codegen_v2_generate_literal(cm_codegen_v2_t* cg, cm_ast_v2_node_t* lit) {
    switch (lit->kind) {
        case CM_AST_V2_STRING_LITERAL:
            cm_string_append(cg->output, "cm_string_new(\"");
            {
                const char* s = lit->as.string_literal.value
                              ? lit->as.string_literal.value->data : "";
                while (*s) {
                    if (*s == '"')       cm_string_append(cg->output, "\\\"");
                    else if (*s == '\n') cm_string_append(cg->output, "\\n");
                    else if (*s == '\t') cm_string_append(cg->output, "\\t");
                    else if (*s == '\\') cm_string_append(cg->output, "\\\\");
                    else {
                        char buf[2] = {*s, '\0'};
                        cm_string_append(cg->output, buf);
                    }
                    s++;
                }
            }
            cm_string_append(cg->output, "\")");
            break;

        case CM_AST_V2_NUMBER:
            cm_string_append(cg->output, lit->as.number_literal.value
                             ? lit->as.number_literal.value->data : "0");
            break;

        case CM_AST_V2_BOOL:
            cm_string_append(cg->output, lit->as.bool_literal.value ? "1" : "0");
            break;

        case CM_AST_V2_IDENTIFIER: {
            /* BUG FIX: was using static buffer — now properly heap-allocated */
            cm_string_t* mangled = cm_codegen_v2_mangle(
                lit->as.identifier.value ? lit->as.identifier.value->data : "");
            cm_string_append(cg->output, mangled->data);
            cm_string_free(mangled);
            break;
        }

        case CM_AST_V2_OPTION_NONE:
            cm_string_append(cg->output, "CM_OPTION_NONE");
            break;

        case CM_AST_V2_OPTION_SOME:
            cm_string_append(cg->output, "CM_OPTION_SOME(");
            cm_codegen_v2_generate_expr(cg, lit->as.option_some.value);
            cm_string_append(cg->output, ")");
            break;

        case CM_AST_V2_RESULT_OK:
            cm_string_append(cg->output, "CM_RESULT_OK(");
            cm_codegen_v2_generate_expr(cg, lit->as.result_ok.value);
            cm_string_append(cg->output, ")");
            break;

        case CM_AST_V2_RESULT_ERR:
            cm_string_append(cg->output, "CM_RESULT_ERR(");
            cm_codegen_v2_generate_expr(cg, lit->as.result_err.value);
            cm_string_append(cg->output, ")");
            break;

        default:
            cm_string_append(cg->output, "/* unknown literal */");
            break;
    }
}

static void cm_codegen_v2_generate_expr(cm_codegen_v2_t* cg, cm_ast_v2_node_t* expr) {
    if (!expr) return;

    switch (expr->kind) {
        case CM_AST_V2_BINARY_OP:           cm_codegen_v2_generate_binary_op(cg, expr); break;
        case CM_AST_V2_UNARY_OP:            cm_codegen_v2_generate_unary_op(cg, expr); break;
        case CM_AST_V2_CALL:                cm_codegen_v2_generate_call(cg, expr); break;
        case CM_AST_V2_FIELD_ACCESS:        cm_codegen_v2_generate_field_access(cg, expr); break;
        case CM_AST_V2_INDEX:               cm_codegen_v2_generate_index_expr(cg, expr); break;
        case CM_AST_V2_DEREF:               cm_codegen_v2_generate_deref(cg, expr); break;
        case CM_AST_V2_ADDR_OF:             cm_codegen_v2_generate_addr_of(cg, expr); break;
        case CM_AST_V2_INTERPOLATED_STRING: cm_codegen_v2_generate_interpolated_string(cg, expr); break;

        case CM_AST_V2_STRING_LITERAL:
        case CM_AST_V2_NUMBER:
        case CM_AST_V2_BOOL:
        case CM_AST_V2_IDENTIFIER:
        case CM_AST_V2_OPTION_NONE:
        case CM_AST_V2_OPTION_SOME:
        case CM_AST_V2_RESULT_OK:
        case CM_AST_V2_RESULT_ERR:
            cm_codegen_v2_generate_literal(cg, expr);
            break;

        default:
            cm_string_append(cg->output, "/* unknown expression */");
            break;
    }
}

/* ============================================================================
 * Statement generators
 * ==========================================================================*/

static void cm_codegen_v2_generate_let_mut(cm_codegen_v2_t* cg, cm_ast_v2_node_t* decl, int is_mutable) {
    const char* type_str;
    cm_ast_v2_node_t* decl_data;

    /* Both let_decl and mut_decl share the same layout — use let_decl fields */
    decl_data = decl;

    if (decl_data->as.let_decl.type) {
        type_str = cm_codegen_v2_type_to_c(decl_data->as.let_decl.type);
    } else {
        /* BUG FIX: was using C++ 'auto' which is invalid as a type in C99.
         * Default to 'int' (Go-style: most common numeric default). */
        type_str = "int";
    }

    cm_codegen_v2_indent(cg);

    if (!is_mutable) {
        /* let → const, immutable by default like Rust */
        cm_string_append(cg->output, "const ");
    }

    cm_string_append(cg->output, type_str);
    cm_string_append(cg->output, " ");

    {
        cm_string_t* mangled = cm_codegen_v2_mangle(
            decl_data->as.let_decl.name ? decl_data->as.let_decl.name->data : "");
        cm_string_append(cg->output, mangled->data);
        cm_string_free(mangled);
    }

    if (decl_data->as.let_decl.init) {
        cm_string_append(cg->output, " = ");
        cm_codegen_v2_generate_expr(cg, decl_data->as.let_decl.init);
    } else {
        /* Default construct to prevent C undefined behavior */
        cm_string_append(cg->output, " = {0}");
    }
    cm_string_append(cg->output, ";");
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_fn(cm_codegen_v2_t* cg, cm_ast_v2_node_t* fn) {
    cm_codegen_v2_indent(cg);

    /* Return type */
    cm_string_append(cg->output, cm_codegen_v2_type_to_c(fn->as.fn_decl.return_type));
    cm_string_append(cg->output, " ");

    /* Function name */
    {
        cm_string_t* mangled = cm_codegen_v2_mangle(
            fn->as.fn_decl.name ? fn->as.fn_decl.name->data : "");
        cm_string_append(cg->output, mangled->data);
        cm_string_free(mangled);
    }

    /* Parameters */
    cm_string_append(cg->output, "(");
    {
        cm_ast_v2_node_t* param = fn->as.fn_decl.params;
        while (param) {
            const char* param_type = cm_codegen_v2_type_to_c(param->as.param.type);
            if (!param->as.param.is_mutable) {
                cm_string_append(cg->output, "const ");
            }
            cm_string_append(cg->output, param_type);
            cm_string_append(cg->output, " ");
            {
                cm_string_t* pname = cm_codegen_v2_mangle(
                    param->as.param.name ? param->as.param.name->data : "");
                cm_string_append(cg->output, pname->data);
                cm_string_free(pname);
            }
            if (param->next) cm_string_append(cg->output, ", ");
            param = param->next;
        }
    }
    cm_string_append(cg->output, ")");
    cm_codegen_v2_newline(cg);

    /* Body */
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "{");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;
    cm_string_set(cg->current_function,
        fn->as.fn_decl.name ? fn->as.fn_decl.name->data : "");

    {
        cm_ast_v2_node_t* s = fn->as.fn_decl.body;
        while (s) {
            cm_codegen_v2_generate_stmt(cg, s);
            s = s->next;
        }
    }

    cg->indent_level--;
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "}");
    cm_codegen_v2_newline(cg);
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_struct(cm_codegen_v2_t* cg, cm_ast_v2_node_t* st) {
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "typedef struct {");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;
    {
        cm_ast_v2_node_t* f = st->as.struct_decl.fields;
        while (f) {
            cm_codegen_v2_indent(cg);
            cm_string_append(cg->output, cm_codegen_v2_type_to_c(f->as.param.type));
            cm_string_append(cg->output, " ");
            cm_string_append(cg->output,
                f->as.param.name ? f->as.param.name->data : "field");
            cm_string_append(cg->output, ";");
            cm_codegen_v2_newline(cg);
            f = f->next;
        }
    }
    cg->indent_level--;

    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "} ");
    cm_string_append(cg->output,
        st->as.struct_decl.name ? st->as.struct_decl.name->data : "");
    cm_string_append(cg->output, ";");
    cm_codegen_v2_newline(cg);
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_if(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "if (");
    cm_codegen_v2_generate_expr(cg, stmt->as.if_stmt.condition);
    cm_string_append(cg->output, ") {");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;
    {
        cm_ast_v2_node_t* s = stmt->as.if_stmt.then_branch;
        while (s) { cm_codegen_v2_generate_stmt(cg, s); s = s->next; }
    }
    cg->indent_level--;

    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "}");

    if (stmt->as.if_stmt.else_branch) {
        cm_string_append(cg->output, " else {");
        cm_codegen_v2_newline(cg);

        cg->indent_level++;
        {
            cm_ast_v2_node_t* s = stmt->as.if_stmt.else_branch;
            while (s) { cm_codegen_v2_generate_stmt(cg, s); s = s->next; }
        }
        cg->indent_level--;

        cm_codegen_v2_indent(cg);
        cm_string_append(cg->output, "}");
    }
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_while(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "while (");
    cm_codegen_v2_generate_expr(cg, stmt->as.while_stmt.condition);
    cm_string_append(cg->output, ") {");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;
    {
        cm_ast_v2_node_t* s = stmt->as.while_stmt.body;
        while (s) { cm_codegen_v2_generate_stmt(cg, s); s = s->next; }
    }
    cg->indent_level--;

    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "}");
    cm_codegen_v2_newline(cg);
}

/* for i in collection { body }
 * Emits: for (int _cm_i = 0; _cm_i < cm_len(collection); _cm_i++) {
 *            const _elem_type cm_i = collection[_cm_i];
 *            <body>
 *        }
 * This gives Go-style ergonomics with C-speed semantics. */
static void cm_codegen_v2_generate_for(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    const char* var = stmt->as.for_stmt.var_name
                    ? stmt->as.for_stmt.var_name->data : "_i";
    char idx_buf[64];
    snprintf(idx_buf, sizeof(idx_buf), "_cm_idx_%d", cg->temp_counter++);

    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "{ /* for ");
    cm_string_append(cg->output, var);
    cm_string_append(cg->output, " in ... */");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;

    /* Emit iterator index */
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "int ");
    cm_string_append(cg->output, idx_buf);
    cm_string_append(cg->output, "; for (");
    cm_string_append(cg->output, idx_buf);
    cm_string_append(cg->output, " = 0; ");
    cm_string_append(cg->output, idx_buf);
    cm_string_append(cg->output, " < (int)cm_len(");
    cm_codegen_v2_generate_expr(cg, stmt->as.for_stmt.iterable);
    cm_string_append(cg->output, "); ");
    cm_string_append(cg->output, idx_buf);
    cm_string_append(cg->output, "++) {");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;

    /* Loop variable binding */
    {
        cm_string_t* mangled = cm_codegen_v2_mangle(var);
        cm_codegen_v2_indent(cg);
        cm_string_append(cg->output, "int ");
        cm_string_append(cg->output, mangled->data);
        cm_string_append(cg->output, " = (");
        cm_codegen_v2_generate_expr(cg, stmt->as.for_stmt.iterable);
        cm_string_append(cg->output, ")[");
        cm_string_append(cg->output, idx_buf);
        cm_string_append(cg->output, "];");
        cm_codegen_v2_newline(cg);
        cm_string_free(mangled);
    }

    {
        cm_ast_v2_node_t* s = stmt->as.for_stmt.body;
        while (s) { cm_codegen_v2_generate_stmt(cg, s); s = s->next; }
    }

    cg->indent_level--;
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "} /* end for loop */");
    cm_codegen_v2_newline(cg);

    cg->indent_level--;
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "}");
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_match(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    char tmp[32];
    int  tmp_id = cg->temp_counter++;

    snprintf(tmp, sizeof(tmp), "_cm_match_%d", tmp_id);

    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "{ /* match */");
    cm_codegen_v2_newline(cg);

    cg->indent_level++;

    /* BUG FIX: was using 'auto' (C++ / GNU extension, invalid in C99).
     * Use 'int' as the match value type — a limitation for now. */
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "int ");
    cm_string_append(cg->output, tmp);
    cm_string_append(cg->output, " = (int)(");
    cm_codegen_v2_generate_expr(cg, stmt->as.match_expr.expr);
    cm_string_append(cg->output, ");");
    cm_codegen_v2_newline(cg);

    {
        cm_ast_v2_node_t* arm   = stmt->as.match_expr.arms;
        int               first = 1;

        while (arm) {
            if (arm->kind == CM_AST_V2_MATCH_ARM) {
                int is_catch_all = 0;
                if (arm->as.match_arm.pattern->kind == CM_AST_V2_IDENTIFIER &&
                    strcmp(arm->as.match_arm.pattern->as.identifier.value->data, "_") == 0) {
                    is_catch_all = 1;
                }

                cm_codegen_v2_indent(cg);
                if (is_catch_all) {
                    cm_string_append(cg->output, first ? "if (1) {" : "else {");
                } else {
                    cm_string_append(cg->output, first ? "if (" : "else if (");
                    cm_codegen_v2_generate_expr(cg, arm->as.match_arm.pattern);
                    cm_string_append(cg->output, " == ");
                    cm_string_append(cg->output, tmp);
                    cm_string_append(cg->output, ") {");
                }
                first = 0;
                cm_codegen_v2_newline(cg);

                cg->indent_level++;
                cm_codegen_v2_indent(cg);
                cm_codegen_v2_generate_expr(cg, arm->as.match_arm.expr);
                cm_string_append(cg->output, ";");
                cm_codegen_v2_newline(cg);
                cg->indent_level--;

                cm_codegen_v2_indent(cg);
                cm_string_append(cg->output, "}");
                cm_codegen_v2_newline(cg);
            }
            arm = arm->next;
        }
    }

    cg->indent_level--;
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "} /* end match */");
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_assign(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    /* If the LHS is an index expression, emit the bounds check first */
    if (stmt->as.assign_stmt.target &&
        stmt->as.assign_stmt.target->kind == CM_AST_V2_INDEX) {
        cm_codegen_v2_generate_index_stmt(cg, stmt->as.assign_stmt.target);
    }

    cm_codegen_v2_indent(cg);
    cm_codegen_v2_generate_expr(cg, stmt->as.assign_stmt.target);
    cm_string_append(cg->output, " = ");
    cm_codegen_v2_generate_expr(cg, stmt->as.assign_stmt.value);
    cm_string_append(cg->output, ";");
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_return(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    cm_codegen_v2_indent(cg);
    cm_string_append(cg->output, "return");
    if (stmt->as.return_stmt.value) {
        cm_string_append(cg->output, " ");
        cm_codegen_v2_generate_expr(cg, stmt->as.return_stmt.value);
    }
    cm_string_append(cg->output, ";");
    cm_codegen_v2_newline(cg);
}

static void cm_codegen_v2_generate_stmt(cm_codegen_v2_t* cg, cm_ast_v2_node_t* stmt) {
    if (!stmt) return;

    switch (stmt->kind) {
        case CM_AST_V2_FN:     cm_codegen_v2_generate_fn(cg, stmt);          break;
        case CM_AST_V2_LET:    cm_codegen_v2_generate_let_mut(cg, stmt, 0);  break;
        case CM_AST_V2_MUT:    cm_codegen_v2_generate_let_mut(cg, stmt, 1);  break;
        case CM_AST_V2_STRUCT: cm_codegen_v2_generate_struct(cg, stmt);      break;
        case CM_AST_V2_IF:     cm_codegen_v2_generate_if(cg, stmt);          break;
        case CM_AST_V2_WHILE:  cm_codegen_v2_generate_while(cg, stmt);       break;
        case CM_AST_V2_FOR:    cm_codegen_v2_generate_for(cg, stmt);         break;
        case CM_AST_V2_MATCH:  cm_codegen_v2_generate_match(cg, stmt);       break;
        case CM_AST_V2_ASSIGN: cm_codegen_v2_generate_assign(cg, stmt);      break;
        case CM_AST_V2_RETURN: cm_codegen_v2_generate_return(cg, stmt);      break;
        case CM_AST_V2_IMPL: {
            cm_ast_v2_node_t* m = stmt->as.impl_decl.methods;
            while (m) {
                cm_string_t* old_name = m->as.fn_decl.name;
                m->as.fn_decl.name = cm_string_format("%s_%s", stmt->as.impl_decl.target_type->as.type_named.name->data, old_name->data);
                cm_codegen_v2_generate_fn(cg, m);
                cm_string_free(m->as.fn_decl.name);
                m->as.fn_decl.name = old_name;
                m = m->next;
            }
            break;
        }

        case CM_AST_V2_BREAK:
            cm_codegen_v2_indent(cg);
            cm_string_append(cg->output, "break;");
            cm_codegen_v2_newline(cg);
            break;

        case CM_AST_V2_CONTINUE:
            cm_codegen_v2_indent(cg);
            cm_string_append(cg->output, "continue;");
            cm_codegen_v2_newline(cg);
            break;

        case CM_AST_V2_EXPR_STMT:
            /* If the expression is an index, emit bounds check before use */
            if (stmt->as.expr_stmt.expr &&
                stmt->as.expr_stmt.expr->kind == CM_AST_V2_INDEX) {
                cm_codegen_v2_generate_index_stmt(cg, stmt->as.expr_stmt.expr);
            }
            cm_codegen_v2_indent(cg);
            cm_codegen_v2_generate_expr(cg, stmt->as.expr_stmt.expr);
            cm_string_append(cg->output, ";");
            cm_codegen_v2_newline(cg);
            break;

        default:
            cm_codegen_v2_indent(cg);
            cm_string_append(cg->output, "/* unknown statement */");
            cm_codegen_v2_newline(cg);
            break;
    }
}

/* ============================================================================
 * Public API: generate C99 code from a parsed AST
 * ==========================================================================*/

cm_string_t* cm_codegen_v2_to_c(const cm_ast_v2_list_t* ast) {
    cm_codegen_v2_t cg;
    cm_codegen_v2_init(&cg);

    /* Pre-pass: Collect OOP methods for unique method dispatch */
    for (cm_ast_v2_node_t* n = ast->head; n; n = n->next) {
        if (n->kind == CM_AST_V2_IMPL) {
            const char* t_name = n->as.impl_decl.target_type->as.type_named.name->data;
            for (cm_ast_v2_node_t* m = n->as.impl_decl.methods; m; m = m->next) {
                if (cg.method_count < 512) {
                    cg.methods[cg.method_count].method_name = m->as.fn_decl.name->data;
                    cg.methods[cg.method_count].type_name = t_name;
                    cg.method_count++;
                }
            }
        }
    }

    /* Preamble */
    cm_string_append(cg.output, "/* Generated by CM v2 compiler — do not edit */\n");
    cm_string_append(cg.output, "#include \"cm/core.h\"\n");
    cm_string_append(cg.output, "#include \"cm/memory.h\"\n");
    cm_string_append(cg.output, "#include \"cm/string.h\"\n");
    cm_string_append(cg.output, "#include \"cm/safe_ptr.h\"\n");
    cm_string_append(cg.output, "#include \"cm/option.h\"\n");
    cm_string_append(cg.output, "#include \"cm/result.h\"\n");
    cm_string_append(cg.output, "#include \"cm/http.h\"\n");
    cm_string_append(cg.output, "#include <stdio.h>\n");
    cm_string_append(cg.output, "#include <stdlib.h>\n");
    cm_string_append(cg.output, "\n");

    /* All top-level statements */
    {
        cm_ast_v2_node_t* s = ast->head;
        while (s) {
            cm_codegen_v2_generate_stmt(&cg, s);
            s = s->next;
        }
    }

    /* Boilerplate main() that calls user's cm_main() */
    cm_string_append(cg.output,
        "\nint main(int argc, char** argv) {\n"
        "    (void)argc; (void)argv;\n"
        "    cm_gc_init();\n"
        "    cm_main();\n"
        "    cm_gc_shutdown();\n"
        "    return 0;\n"
        "}\n");

    cm_codegen_v2_destroy(&cg);
    return cg.output;
}
