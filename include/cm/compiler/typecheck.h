#ifndef CM_TYPECHECK_H
#define CM_TYPECHECK_H

#include "cm/compiler/ast_v2.h"

/* Forward declaration */
typedef struct cm_typecheck_ctx cm_typecheck_ctx_t;

/* Create/destroy type checker context */
cm_typecheck_ctx_t* cm_typecheck_new(void);
void cm_typecheck_free(cm_typecheck_ctx_t* ctx);

/* Type check an entire module */
int cm_typecheck_module(cm_typecheck_ctx_t* ctx, cm_ast_v2_list_t* ast);

/* Get error/warning counts */
int cm_typecheck_get_error_count(cm_typecheck_ctx_t* ctx);
int cm_typecheck_get_warning_count(cm_typecheck_ctx_t* ctx);

#endif
