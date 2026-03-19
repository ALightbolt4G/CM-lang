#include "cm/cm_lang.h"
#include "cm/memory.h"
#include <string.h>

/* ============================================================================
 * AST
 * ========================================================================== */

cm_ast_node_t* cm_ast_new(cm_ast_kind_t kind, size_t line, size_t col) {
    cm_ast_node_t* n = (cm_ast_node_t*)cm_alloc(sizeof(cm_ast_node_t), "cm_ast_node");
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->kind   = kind;
    n->line   = line;
    n->column = col;
    return n;
}

void cm_ast_list_append(cm_ast_list_t* list, cm_ast_node_t* n) {
    if (!list || !n) return;
    if (!list->head) {
        list->head = list->tail = n;
    } else {
        list->tail->next = n;
        list->tail = n;
    }
}

void cm_ast_free_list(cm_ast_list_t* list) {
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
            case CM_AST_CLASS:
            case CM_AST_METHOD:
            case CM_AST_PROPERTY:
            case CM_AST_IMPORT:
            case CM_AST_TRY_CATCH:
            case CM_AST_THROW:
            case CM_AST_ASYNC_AWAIT:
            case CM_AST_ATTRIBUTE:
            case CM_AST_INTERFACE:
            case CM_AST_IMPLEMENTS:
            case CM_AST_GENERIC_TYPE:
                /* New node types - cleanup handled in cm_lang.c */
                break;
        }
        cm_free(cur);
        cur = next;
    }
    list->head = list->tail = NULL;
}

