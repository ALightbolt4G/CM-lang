#ifndef CM_CODEGEN_H
#define CM_CODEGEN_H

#include "cm/core.h"
#include "cm/string.h"

/* Forward declarations of frontend AST types (kept internal for now). */
#include "cm/compiler/ast.h"

/* Generate hardened C for a parsed CM program. */
cm_string_t* cm_codegen_to_c(const cm_ast_list_t* ast);

#endif /* CM_CODEGEN_H */

