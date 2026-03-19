#ifndef CM_PARSER_H
#define CM_PARSER_H
#include "cm/compiler/lexer.h"
#include "cm/compiler/ast.h"
#include "cm/compiler/ast_v2.h"

typedef struct {
    cm_lexer_t lexer;
    cm_token_t current;
} cm_parser_t;

int cm_compile_file(const char* entry_path, const char* output_exe);
int cm_emit_c_file(const char* entry_path, const char* output_c_path);

/* v2 high-level parse interface */
cm_ast_v2_list_t cm_parse_v2(const char* src);

#endif
