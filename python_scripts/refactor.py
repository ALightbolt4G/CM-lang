import os
import re

c_src = open("src/cm_lang.c", "r").read()
h_src = open("include/cm/cm_lang.h", "r").read()

os.makedirs("src/compiler/lexer", exist_ok=True)
os.makedirs("src/compiler/parser", exist_ok=True)
os.makedirs("src/compiler/ast", exist_ok=True)
os.makedirs("src/compiler/tokens", exist_ok=True)
os.makedirs("include/cm/compiler", exist_ok=True)

# Split h_src
# It has three main parts: tokens, core declarations.
tok_start = h_src.find("typedef enum {")
tok_end = h_src.find("} cm_token_kind_t;") + len("} cm_token_kind_t;")
tok_t_end = h_src.find("} cm_token_t;") + len("} cm_token_t;")
tokens_h = """#ifndef CM_TOKENS_H
#define CM_TOKENS_H

#include "cm/core.h"
#include "cm/string.h"

""" + h_src[tok_start:tok_t_end] + """

#endif
"""
with open("include/cm/compiler/tokens.h", "w") as f: f.write(tokens_h)


lexer_h = """#ifndef CM_LEXER_H
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

#endif
"""
with open("include/cm/compiler/lexer.h", "w") as f: f.write(lexer_h)


ast_h = """#ifndef CM_AST_H
#define CM_AST_H
#include "cm/compiler/tokens.h"

typedef enum {
    CM_AST_REQUIRE,
    CM_AST_VAR_DECL,
    CM_AST_EXPR_STMT,
    CM_AST_POLYGLOT
} cm_ast_kind_t;

typedef enum { CM_POLY_C, CM_POLY_CPP } cm_poly_kind_t;

typedef struct cm_ast_node cm_ast_node_t;
struct cm_ast_node {
    cm_ast_kind_t kind;
    size_t line;
    size_t column;
    union {
        struct { cm_string_t* path; } require_stmt;
        struct { cm_string_t* type_name; cm_string_t* var_name; cm_string_t* init_expr; } var_decl;
        struct { cm_string_t* expr_text; } expr_stmt;
        struct { cm_poly_kind_t lang; cm_string_t* code; } poly_block;
    } as;
    cm_ast_node_t* next;
};

typedef struct {
    cm_ast_node_t* head;
    cm_ast_node_t* tail;
} cm_ast_list_t;

#endif
"""
with open("include/cm/compiler/ast.h", "w") as f: f.write(ast_h)

parser_h = """#ifndef CM_PARSER_H
#define CM_PARSER_H
#include "cm/compiler/lexer.h"
#include "cm/compiler/ast.h"

typedef struct {
    cm_lexer_t lexer;
    cm_token_t current;
} cm_parser_t;

int cm_compile_file(const char* entry_path, const char* output_exe);
int cm_emit_c_file(const char* entry_path, const char* output_c_path);

#endif
"""
with open("include/cm/compiler/parser.h", "w") as f: f.write(parser_h)

# Make cm_lang.h purely an include aggregator
cm_lang_h = """#ifndef CM_LANG_H
#define CM_LANG_H
#include "cm/compiler/tokens.h"
#include "cm/compiler/lexer.h"
#include "cm/compiler/ast.h"
#include "cm/compiler/parser.h"
#endif
"""
with open("include/cm/cm_lang.h", "w") as f: f.write(cm_lang_h)

# Split cm_lang.c
# We will use regex to find sections.
lexer_start = c_src.find(" * Lexer")
ast_start = c_src.find(" * AST")
parser_start = c_src.find(" * Parser")

# Back up to the start of the /* == block
if lexer_start != -1: lexer_start = c_src.rfind("/* =", 0, lexer_start)
if ast_start != -1: ast_start = c_src.rfind("/* =", 0, ast_start)
if parser_start != -1: parser_start = c_src.rfind("/* =", 0, parser_start)

lexer_code = '#include "cm/cm_lang.h"\n#include "cm/memory.h"\n#include "cm/error.h"\n#include <string.h>\n#include <ctype.h>\n\n' + (c_src[lexer_start:ast_start] if lexer_start != -1 else "")
ast_code = '#include "cm/cm_lang.h"\n#include "cm/memory.h"\n#include <string.h>\n\n' + (c_src[ast_start:parser_start] if ast_start != -1 else "")
# remove Ast type definitions from ast_code since they are in ast.h now
ast_code = re.sub(r'typedef enum \{[^}]+\} cm_ast_kind_t;', '', ast_code)
ast_code = re.sub(r'typedef enum \{[^}]+\} cm_poly_kind_t;', '', ast_code)
ast_code = re.sub(r'typedef struct cm_ast_node cm_ast_node_t;', '', ast_code)
ast_code = re.sub(r'struct cm_ast_node \{[^}]+\};', '', ast_code)
ast_code = re.sub(r'struct cm_ast_list \{[^}]+\};', '', ast_code)

parser_code = '#include "cm/cm_lang.h"\n#include "cm/memory.h"\n#include "cm/error.h"\n#include "cm_codegen.h"\n#include <string.h>\n#include <stdio.h>\n#include <ctype.h>\n#include <stdlib.h>\n\n' + c_src[parser_start:]
# remove parser struct definition from parser_code since it's in parser.h now
parser_code = re.sub(r'typedef struct \{[^}]+\} cm_parser_t;', '', parser_code)

with open("src/compiler/lexer/lexer.c", "w") as f: f.write(lexer_code.replace("/* ============================================================================\n * AST\n * ==========================================================================*/", ""))
with open("src/compiler/ast/ast.c", "w") as f: f.write(ast_code)
with open("src/compiler/parser/parser.c", "w") as f: f.write(parser_code)
with open("src/compiler/tokens/tokens.c", "w") as f: 
    f.write('#include "cm/compiler/tokens.h"\n// Tokens are mostly enums.\nvoid cm_token_free(cm_token_t* t) {\n    if (t && t->lexeme) {\n        cm_string_free(t->lexeme);\n        t->lexeme = NULL;\n    }\n}\n')

# update CMakeLists.txt
cmakelists = open("CMakeLists.txt", "r").read()
cmakelists = cmakelists.replace("src/cm_lang.c", "src/compiler/lexer/lexer.c\n    src/compiler/ast/ast.c\n    src/compiler/parser/parser.c\n    src/compiler/tokens/tokens.c")
with open("CMakeLists.txt", "w") as f: f.write(cmakelists)

print("Split completed successfully.")
