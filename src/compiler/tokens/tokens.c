#include "cm/compiler/tokens.h"
// Tokens are mostly enums.
void cm_token_free(cm_token_t* t) {
    if (t && t->lexeme) {
        cm_string_free(t->lexeme);
        t->lexeme = NULL;
    }
}
