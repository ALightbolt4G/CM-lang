#include "cm/cm_lang.h"
#include "cm/memory.h"
#include "cm/error.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Lexer
 * ==========================================================================*/



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

void cm_lexer_init(cm_lexer_t* lx, const char* src) {
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

cm_token_t cm_lexer_next_token(cm_lexer_t* lx) {
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
        case ';': return cm_make_token(CM_TOK_SEMI,   ";", 1, start_line, start_col);
        case ',': return cm_make_token(CM_TOK_COMMA,  ",", 1, start_line, start_col);
        case '=': 
            if (cm_lexer_peek(lx) == '=') { cm_lexer_next(lx); return cm_make_token(CM_TOK_EQUAL, "==", 2, start_line, start_col); }
            break;
            if (cm_lexer_peek(lx) == '>') { cm_lexer_next(lx); return cm_make_token(CM_TOK_EQUAL, "=>", 2, start_line, start_col); }
            break;
            return cm_make_token(CM_TOK_EQUAL,  "=", 1, start_line, start_col);
        case '+': return cm_make_token(CM_TOK_PLUS,   "+", 1, start_line, start_col);
        case '-': return cm_make_token(CM_TOK_MINUS,  "-", 1, start_line, start_col);
        case '*': return cm_make_token(CM_TOK_STAR,   "*", 1, start_line, start_col);
        case '/': return cm_make_token(CM_TOK_SLASH,  "/", 1, start_line, start_col);
        case '.': return cm_make_token(CM_TOK_DOT,    ".", 1, start_line, start_col);
        case '<': 
            if (cm_lexer_peek(lx) == '=') { cm_lexer_next(lx); return cm_make_token(CM_TOK_LT, "<=", 2, start_line, start_col); }
            break;
            return cm_make_token(CM_TOK_LT,     "<", 1, start_line, start_col);
        case '>': 
            if (cm_lexer_peek(lx) == '=') { cm_lexer_next(lx); return cm_make_token(CM_TOK_GT, ">=", 2, start_line, start_col); }
            break;
            return cm_make_token(CM_TOK_GT,     ">", 1, start_line, start_col);
        case '!': 
            if (cm_lexer_peek(lx) == '=') { cm_lexer_next(lx); return cm_make_token(CM_TOK_BANG, "!=", 2, start_line, start_col); }
            break;
            return cm_make_token(CM_TOK_BANG,   "!", 1, start_line, start_col);
        case '&': 
            if (cm_lexer_peek(lx) == '&') { cm_lexer_next(lx); return cm_make_token(CM_TOK_AMPERSAND, "&&", 2, start_line, start_col); }
            break;
            return cm_make_token(CM_TOK_AMPERSAND, "&", 1, start_line, start_col);
        case '|': 
            if (cm_lexer_peek(lx) == '|') { cm_lexer_next(lx); return cm_make_token(CM_TOK_PIPE, "||", 2, start_line, start_col); }
            break;
            return cm_make_token(CM_TOK_PIPE,   "|", 1, start_line, start_col);
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

