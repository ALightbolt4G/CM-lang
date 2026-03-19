#ifndef CM_LEXER_H
#define CM_LEXER_H
#include "cm/compiler/tokens.h"

typedef struct {
    const char* src;
    size_t length;
    size_t pos;
    size_t line;
    size_t column;
    cm_string_t* current_lexeme;
} cm_lexer_t;

void cm_lexer_init(cm_lexer_t* lx, const char* src);
cm_token_t cm_lexer_next_token(cm_lexer_t* lx);

/* v2 lexer functions */
void cm_lexer_v2_init(cm_lexer_t* lx, const char* src);
cm_token_t cm_lexer_v2_next_token(cm_lexer_t* lx);

#endif
