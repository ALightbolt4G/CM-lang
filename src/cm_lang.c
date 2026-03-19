#include "cm/cm_lang.h"
#include "cm/memory.h"
#include "cm/error.h"
#include "cm/file.h"
#include "cm/cmd.h"
#include "cm_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#else
/* On Windows, ship or depend on a dirent/stat compatibility layer if needed. */
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#endif

/* ============================================================================
 * Lexer
 * ==========================================================================*/

typedef struct {
    const char* src;
    size_t      length;
    size_t      pos;
    size_t      line;
    size_t      column;
} cm_lexer_t;

static int cm_lexer_peek(cm_lexer_t* lx) {
    if (lx->pos >= lx->length) return EOF;
    return (unsigned char)lx->src[lx->pos];
}

static int cm_lexer_next(cm_lexer_t* lx) {
    if (lx->pos >= lx->length) return EOF;
    int c = (unsigned char)lx->src[lx->pos++];
    if (c == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    return c;
}

static void cm_lexer_init(cm_lexer_t* lx, const char* src) {
    memset(lx, 0, sizeof(*lx));
    lx->src    = src;
    lx->length = src ? strlen(src) : 0;
    lx->line   = 1;
    lx->column = 1;
}

static int cm_is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int cm_is_ident_part(int c) {
    return isalnum(c) || c == '_';
}

static cm_token_t cm_make_token(cm_token_kind_t kind, const char* text, size_t len,
                                size_t line, size_t col) {
    cm_token_t tok;
    tok.kind   = kind;
    tok.lexeme = cm_string_new(NULL);
    if (text && len > 0) {
        char* tmp = (char*)malloc(len + 1);
        if (tmp) {
            memcpy(tmp, text, len);
            tmp[len] = '\0';
            cm_string_set(tok.lexeme, tmp);
            free(tmp);
        }
    }
    tok.line   = line;
    tok.column = col;
    return tok;
}

static void cm_token_free(cm_token_t* tok) {
    if (!tok) return;
    if (tok->lexeme) {
        cm_string_free(tok->lexeme);
        tok->lexeme = NULL;
    }
}

static void cm_lexer_skip_ws_and_comments(cm_lexer_t* lx) {
    int c;
    for (;;) {
        c = cm_lexer_peek(lx);
        if (isspace(c)) {
            cm_lexer_next(lx);
            continue;
        }
        if (c == '/' && lx->pos + 1 < lx->length && lx->src[lx->pos + 1] == '/') {
            while ((c = cm_lexer_next(lx)) != EOF && c != '\n') {}
            continue;
        }
        break;
    }
}

static cm_token_t cm_lexer_next_token(cm_lexer_t* lx) {
    cm_lexer_skip_ws_and_comments(lx);
    size_t start_pos = lx->pos;
    size_t start_line = lx->line;
    size_t start_col  = lx->column;

    int c = cm_lexer_next(lx);
    if (c == EOF) {
        return cm_make_token(CM_TOK_EOF, NULL, 0, lx->line, lx->column);
    }

    switch (c) {
        case '(': return cm_make_token(CM_TOK_LPAREN, "(", 1, start_line, start_col);
        case ')': return cm_make_token(CM_TOK_RPAREN, ")", 1, start_line, start_col);
        case '{': return cm_make_token(CM_TOK_LBRACE, "{", 1, start_line, start_col);
        case '}': return cm_make_token(CM_TOK_RBRACE, "}", 1, start_line, start_col);
        case '[': return cm_make_token(CM_TOK_LBRACKET, "[", 1, start_line, start_col);
        case ']': return cm_make_token(CM_TOK_RBRACKET, "]", 1, start_line, start_col);
        case ';': return cm_make_token(CM_TOK_SEMI,   ";", 1, start_line, start_col);
        case ',': return cm_make_token(CM_TOK_COMMA,  ",", 1, start_line, start_col);
        case ':': return cm_make_token(CM_TOK_COLON,  ":", 1, start_line, start_col);
        case '=':
            if (cm_lexer_peek(lx) == '=') { cm_lexer_next(lx); return cm_make_token(CM_TOK_EQUAL, "==", 2, start_line, start_col); }
            if (cm_lexer_peek(lx) == '>') { cm_lexer_next(lx); return cm_make_token(CM_TOK_FAT_ARROW, "=>", 2, start_line, start_col); }
            return cm_make_token(CM_TOK_EQUAL,  "=", 1, start_line, start_col);
        case '+': return cm_make_token(CM_TOK_PLUS,   "+", 1, start_line, start_col);
        case '-':
            if (cm_lexer_peek(lx) == '>') { cm_lexer_next(lx); return cm_make_token(CM_TOK_ARROW, "->", 2, start_line, start_col); }
            return cm_make_token(CM_TOK_MINUS,  "-", 1, start_line, start_col);
        case '*': return cm_make_token(CM_TOK_STAR,   "*", 1, start_line, start_col);
        case '/': return cm_make_token(CM_TOK_SLASH,  "/", 1, start_line, start_col);
        case '.': return cm_make_token(CM_TOK_DOT,    ".", 1, start_line, start_col);
        case '<': return cm_make_token(CM_TOK_LT,     "<", 1, start_line, start_col);
        case '>': return cm_make_token(CM_TOK_GT,     ">", 1, start_line, start_col);
        case '!': return cm_make_token(CM_TOK_BANG,   "!", 1, start_line, start_col);
        case '&': return cm_make_token(CM_TOK_AMPERSAND, "&", 1, start_line, start_col);
        case '|': return cm_make_token(CM_TOK_PIPE,   "|", 1, start_line, start_col);
        case '"': {
            size_t str_start = lx->pos;
            while (1) {
                int ch = cm_lexer_next(lx);
                if (ch == EOF || ch == '\n') break;
                if (ch == '"') break;
                if (ch == '\\') {
                    cm_lexer_next(lx);
                }
            }
            size_t str_end = lx->pos;
            size_t len = (str_end > str_start) ? (str_end - str_start - 1) : 0;
            const char* base = lx->src + str_start;
            return cm_make_token(CM_TOK_STRING_LITERAL, base, len, start_line, start_col);
        }
        default:
            break;
    }

    if (isdigit(c)) {
        while (isdigit(cm_lexer_peek(lx))) {
            cm_lexer_next(lx);
        }
        size_t end = lx->pos;
        return cm_make_token(CM_TOK_NUMBER, lx->src + start_pos, end - start_pos,
                             start_line, start_col);
    }

    if (cm_is_ident_start(c)) {
        while (cm_is_ident_part(cm_lexer_peek(lx))) {
            cm_lexer_next(lx);
        }
        size_t end = lx->pos;
        size_t len = end - start_pos;
        const char* ident = lx->src + start_pos;

        if (len == 6 && strncmp(ident, "string", 6) == 0) {
            return cm_make_token(CM_TOK_KW_STRING, ident, len, start_line, start_col);
        }
        if (len == 9 && strncmp(ident, "namespace", 9) == 0) {
            return cm_make_token(CM_TOK_KW_NAMESPACE, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "using", 5) == 0) {
            return cm_make_token(CM_TOK_KW_USING, ident, len, start_line, start_col);
        }
        if (len == 6 && strncmp(ident, "public", 6) == 0) {
            return cm_make_token(CM_TOK_KW_PUBLIC, ident, len, start_line, start_col);
        }
        if (len == 7 && strncmp(ident, "private", 7) == 0) {
            return cm_make_token(CM_TOK_KW_PRIVATE, ident, len, start_line, start_col);
        }
        if (len == 6 && strncmp(ident, "static", 6) == 0) {
            return cm_make_token(CM_TOK_KW_STATIC, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "async", 5) == 0) {
            return cm_make_token(CM_TOK_KW_ASYNC, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "await", 5) == 0) {
            return cm_make_token(CM_TOK_KW_AWAIT, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "task", 4) == 0) {
            return cm_make_token(CM_TOK_KW_TASK, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "try", 3) == 0) {
            return cm_make_token(CM_TOK_KW_TRY, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "catch", 5) == 0) {
            return cm_make_token(CM_TOK_KW_CATCH, ident, len, start_line, start_col);
        }
        if (len == 7 && strncmp(ident, "finally", 7) == 0) {
            return cm_make_token(CM_TOK_KW_FINALLY, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "throw", 5) == 0) {
            return cm_make_token(CM_TOK_KW_THROW, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "get", 3) == 0) {
            return cm_make_token(CM_TOK_KW_GET, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "set", 3) == 0) {
            return cm_make_token(CM_TOK_KW_SET, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "void", 4) == 0) {
            return cm_make_token(CM_TOK_KW_VOID, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "int", 3) == 0) {
            return cm_make_token(CM_TOK_KW_INT, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "bool", 4) == 0) {
            return cm_make_token(CM_TOK_KW_BOOL, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "float", 5) == 0) {
            return cm_make_token(CM_TOK_KW_FLOAT, ident, len, start_line, start_col);
        }
        if (len == 6 && strncmp(ident, "double", 6) == 0) {
            return cm_make_token(CM_TOK_KW_DOUBLE, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "true", 4) == 0) {
            return cm_make_token(CM_TOK_KW_TRUE, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "false", 5) == 0) {
            return cm_make_token(CM_TOK_KW_FALSE, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "null", 4) == 0) {
            return cm_make_token(CM_TOK_KW_NULL, ident, len, start_line, start_col);
        }
        if (len == 6 && strncmp(ident, "import", 6) == 0) {
            return cm_make_token(CM_TOK_KW_IMPORT, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "from", 4) == 0) {
            return cm_make_token(CM_TOK_KW_FROM, ident, len, start_line, start_col);
        }
        if (len == 7 && strncmp(ident, "package", 7) == 0) {
            return cm_make_token(CM_TOK_KW_PACKAGE, ident, len, start_line, start_col);
        }
        if (len == 9 && strncmp(ident, "interface", 9) == 0) {
            return cm_make_token(CM_TOK_KW_INTERFACE, ident, len, start_line, start_col);
        }
        if (len == 10 && strncmp(ident, "implements", 10) == 0) {
            return cm_make_token(CM_TOK_KW_IMPLEMENTS, ident, len, start_line, start_col);
        }
        if (len == 7 && strncmp(ident, "extends", 7) == 0) {
            return cm_make_token(CM_TOK_KW_EXTENDS, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "input", 5) == 0) {
            return cm_make_token(CM_TOK_KW_INPUT, ident, len, start_line, start_col);
        }
        if (len == 7 && strncmp(ident, "require", 7) == 0) {
            return cm_make_token(CM_TOK_KW_REQUIRE, ident, len, start_line, start_col);
        }
        if (len == 2 && strncmp(ident, "if", 2) == 0) {
            return cm_make_token(CM_TOK_KW_IF, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "else", 4) == 0) {
            return cm_make_token(CM_TOK_KW_ELSE, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "while", 5) == 0) {
            return cm_make_token(CM_TOK_KW_WHILE, ident, len, start_line, start_col);
        }
        if (len == 6 && strncmp(ident, "return", 6) == 0) {
            return cm_make_token(CM_TOK_KW_RETURN, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "map", 3) == 0) {
            return cm_make_token(CM_TOK_KW_MAP, ident, len, start_line, start_col);
        }
        if (len == 2 && strncmp(ident, "gc", 2) == 0) {
            return cm_make_token(CM_TOK_KW_GC, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "list", 4) == 0) {
            return cm_make_token(CM_TOK_KW_LIST, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "array", 5) == 0) {
            return cm_make_token(CM_TOK_KW_ARRAY, ident, len, start_line, start_col);
        }
        if (len == 5 && strncmp(ident, "class", 5) == 0) {
            return cm_make_token(CM_TOK_KW_CLASS, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "new", 3) == 0) {
            return cm_make_token(CM_TOK_KW_NEW, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "ptr", 3) == 0) {
            return cm_make_token(CM_TOK_KW_PTR, ident, len, start_line, start_col);
        }
        if (len == 3 && strncmp(ident, "str", 3) == 0) {
            return cm_make_token(CM_TOK_KW_STR, ident, len, start_line, start_col);
        }
        if (len == 6 && strncmp(ident, "malloc", 6) == 0) {
            return cm_make_token(CM_TOK_KW_MALLOC, ident, len, start_line, start_col);
        }
        if (len == 4 && strncmp(ident, "free", 4) == 0) {
            return cm_make_token(CM_TOK_KW_FREE, ident, len, start_line, start_col);
        }
        if (len == 10 && strncmp(ident, "gc_collect", 10) == 0) {
            return cm_make_token(CM_TOK_KW_GC_COLLECT, ident, len, start_line, start_col);
        }
        if (len == 3 && ident[0] == 'c' && ident[1] == 'p' && ident[2] == 'p') {
            cm_lexer_skip_ws_and_comments(lx);
            int brace = cm_lexer_next(lx);
            if (brace != '{') {
                return cm_make_token(CM_TOK_IDENTIFIER, ident, len, start_line, start_col);
            }
            size_t code_start = lx->pos;
            int depth = 1;
            while (lx->pos < lx->length && depth > 0) {
                int ch = cm_lexer_next(lx);
                if (ch == '{') depth++;
                else if (ch == '}') depth--;
            }
            size_t code_end = (depth == 0) ? (lx->pos - 1) : lx->pos;
            if (code_end < code_start) code_end = code_start;
            size_t code_len = code_end - code_start;
            return cm_make_token(CM_TOK_CPP_BLOCK, lx->src + code_start, code_len,
                                 start_line, start_col);
        }
        if (len == 1 && ident[0] == 'c') {
            cm_lexer_skip_ws_and_comments(lx);
            int brace = cm_lexer_next(lx);
            if (brace != '{') {
                return cm_make_token(CM_TOK_IDENTIFIER, ident, len, start_line, start_col);
            }
            size_t code_start = lx->pos;
            int depth = 1;
            while (lx->pos < lx->length && depth > 0) {
                int ch = cm_lexer_next(lx);
                if (ch == '{') depth++;
                else if (ch == '}') depth--;
            }
            size_t code_end = (depth == 0) ? (lx->pos - 1) : lx->pos;
            if (code_end < code_start) code_end = code_start;
            size_t code_len = code_end - code_start;
            return cm_make_token(CM_TOK_C_BLOCK, lx->src + code_start, code_len,
                                 start_line, start_col);
        }

        return cm_make_token(CM_TOK_IDENTIFIER, ident, len, start_line, start_col);
    }

    char ch = (char)c;
    return cm_make_token(CM_TOK_IDENTIFIER, &ch, 1, start_line, start_col);
}

/* ============================================================================
 * AST
 * ==========================================================================*/

typedef enum {
    CM_AST_REQUIRE,
    CM_AST_VAR_DECL,
    CM_AST_EXPR_STMT,
    CM_AST_POLYGLOT,
    CM_AST_NAMESPACE,
    CM_AST_CLASS,
    CM_AST_METHOD,
    CM_AST_PROPERTY,
    CM_AST_IMPORT,
    CM_AST_TRY_CATCH,
    CM_AST_THROW,
    CM_AST_ASYNC_AWAIT,
    CM_AST_ATTRIBUTE,
    CM_AST_INTERFACE,
    CM_AST_IMPLEMENTS,
    CM_AST_GENERIC_TYPE
} cm_ast_kind_t;

typedef enum {
    CM_POLY_C,
    CM_POLY_CPP
} cm_poly_kind_t;

typedef struct cm_ast_node cm_ast_node_t;

struct cm_ast_node {
    cm_ast_kind_t kind;
    size_t        line;
    size_t        column;

    union {
        struct {
            cm_string_t* path;
        } require_stmt;

        struct {
            cm_string_t* type_name;
            cm_string_t* var_name;
            cm_string_t* init_expr;
        } var_decl;

        struct {
            cm_string_t* expr_text;
        } expr_stmt;

        struct {
            cm_poly_kind_t lang;
            cm_string_t*   code;
        } poly_block;

        struct { cm_string_t* name; cm_ast_node_t* body; } namespace_decl;
        struct {
            cm_string_t* name;
            cm_string_t* parent_class;
            cm_ast_node_t* members;
            int is_public;
        } class_decl;
        struct {
            cm_string_t* name;
            cm_string_t* return_type;
            cm_ast_node_t* params;
            cm_ast_node_t* body;
            int is_public;
            int is_static;
            int is_async;
        } method_decl;
        struct {
            cm_string_t* name;
            cm_string_t* type;
            cm_ast_node_t* getter;
            cm_ast_node_t* setter;
            int is_public;
        } property_decl;
        struct { cm_string_t* module; cm_string_t* symbol; } import_stmt;
        struct {
            cm_ast_node_t* try_body;
            cm_ast_node_t* catch_body;
            cm_string_t* catch_var;
        } try_catch;
        struct { cm_string_t* expr; } throw_stmt;
        struct { cm_ast_node_t* expr; } async_await;
        struct { cm_string_t* name; cm_ast_node_t* args; } attribute_decl;
        struct { cm_string_t* name; cm_ast_node_t* methods; } interface_decl;
        struct { cm_string_t* base; cm_ast_node_t* interfaces; } implements_decl;
        struct { cm_string_t* name; cm_ast_node_t* type_params; } generic_type;
    } as;

    cm_ast_node_t* next;
};

struct cm_ast_list {
    cm_ast_node_t* head;
    cm_ast_node_t* tail;
};

static cm_ast_node_t* cm_ast_new(cm_ast_kind_t kind, size_t line, size_t col) {
    cm_ast_node_t* n = (cm_ast_node_t*)cm_alloc(sizeof(cm_ast_node_t), "cm_ast_node");
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->kind   = kind;
    n->line   = line;
    n->column = col;
    return n;
}

static void cm_ast_list_append(cm_ast_list_t* list, cm_ast_node_t* n) {
    if (!list || !n) return;
    if (!list->head) {
        list->head = list->tail = n;
    } else {
        list->tail->next = n;
        list->tail = n;
    }
}

static void cm_ast_free_list(cm_ast_list_t* list) {
    if (!list) return;
    cm_ast_node_t* cur = list->head;
    while (cur) {
        cm_ast_node_t* next = cur->next;
        switch (cur->kind) {
            case CM_AST_REQUIRE:
                if (cur->as.require_stmt.path) cm_string_free(cur->as.require_stmt.path);
                break;
            case CM_AST_VAR_DECL:
                if (cur->as.var_decl.type_name) cm_string_free(cur->as.var_decl.type_name);
                if (cur->as.var_decl.var_name) cm_string_free(cur->as.var_decl.var_name);
                if (cur->as.var_decl.init_expr) cm_string_free(cur->as.var_decl.init_expr);
                break;
            case CM_AST_EXPR_STMT:
                if (cur->as.expr_stmt.expr_text) cm_string_free(cur->as.expr_stmt.expr_text);
                break;
            case CM_AST_POLYGLOT:
                if (cur->as.poly_block.code) cm_string_free(cur->as.poly_block.code);
                break;
            case CM_AST_NAMESPACE:
                if (cur->as.namespace_decl.name) cm_string_free(cur->as.namespace_decl.name);
                break;
            case CM_AST_CLASS:
                if (cur->as.class_decl.name) cm_string_free(cur->as.class_decl.name);
                if (cur->as.class_decl.parent_class) cm_string_free(cur->as.class_decl.parent_class);
                break;
            case CM_AST_METHOD:
                if (cur->as.method_decl.name) cm_string_free(cur->as.method_decl.name);
                if (cur->as.method_decl.return_type) cm_string_free(cur->as.method_decl.return_type);
                break;
            case CM_AST_PROPERTY:
                if (cur->as.property_decl.name) cm_string_free(cur->as.property_decl.name);
                if (cur->as.property_decl.type) cm_string_free(cur->as.property_decl.type);
                break;
            case CM_AST_IMPORT:
                if (cur->as.import_stmt.module) cm_string_free(cur->as.import_stmt.module);
                if (cur->as.import_stmt.symbol) cm_string_free(cur->as.import_stmt.symbol);
                break;
            case CM_AST_TRY_CATCH:
                if (cur->as.try_catch.catch_var) cm_string_free(cur->as.try_catch.catch_var);
                break;
            case CM_AST_THROW:
                if (cur->as.throw_stmt.expr) cm_string_free(cur->as.throw_stmt.expr);
                break;
            case CM_AST_ATTRIBUTE:
                if (cur->as.attribute_decl.name) cm_string_free(cur->as.attribute_decl.name);
                break;
            case CM_AST_INTERFACE:
                if (cur->as.interface_decl.name) cm_string_free(cur->as.interface_decl.name);
                break;
            case CM_AST_IMPLEMENTS:
                if (cur->as.implements_decl.base) cm_string_free(cur->as.implements_decl.base);
                break;
            case CM_AST_GENERIC_TYPE:
                if (cur->as.generic_type.name) cm_string_free(cur->as.generic_type.name);
                break;
            default:
                break;
        }
        cm_free(cur);
        cur = next;
    }
    list->head = list->tail = NULL;
}

/* ============================================================================
 * Parser
 * ==========================================================================*/

typedef struct {
    cm_lexer_t   lexer;
    cm_token_t   current;
} cm_parser_t;

static void cm_parser_init(cm_parser_t* p, const char* src) {
    cm_lexer_init(&p->lexer, src);
    p->current  = cm_lexer_next_token(&p->lexer);
}

static void cm_parser_advance(cm_parser_t* p) {
    cm_token_free(&p->current);
    p->current = cm_lexer_next_token(&p->lexer);
}

static void cm_parser_destroy(cm_parser_t* p) {
    if (!p) return;
    cm_token_free(&p->current);
}

static void cm_expr_append_token(cm_string_t* out, const cm_token_t* tok) {
    if (!out || !tok || !tok->lexeme) return;
    const cm_token_kind_t k = tok->kind;
    const char* s = tok->lexeme->data ? tok->lexeme->data : "";

    /* Avoid injecting spaces around punctuation so desugaring can match. */
    const int is_word =
        (k == CM_TOK_IDENTIFIER) || (k == CM_TOK_NUMBER) || (k == CM_TOK_STRING_LITERAL) ||
        (k == CM_TOK_KW_REQUIRE) || (k == CM_TOK_KW_INPUT) || (k == CM_TOK_KW_STRING);

    if (is_word && out->length > 0) {
        char last = out->data[out->length - 1];
        if (isalnum((unsigned char)last) || last == '_' || last == '"' ) {
            cm_string_append(out, " ");
        }
    }

    if (k == CM_TOK_STRING_LITERAL) {
        cm_string_append(out, "\"");
    }
    cm_string_append(out, s);
    if (k == CM_TOK_STRING_LITERAL) {
        cm_string_append(out, "\"");
    }

    /* Post-fix spacing: keep '.' and '(' tight by not adding spaces at all here.
       Also avoid leaving a space before '.' due to previous word spacing. */
    if (strcmp(s, ".") == 0 && out->length >= 2 && out->data[out->length - 2] == ' ') {
        /* Remove the space before the dot. */
        out->data[out->length - 2] = '.';
        out->data[out->length - 1] = '\0';
        out->length -= 1;
    }
}

static int cm_parser_match(cm_parser_t* p, cm_token_kind_t kind) {
    if (p->current.kind == kind) {
        cm_parser_advance(p);
        return 1;
    }
    return 0;
}

static void cm_parser_expect(cm_parser_t* p, cm_token_kind_t kind, const char* what) {
    if (!cm_parser_match(p, kind)) {
        cm_error_set(CM_ERROR_PARSE, what);
        CM_THROW(CM_ERROR_PARSE, what);
    }
}

static cm_ast_node_t* cm_parse_require(cm_parser_t* p) {
    size_t line = p->current.line;
    size_t col  = p->current.column;
    cm_parser_advance(p);
    cm_parser_expect(p, CM_TOK_LPAREN, "expected '(' after require");
    if (p->current.kind != CM_TOK_STRING_LITERAL) {
        cm_error_set(CM_ERROR_PARSE, "expected string literal path in require()");
        CM_THROW(CM_ERROR_PARSE, "expected string literal path in require()");
    }
    cm_string_t* path = cm_string_new(p->current.lexeme->data);
    cm_parser_advance(p);
    cm_parser_expect(p, CM_TOK_RPAREN, "expected ')' after require path");
    cm_parser_expect(p, CM_TOK_SEMI, "expected ';' after require()");

    cm_ast_node_t* n = cm_ast_new(CM_AST_REQUIRE, line, col);
    if (!n) return NULL;
    n->as.require_stmt.path = path;
    return n;
}

static cm_ast_node_t* cm_parse_simple_stmt(cm_parser_t* p) {
    /* Check for type keywords: string/str, ptr, list, array, map */
    cm_token_kind_t type_kind = p->current.kind;
    const char* type_name = NULL;
    
    if (type_kind == CM_TOK_KW_STRING) type_name = "string";
    else if (type_kind == CM_TOK_KW_STR) type_name = "str";
    else if (type_kind == CM_TOK_KW_PTR) type_name = "ptr";
    else if (type_kind == CM_TOK_KW_LIST) type_name = "list";
    else if (type_kind == CM_TOK_KW_ARRAY) type_name = "array";
    else if (type_kind == CM_TOK_KW_MAP) type_name = "map";
    
    if (type_name) {
        size_t line = p->current.line;
        size_t col  = p->current.column;
        cm_parser_advance(p);
        
        if (type_kind == CM_TOK_KW_PTR) {
            cm_token_kind_t nk = p->current.kind;
            if (nk == CM_TOK_KW_STRING || nk == CM_TOK_KW_STR || nk == CM_TOK_KW_LIST || nk == CM_TOK_KW_ARRAY || nk == CM_TOK_KW_MAP || nk == CM_TOK_IDENTIFIER) {
                cm_parser_advance(p);
            }
        }

        if (p->current.kind != CM_TOK_IDENTIFIER) {
            cm_error_set(CM_ERROR_PARSE, "expected identifier after 'string'");
            CM_THROW(CM_ERROR_PARSE, "expected identifier after 'string'");
        }
        cm_string_t* var = cm_string_new(p->current.lexeme->data);
        cm_parser_advance(p);
        cm_parser_expect(p, CM_TOK_EQUAL, "expected '=' after variable name");

        cm_string_t* expr = cm_string_new("");
        size_t expr_start_line = p->current.line;
        (void)expr_start_line;
        while (p->current.kind != CM_TOK_SEMI && p->current.kind != CM_TOK_EOF) {
            cm_expr_append_token(expr, &p->current);
            cm_parser_advance(p);
        }
        cm_parser_expect(p, CM_TOK_SEMI, "expected ';' after expression");

        cm_ast_node_t* n = cm_ast_new(CM_AST_VAR_DECL, line, col);
        if (!n) {
            cm_string_free(var);
            cm_string_free(expr);
            return NULL;
        }
        n->as.var_decl.type_name = cm_string_new(type_name);
        n->as.var_decl.var_name  = var;
        n->as.var_decl.init_expr = expr;
        return n;
    }

    if (p->current.kind == CM_TOK_CPP_BLOCK) {
        cm_ast_node_t* n = cm_ast_new(CM_AST_POLYGLOT, p->current.line, p->current.column);
        if (!n) return NULL;
        n->as.poly_block.lang = CM_POLY_CPP;
        n->as.poly_block.code = cm_string_new(p->current.lexeme ? p->current.lexeme->data : "");
        cm_parser_advance(p);
        return n;
    }

    if (p->current.kind == CM_TOK_C_BLOCK) {
        cm_ast_node_t* n = cm_ast_new(CM_AST_POLYGLOT, p->current.line, p->current.column);
        if (!n) return NULL;
        n->as.poly_block.lang = CM_POLY_C;
        n->as.poly_block.code = cm_string_new(p->current.lexeme ? p->current.lexeme->data : "");
        cm_parser_advance(p);
        return n;
    }

    size_t line = p->current.line;
    size_t col  = p->current.column;
    cm_string_t* expr = cm_string_new("");
    
    int brace_depth = 0;
    int is_block = 0;

    while (p->current.kind != CM_TOK_EOF) {
        if (p->current.kind == CM_TOK_LBRACE) {
            brace_depth++;
            is_block = 1;
        } else if (p->current.kind == CM_TOK_RBRACE) {
            brace_depth--;
        }

        cm_expr_append_token(expr, &p->current);
        cm_parser_advance(p);

        if (brace_depth == 0) {
            if (is_block) {
                break;
            } else if (p->current.kind == CM_TOK_SEMI) {
                break;
            }
        }
    }
    if (!is_block && p->current.kind == CM_TOK_SEMI) {
        cm_parser_advance(p);
    }

    cm_ast_node_t* n = cm_ast_new(CM_AST_EXPR_STMT, line, col);
    if (!n) {
        cm_string_free(expr);
        return NULL;
    }
    n->as.expr_stmt.expr_text = expr;

    return n;
}

static cm_ast_list_t cm_parser_parse(cm_parser_t* p) {
    cm_ast_list_t list = {0};
    while (p->current.kind != CM_TOK_EOF) {
        if (p->current.kind == CM_TOK_KW_REQUIRE) {
            cm_ast_node_t* req = cm_parse_require(p);
            cm_ast_list_append(&list, req);
            continue;
        }
        cm_ast_node_t* stmt = cm_parse_simple_stmt(p);
        if (stmt) cm_ast_list_append(&list, stmt);
    }
    return list;
}

/* ============================================================================
 * Directory / module loading helpers
 * ==========================================================================*/

static int cm_is_directory(const char* path) {
#ifndef _WIN32
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
#else
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (st.st_mode & _S_IFDIR) != 0;
#endif
}

typedef struct cm_module_list {
    cm_string_t** items;
    size_t        count;
    size_t        capacity;
} cm_module_list_t;

static void cm_module_list_init(cm_module_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void cm_module_list_append(cm_module_list_t* list, const char* path) {
    if (!list || !path) return;
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 8;
        cm_string_t** tmp = (cm_string_t**)cm_alloc(new_cap * sizeof(cm_string_t*), "cm_module_list");
        if (!tmp) return;
        if (list->items && list->count) {
            memcpy(tmp, list->items, list->count * sizeof(cm_string_t*));
        }
        list->items = tmp;
        list->capacity = new_cap;
    }
    list->items[list->count++] = cm_string_new(path);
}

static void cm_module_list_free(cm_module_list_t* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->items[i]) cm_string_free(list->items[i]);
    }
    if (list->items) cm_free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static void cm_scan_directory_for_cm(const char* dir, cm_module_list_t* out) {
    if (!dir || !out) return;
#ifndef _WIN32
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len > 3 && strcmp(name + len - 3, ".cm") == 0) {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, name);
            cm_module_list_append(out, full);
        }
    }
    closedir(d);
#else
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.cm", dir);
    struct _finddata_t data;
    intptr_t handle = _findfirst(pattern, &data);
    if (handle == -1L) return;
    do {
        char full[1024];
        snprintf(full, sizeof(full), "%s\\%s", dir, data.name);
        cm_module_list_append(out, full);
    } while (_findnext(handle, &data) == 0);
    _findclose(handle);
#endif
}

/* ============================================================================
 * Transpiler / Codegen: CM AST -> Hardened C
 * ==========================================================================*/

/* Builtin blacklist for unsafe C APIs in CM code (not polyglot blocks). */
static const char* CM_BLACKLISTED_FUNCS[] = {
    "strcpy", "strcat", "sprintf", "snprintf",
    "scanf", "gets", "fgets",
    /* Note: malloc/free are allowed - they get converted to cm_alloc/cm_free */
    "printf",
    NULL
};

/* Check if 'word' appears as a complete word in 'text' (not as substring) */
static int cm_contains_word(const char* text, const char* word) {
    if (!text || !word) return 0;
    size_t word_len = strlen(word);
    if (word_len == 0) return 0;
    
    const char* p = text;
    while ((p = strstr(p, word)) != NULL) {
        /* Check if preceded by word boundary (start or non-alnum/_ char) */
        int prev_ok = (p == text) || !isalnum((unsigned char)p[-1]) && p[-1] != '_';
        /* Check if followed by word boundary (end or non-alnum/_ char or '(') */
        int next_ok = !isalnum((unsigned char)p[word_len]) && p[word_len] != '_';
        
        if (prev_ok && next_ok) {
            return 1;
        }
        p += word_len;
    }
    return 0;
}

static void cm_check_blacklist_string(const cm_string_t* s) {
    if (!s || !s->data) return;
    for (const char* const* p = CM_BLACKLISTED_FUNCS; *p; ++p) {
        if (cm_contains_word(s->data, *p)) {
            cm_string_t* msg = cm_string_format(
                "[CM Compiler] use of banned function '%s' detected. "
                "Use CM safe alternatives (cm_string_*, cm_alloc, cm_map_*, print/input) instead.",
                *p);
            cm_error_set(CM_ERROR_TYPE, msg ? msg->data : "banned function use");
            if (msg) cm_string_free(msg);
            CM_THROW(CM_ERROR_TYPE, "banned function use");
        }
    }
}

/* Walk AST and apply blacklist checks to CM expressions (not polyglot blocks). */
static void cm_check_blacklist_ast(cm_ast_list_t* ast) {
    if (!ast) return;
    for (cm_ast_node_t* n = ast->head; n; n = n->next) {
        switch (n->kind) {
            case CM_AST_VAR_DECL:
                cm_check_blacklist_string(n->as.var_decl.init_expr);
                break;
            case CM_AST_EXPR_STMT:
                cm_check_blacklist_string(n->as.expr_stmt.expr_text);
                break;
            case CM_AST_REQUIRE:
                cm_check_blacklist_string(n->as.require_stmt.path);
                break;
            case CM_AST_POLYGLOT:
                /* Polyglot blocks are passed through verbatim by design. */
                break;
        }
    }
}

static cm_string_t* cm_transpile_ast_to_c(cm_ast_list_t* ast) {
    return cm_codegen_to_c((const cm_ast_list_t*)ast);
}

/* ============================================================================
 * High-level compile pipeline
 * ==========================================================================*/

static char* cm_read_file_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)sz, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

static int cm_write_text_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (text) {
        fwrite(text, 1, strlen(text), f);
    }
    fclose(f);
    return 0;
}

static void cm_try_remove_output(const char* output_exe) {
    if (!output_exe || !output_exe[0]) return;
    remove(output_exe);
#ifdef _WIN32
    /* MinGW produces .exe when -o is basename */
    char buf[512];
    snprintf(buf, sizeof(buf), "%s.exe", output_exe);
    remove(buf);
#endif
}

static int cm_collect_require_modules(cm_ast_list_t* ast, cm_module_list_t* modules) {
    if (!ast || !modules) return 0;
    for (cm_ast_node_t* n = ast->head; n; n = n->next) {
        if (n->kind != CM_AST_REQUIRE) continue;
        const char* path = n->as.require_stmt.path->data;
        if (cm_is_directory(path)) {
            cm_scan_directory_for_cm(path, modules);
            continue;
        }

        /* Try the exact path first. */
        cm_module_list_append(modules, path);

        /* Also attempt resolution in standard search locations: src/ and include/cm/. */
        char buf[1024];
        snprintf(buf, sizeof(buf), "src/%s", path);
        cm_module_list_append(modules, buf);
        snprintf(buf, sizeof(buf), "include/cm/%s", path);
        cm_module_list_append(modules, buf);
    }
    return 0;
}

static int cm_invoke_system_compiler(const char* c_path, const char* output_exe) {
    if (!c_path || !output_exe) return -1;

    /* Enterprise default: compile generated C together with the CM runtime sources,
       so `cm main.cm app` works even without a pre-built libcm present. */
    const char* candidates[] = { "gcc", "clang", "cc", NULL };
    const char* cc = NULL;

    for (int i = 0; candidates[i]; ++i) {
        cm_cmd_t* probe = cm_cmd_new(candidates[i]);
        if (!probe) continue;
        cm_cmd_arg(probe, "--version");
        cm_cmd_result_t* r = cm_cmd_run(probe);
        cm_cmd_free(probe);
        if (r && r->exit_code == 0) {
            cc = candidates[i];
            cm_cmd_result_free(r);
            break;
        }
        if (r) cm_cmd_result_free(r);
    }

    if (!cc) {
        cm_error_set(CM_ERROR_IO, "no C compiler found (expected gcc/clang/cc in PATH)");
        return -1;
    }

    cm_cmd_t* cmd = cm_cmd_new(cc);
    if (!cmd) return -1;

    cm_cmd_arg(cmd, c_path);
    cm_cmd_arg(cmd, "src/runtime/memory.c");
    cm_cmd_arg(cmd, "src/runtime/error.c");
    cm_cmd_arg(cmd, "src/runtime/string.c");
    cm_cmd_arg(cmd, "src/runtime/array.c");
    cm_cmd_arg(cmd, "src/runtime/map.c");
    cm_cmd_arg(cmd, "src/runtime/json.c");
    cm_cmd_arg(cmd, "src/runtime/http.c");
    cm_cmd_arg(cmd, "src/runtime/file.c");
    cm_cmd_arg(cmd, "src/runtime/thread.c");
    cm_cmd_arg(cmd, "src/runtime/core.c");

    cm_cmd_arg(cmd, "-Iinclude");
    cm_cmd_arg(cmd, "-o");
    cm_cmd_arg(cmd, output_exe);
    cm_cmd_arg(cmd, "-Wall");
    cm_cmd_arg(cmd, "-Wextra");

#ifdef _WIN32
    cm_cmd_arg(cmd, "-lws2_32");
#endif

    cm_cmd_result_t* res = cm_cmd_run(cmd);
    int exit_code = res ? res->exit_code : -1;
    if (exit_code != 0) {
        const char* msg = res && res->stderr_output
            ? res->stderr_output->data
            : "system compiler error";
        cm_error_set(CM_ERROR_IO, msg);
    }
    if (res) cm_cmd_result_free(res);
    cm_cmd_free(cmd);
    return exit_code;
}

int cm_compile_file(const char* entry_path, const char* output_exe) {
    if (!entry_path || !output_exe) return -1;

    char* src = cm_read_file_all(entry_path);
    if (!src) {
        cm_error_set(CM_ERROR_IO, "failed to read entry .cm file");
        return -1;
    }

    cm_parser_t parser;
    cm_parser_init(&parser, src);

    cm_ast_list_t ast = {0};
    CM_TRY() {
        ast = cm_parser_parse(&parser);
        /* Enforce safety & blacklist policy after parsing. */
        cm_check_blacklist_ast(&ast);
    } CM_CATCH() {
        cm_parser_destroy(&parser);
        free(src);
        cm_ast_free_list(&ast);
        return -1;
    }
    cm_parser_destroy(&parser);

    cm_module_list_t modules;
    cm_module_list_init(&modules);
    cm_collect_require_modules(&ast, &modules);

    cm_string_t* c_code = cm_transpile_ast_to_c(&ast);

    const char* c_path = "cm_out.c";
    if (cm_write_text_file(c_path, c_code->data) != 0) {
        cm_string_free(c_code);
        cm_module_list_free(&modules);
        cm_ast_free_list(&ast);
        free(src);
        cm_error_set(CM_ERROR_IO, "failed to write intermediate C file");
        return -1;
    }

    cm_try_remove_output(output_exe);
    int rc = cm_invoke_system_compiler(c_path, output_exe);

    cm_string_free(c_code);
    cm_module_list_free(&modules);
    cm_ast_free_list(&ast);
    free(src);

    return rc;
}

int cm_emit_c_file(const char* entry_path, const char* output_c_path) {
    if (!entry_path || !output_c_path) return -1;

    char* src = cm_read_file_all(entry_path);
    if (!src) {
        cm_error_set(CM_ERROR_IO, "failed to read entry .cm file");
        return -1;
    }

    cm_parser_t parser;
    cm_parser_init(&parser, src);

    cm_ast_list_t ast = {0};
    CM_TRY() {
        ast = cm_parser_parse(&parser);
        cm_check_blacklist_ast(&ast);
    } CM_CATCH() {
        cm_parser_destroy(&parser);
        free(src);
        cm_ast_free_list(&ast);
        return -1;
    }
    cm_parser_destroy(&parser);

    cm_string_t* c_code = cm_transpile_ast_to_c(&ast);
    if (cm_write_text_file(output_c_path, c_code->data) != 0) {
        cm_string_free(c_code);
        cm_ast_free_list(&ast);
        free(src);
        cm_error_set(CM_ERROR_IO, "failed to write generated C file");
        return -1;
    }

    cm_string_free(c_code);
    cm_ast_free_list(&ast);
    free(src);
    return 0;
}

