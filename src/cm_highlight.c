#include "cm/cm_highlight.h"
#include "cm/memory.h"
#include "cm/file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * ANSI Color Codes
 * ========================================================================== */

/* Standard ANSI colors for terminal output */
#define ANSI_RESET       "\033[0m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_GRAY        "\033[90m"
#define ANSI_RED         "\033[31m"
#define ANSI_WHITE       "\033[37m"

const char* cm_color_ansi(cm_color_t color, int use_color) {
    if (!use_color) return "";
    switch (color) {
        case CM_COLOR_KEYWORD:   return ANSI_BLUE;
        case CM_COLOR_STRING:    return ANSI_GREEN;
        case CM_COLOR_NUMBER:    return ANSI_YELLOW;
        case CM_COLOR_FUNCTION:  return ANSI_CYAN;
        case CM_COLOR_METHOD:    return ANSI_MAGENTA;
        case CM_COLOR_COMMENT:   return ANSI_GRAY;
        case CM_COLOR_OPERATOR:  return ANSI_WHITE;
        case CM_COLOR_ERROR:     return ANSI_RED;
        default:                 return "";
    }
}

const char* cm_color_reset(int use_color) {
    return use_color ? ANSI_RESET : "";
}

/* ============================================================================
 * Token Classification
 * ========================================================================== */

static int cm_is_builtin_function(const char* name) {
    if (!name) return 0;
    static const char* builtins[] = {
        "print", "input", "gc", "require", "map", "array", "json", "file", "http", "thread", NULL
    };
    for (const char** p = builtins; *p; ++p) {
        if (strcmp(name, *p) == 0) return 1;
    }
    return 0;
}

cm_color_t cm_token_to_color(cm_token_kind_t kind) {
    switch (kind) {
        /* Keywords */
        case CM_TOK_KW_STRING:
        case CM_TOK_KW_INPUT:
        case CM_TOK_KW_REQUIRE:
        case CM_TOK_KW_IF:
        case CM_TOK_KW_ELSE:
        case CM_TOK_KW_WHILE:
        case CM_TOK_KW_RETURN:
        case CM_TOK_KW_MAP:
        case CM_TOK_KW_GC:
            return CM_COLOR_KEYWORD;

        /* Literals */
        case CM_TOK_STRING_LITERAL:
            return CM_COLOR_STRING;
        case CM_TOK_NUMBER:
            return CM_COLOR_NUMBER;

        /* Function/Method names */
        case CM_TOK_FUNCTION_NAME:
            return CM_COLOR_FUNCTION;
        case CM_TOK_METHOD_NAME:
            return CM_COLOR_METHOD;

        /* Comments */
        case CM_TOK_COMMENT:
            return CM_COLOR_COMMENT;

        /* Operators */
        case CM_TOK_PLUS:
        case CM_TOK_MINUS:
        case CM_TOK_STAR:
        case CM_TOK_SLASH:
        case CM_TOK_DOT:
        case CM_TOK_LT:
        case CM_TOK_GT:
        case CM_TOK_BANG:
        case CM_TOK_AMPERSAND:
        case CM_TOK_PIPE:
        case CM_TOK_EQUAL:
            return CM_COLOR_OPERATOR;

        /* Default for identifiers, punctuation, etc. */
        default:
            return CM_COLOR_DEFAULT;
    }
}

/* ============================================================================
 * Lexer (Internal - mirrors cm_lang.c but tracks comments)
 * ========================================================================== */

typedef struct {
    const char* src;
    size_t      length;
    size_t      pos;
    size_t      line;
    size_t      column;
    int         in_error;
    size_t      error_line;
    size_t      error_column;
} hl_lexer_t;

static void hl_lexer_init(hl_lexer_t* lx, const char* src) {
    memset(lx, 0, sizeof(*lx));
    lx->src    = src;
    lx->length = src ? strlen(src) : 0;
    lx->line   = 1;
    lx->column = 1;
}

static int hl_lexer_peek(hl_lexer_t* lx) {
    if (lx->pos >= lx->length) return EOF;
    return (unsigned char)lx->src[lx->pos];
}

static int hl_lexer_next(hl_lexer_t* lx) {
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

static int hl_is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int hl_is_ident_part(int c) {
    return isalnum(c) || c == '_';
}

static cm_token_t hl_make_token(cm_token_kind_t kind, const char* text, size_t len,
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

static void hl_token_free(cm_token_t* tok) {
    if (!tok) return;
    if (tok->lexeme) {
        cm_string_free(tok->lexeme);
        tok->lexeme = NULL;
    }
}

/* ============================================================================
 * Comment-aware Tokenizer
 * ========================================================================== */

static cm_token_t hl_lexer_next_token(hl_lexer_t* lx) {
    /* Skip whitespace */
    int c;
    for (;;) {
        c = hl_lexer_peek(lx);
        if (!isspace(c)) break;
        hl_lexer_next(lx);
    }

    size_t start_pos = lx->pos;
    size_t start_line = lx->line;
    size_t start_col  = lx->column;

    c = hl_lexer_next(lx);
    if (c == EOF) {
        return hl_make_token(CM_TOK_EOF, NULL, 0, lx->line, lx->column);
    }

    /* Handle comments first - return as COMMENT token */
    if (c == '/' && hl_lexer_peek(lx) == '/') {
        size_t comment_start = lx->pos - 1;
        while ((c = hl_lexer_next(lx)) != EOF && c != '\n') {}
        size_t comment_len = lx->pos - comment_start;
        return hl_make_token(CM_TOK_COMMENT, lx->src + comment_start, comment_len, start_line, start_col);
    }

    switch (c) {
        case '(': return hl_make_token(CM_TOK_LPAREN, "(", 1, start_line, start_col);
        case ')': return hl_make_token(CM_TOK_RPAREN, ")", 1, start_line, start_col);
        case '{': return hl_make_token(CM_TOK_LBRACE, "{", 1, start_line, start_col);
        case '}': return hl_make_token(CM_TOK_RBRACE, "}", 1, start_line, start_col);
        case ';': return hl_make_token(CM_TOK_SEMI,   ";", 1, start_line, start_col);
        case ',': return hl_make_token(CM_TOK_COMMA,  ",", 1, start_line, start_col);
        case '=': return hl_make_token(CM_TOK_EQUAL,  "=", 1, start_line, start_col);
        case '+': return hl_make_token(CM_TOK_PLUS,   "+", 1, start_line, start_col);
        case '-': return hl_make_token(CM_TOK_MINUS,  "-", 1, start_line, start_col);
        case '*': return hl_make_token(CM_TOK_STAR,   "*", 1, start_line, start_col);
        case '.': return hl_make_token(CM_TOK_DOT,    ".", 1, start_line, start_col);
        case '<': return hl_make_token(CM_TOK_LT,     "<", 1, start_line, start_col);
        case '>': return hl_make_token(CM_TOK_GT,     ">", 1, start_line, start_col);
        case '!': return hl_make_token(CM_TOK_BANG,   "!", 1, start_line, start_col);
        case '&': return hl_make_token(CM_TOK_AMPERSAND, "&", 1, start_line, start_col);
        case '|': return hl_make_token(CM_TOK_PIPE,   "|", 1, start_line, start_col);
        case '"': {
            size_t str_start = lx->pos;
            int closed = 0;
            while (1) {
                int ch = hl_lexer_next(lx);
                if (ch == EOF) {
                    lx->in_error = 1;
                    lx->error_line = start_line;
                    lx->error_column = start_col;
                    break;
                }
                if (ch == '\n') {
                    lx->in_error = 1;
                    lx->error_line = start_line;
                    lx->error_column = start_col;
                    break;
                }
                if (ch == '"') {
                    closed = 1;
                    break;
                }
                if (ch == '\\') {
                    hl_lexer_next(lx);
                }
            }
            size_t str_end = lx->pos;
            size_t len = (closed && str_end > str_start) ? (str_end - str_start - 1) : (str_end - str_start);
            const char* base = lx->src + str_start;
            cm_token_t tok = hl_make_token(CM_TOK_STRING_LITERAL, base, len, start_line, start_col);
            if (!closed) {
                lx->in_error = 1;
                lx->error_line = start_line;
                lx->error_column = start_col;
            }
            return tok;
        }
        default:
            break;
    }

    if (isdigit(c)) {
        while (isdigit(hl_lexer_peek(lx))) {
            hl_lexer_next(lx);
        }
        size_t end = lx->pos;
        return hl_make_token(CM_TOK_NUMBER, lx->src + start_pos, end - start_pos, start_line, start_col);
    }

    if (hl_is_ident_start(c)) {
        while (hl_is_ident_part(hl_lexer_peek(lx))) {
            hl_lexer_next(lx);
        }
        size_t end = lx->pos;
        size_t len = end - start_pos;
        const char* ident = lx->src + start_pos;

        /* Check for keywords */
        if (len == 6 && strncmp(ident, "string", 6) == 0)
            return hl_make_token(CM_TOK_KW_STRING, ident, len, start_line, start_col);
        if (len == 5 && strncmp(ident, "input", 5) == 0)
            return hl_make_token(CM_TOK_KW_INPUT, ident, len, start_line, start_col);
        if (len == 7 && strncmp(ident, "require", 7) == 0)
            return hl_make_token(CM_TOK_KW_REQUIRE, ident, len, start_line, start_col);
        if (len == 2 && strncmp(ident, "if", 2) == 0)
            return hl_make_token(CM_TOK_KW_IF, ident, len, start_line, start_col);
        if (len == 4 && strncmp(ident, "else", 4) == 0)
            return hl_make_token(CM_TOK_KW_ELSE, ident, len, start_line, start_col);
        if (len == 5 && strncmp(ident, "while", 5) == 0)
            return hl_make_token(CM_TOK_KW_WHILE, ident, len, start_line, start_col);
        if (len == 6 && strncmp(ident, "return", 6) == 0)
            return hl_make_token(CM_TOK_KW_RETURN, ident, len, start_line, start_col);
        if (len == 3 && strncmp(ident, "map", 3) == 0)
            return hl_make_token(CM_TOK_KW_MAP, ident, len, start_line, start_col);
        if (len == 2 && strncmp(ident, "gc", 2) == 0)
            return hl_make_token(CM_TOK_KW_GC, ident, len, start_line, start_col);

        /* Check for builtin functions - return as FUNCTION_NAME */
        if (cm_is_builtin_function(ident)) {
            return hl_make_token(CM_TOK_FUNCTION_NAME, ident, len, start_line, start_col);
        }

        return hl_make_token(CM_TOK_IDENTIFIER, ident, len, start_line, start_col);
    }

    char ch = (char)c;
    return hl_make_token(CM_TOK_IDENTIFIER, &ch, 1, start_line, start_col);
}

/* ============================================================================
 * Public API
 * ========================================================================== */

cm_highlight_result_t* cm_tokenize_for_highlight(const char* source) {
    if (!source) return NULL;

    cm_highlight_result_t* result = (cm_highlight_result_t*)cm_alloc(sizeof(cm_highlight_result_t), "highlight_result");
    if (!result) return NULL;
    memset(result, 0, sizeof(*result));

    hl_lexer_t lexer;
    hl_lexer_init(&lexer, source);

    size_t capacity = 64;
    result->tokens = (cm_highlight_token_t*)cm_alloc(capacity * sizeof(cm_highlight_token_t), "highlight_tokens");
    if (!result->tokens) {
        cm_free(result);
        return NULL;
    }

    cm_token_t prev_token;
    int has_prev = 0;
    memset(&prev_token, 0, sizeof(prev_token));

    for (;;) {
        cm_token_t tok = hl_lexer_next_token(&lexer);

        /* Check for capacity */
        if (result->count >= capacity) {
            capacity *= 2;
            cm_highlight_token_t* new_tokens = (cm_highlight_token_t*)cm_alloc(capacity * sizeof(cm_highlight_token_t), "highlight_tokens");
            if (!new_tokens) {
                hl_token_free(&tok);
                cm_highlight_free(result);
                return NULL;
            }
            memcpy(new_tokens, result->tokens, result->count * sizeof(cm_highlight_token_t));
            cm_free(result->tokens);
            result->tokens = new_tokens;
        }

        /* Classify the token */
        cm_color_t color = cm_token_to_color(tok.kind);

        /* Post-process: check for method calls (DOT followed by identifier)
         * If we have: identifier DOT identifier, the identifier after DOT is a method */
        if (has_prev && prev_token.kind == CM_TOK_DOT && tok.kind == CM_TOK_IDENTIFIER) {
            /* Just change color to method - don't modify token data */
            color = CM_COLOR_METHOD;
        }

        /* Store token */
        result->tokens[result->count].token = tok;
        result->tokens[result->count].color = color;
        result->count++;

        if (tok.kind == CM_TOK_EOF) {
            hl_token_free(&tok);
            break;
        }

        /* Move prev_token forward (shallow copy is fine, we don't free) */
        if (has_prev) {
            /* Don't free prev_token - it's stored in result array */
        }
        prev_token = tok;
        has_prev = 1;
    }

    /* Don't free prev_token here - it's owned by result array */

    result->has_error = lexer.in_error;
    result->error_line = lexer.error_line;
    result->error_column = lexer.error_column;

    return result;
}

void cm_highlight_free(cm_highlight_result_t* result) {
    if (!result) return;
    if (result->tokens) {
        for (size_t i = 0; i < result->count; i++) {
            hl_token_free(&result->tokens[i].token);
        }
        cm_free(result->tokens);
    }
    if (result->error_message) {
        cm_free(result->error_message);
    }
    cm_free(result);
}

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

int cm_highlight_file(const char* path, cm_string_t** output) {
    if (!path || !output) return -1;

    char* source = cm_read_file_all(path);
    if (!source) return -1;

    int rc = cm_highlight_source(source, output);
    free(source);
    return rc;
}

int cm_highlight_source(const char* source, cm_string_t** output) {
    if (!source || !output) return -1;

    cm_highlight_result_t* result = cm_tokenize_for_highlight(source);
    if (!result) return -1;

    cm_string_t* out = cm_string_new("");
    if (!out) {
        cm_highlight_free(result);
        return -1;
    }

    int use_color = 1;
    size_t current_line = 1;

    for (size_t i = 0; i < result->count; i++) {
        cm_highlight_token_t* ht = &result->tokens[i];
        cm_token_t* tok = &ht->token;

        /* Add newlines to match original line structure */
        while (current_line < tok->line) {
            cm_string_append(out, "\n");
            current_line++;
        }

        if (tok->kind == CM_TOK_EOF) break;

        /* Add color code */
        cm_string_append(out, cm_color_ansi(ht->color, use_color));

        /* Add token text */
        if (tok->lexeme && tok->lexeme->data) {
            cm_string_append(out, tok->lexeme->data);
        }

        /* Reset color */
        cm_string_append(out, cm_color_reset(use_color));

        /* Add space after certain tokens for readability */
        if (i + 1 < result->count) {
            cm_token_kind_t next_kind = result->tokens[i + 1].token.kind;
            if (next_kind != CM_TOK_EOF && next_kind != CM_TOK_RPAREN &&
                next_kind != CM_TOK_RBRACE && next_kind != CM_TOK_SEMI &&
                next_kind != CM_TOK_COMMA && tok->kind != CM_TOK_LPAREN &&
                tok->kind != CM_TOK_DOT) {
                cm_string_append(out, " ");
            }
        }
    }

    *output = out;
    cm_highlight_free(result);
    return 0;
}

void cm_highlight_print_error(const char* source, size_t line, size_t column, const char* message) {
    if (!source) return;

    fprintf(stderr, "\n%sError:%s %s\n", ANSI_RED, ANSI_RESET, message ? message : "unknown error");

    /* Find the line in source */
    size_t current_line = 1;
    const char* line_start = source;
    const char* p = source;

    while (*p && current_line < line) {
        if (*p == '\n') {
            current_line++;
            line_start = p + 1;
        }
        p++;
    }

    /* Print the line */
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') {
        line_end++;
    }

    fprintf(stderr, "%.*s\n", (int)(line_end - line_start), line_start);

    /* Print pointer */
    for (size_t i = 1; i < column; i++) {
        fprintf(stderr, " ");
    }
    fprintf(stderr, "%s^%s\n", ANSI_RED, ANSI_RESET);
}
