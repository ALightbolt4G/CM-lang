#include "cm/compiler/ast_v2.h"
#include "cm/memory.h"
#include "cm/error.h"
#include "cm/string.h"
#include "cm/map.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * CM v2 Type Checker
 * 
 * - Type inference for := operator
 * - Immutability enforcement (let vs mut)
 * - Mandatory error checking for ?T and Result<T,E>
 * - Function type checking
 * ==========================================================================*/

typedef struct cm_type_info {
    cm_ast_v2_node_t* type_node;    /* AST type node */
    int is_mutable;                  /* 1 if mutable, 0 if immutable */
    int is_initialized;              /* 1 if variable has been assigned */
    int is_error_type;               /* 1 if ?T or Result<T,E> */
    struct cm_type_info* next;
} cm_type_info_t;

typedef struct cm_scope {
    cm_map_t* bindings;             /* Variable name -> type_info */
    struct cm_scope* parent;         /* Enclosing scope */
} cm_scope_t;

typedef struct {
    cm_scope_t* current_scope;
    cm_ast_v2_node_t* current_function; /* For return type checking */
    int error_count;
    int warning_count;
} cm_typecheck_ctx_t;

/* Forward declarations */
static int cm_typecheck_expr(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* expr, cm_type_info_t** out_type);
static int cm_typecheck_stmt(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* stmt);
static int cm_typecheck_type(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* type);

/* Create new type info */
static cm_type_info_t* cm_type_info_new(cm_ast_v2_node_t* type_node, int is_mutable) {
    cm_type_info_t* info = (cm_type_info_t*)cm_alloc(sizeof(cm_type_info_t), "type_info");
    if (!info) return NULL;
    
    memset(info, 0, sizeof(*info));
    info->type_node = type_node;
    info->is_mutable = is_mutable;
    info->is_initialized = 1;
    
    /* Check if this is an error type */
    if (type_node) {
        if (type_node->kind == CM_AST_V2_TYPE_OPTION ||
            type_node->kind == CM_AST_V2_TYPE_RESULT) {
            info->is_error_type = 1;
        }
    }
    
    return info;
}

/* Copy type info */
static cm_type_info_t* cm_type_info_copy(cm_type_info_t* src) {
    if (!src) return NULL;
    cm_type_info_t* copy = cm_type_info_new(src->type_node, src->is_mutable);
    if (copy) {
        copy->is_initialized = src->is_initialized;
        copy->is_error_type = src->is_error_type;
    }
    return copy;
}

/* Free type info */
static void cm_type_info_free(cm_type_info_t* info) {
    cm_free(info);
}

/* Create new scope */
static cm_scope_t* cm_scope_new(cm_scope_t* parent) {
    cm_scope_t* scope = (cm_scope_t*)cm_alloc(sizeof(cm_scope_t), "scope");
    if (!scope) return NULL;
    
    memset(scope, 0, sizeof(*scope));
    scope->bindings = cm_map_new("scope_bindings");
    scope->parent = parent;
    
    return scope;
}

/* Free scope */
static void cm_scope_free(cm_scope_t* scope) {
    if (!scope) return;
    
    /* Free all type info in bindings */
    if (scope->bindings) {
        cm_map_free(scope->bindings);
    }
    
    cm_free(scope);
}

/* Look up variable in scope chain */
static cm_type_info_t* cm_scope_lookup(cm_scope_t* scope, const char* name) {
    while (scope) {
        cm_type_info_t* info = (cm_type_info_t*)cm_map_get(scope->bindings, name);
        if (info) return info;
        scope = scope->parent;
    }
    return NULL;
}

/* Add variable to current scope */
static int cm_scope_define(cm_scope_t* scope, const char* name, cm_type_info_t* info) {
    if (!scope || !name || !info) return 0;
    
    /* Check for redefinition in current scope */
    if (cm_map_get(scope->bindings, name)) {
        cm_error_set(CM_ERROR_TYPE, "Variable '%s' already defined in this scope", name);
        return 0;
    }
    
    cm_map_set(scope->bindings, name, info);
    return 1;
}

/* Push new scope */
static void cm_typecheck_push_scope(cm_typecheck_ctx_t* ctx) {
    ctx->current_scope = cm_scope_new(ctx->current_scope);
}

/* Pop scope */
static void cm_typecheck_pop_scope(cm_typecheck_ctx_t* ctx) {
    if (ctx->current_scope) {
        cm_scope_t* parent = ctx->current_scope->parent;
        cm_scope_free(ctx->current_scope);
        ctx->current_scope = parent;
    }
}

/* Type comparison - basic name matching for now */
static int cm_types_equal(cm_ast_v2_node_t* a, cm_ast_v2_node_t* b) {
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    
    switch (a->kind) {
        case CM_AST_V2_TYPE_NAMED:
            return strcmp(a->as.type_named.name->data, b->as.type_named.name->data) == 0;
        
        case CM_AST_V2_TYPE_PTR:
        case CM_AST_V2_TYPE_OPTION:
            return cm_types_equal(a->as.type_ptr.base, b->as.type_ptr.base);
        
        case CM_AST_V2_TYPE_RESULT:
            return cm_types_equal(a->as.type_result.ok_type, b->as.type_result.ok_type) &&
                   cm_types_equal(a->as.type_result.err_type, b->as.type_result.err_type);
        
        default:
            return 1; /* Assume equal for complex types */
    }
}

/* Get type name for error messages */
static const char* cm_type_name(cm_ast_v2_node_t* type) {
    if (!type) return "<unknown>";
    
    switch (type->kind) {
        case CM_AST_V2_TYPE_NAMED:
            return type->as.type_named.name->data;
        case CM_AST_V2_TYPE_PTR:
            return "pointer";
        case CM_AST_V2_TYPE_OPTION:
            return "option";
        case CM_AST_V2_TYPE_RESULT:
            return "result";
        case CM_AST_V2_TYPE_ARRAY:
            return "array";
        case CM_AST_V2_TYPE_SLICE:
            return "slice";
        case CM_AST_V2_TYPE_MAP:
            return "map";
        case CM_AST_V2_TYPE_FN:
            return "function";
        default:
            return "<unknown>";
    }
}

/* Infer type from expression */
static cm_ast_v2_node_t* cm_infer_type(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* expr) {
    if (!expr) return NULL;
    
    switch (expr->kind) {
        case CM_AST_V2_NUMBER:
            /* Check if float */
            if (expr->as.number_literal.is_float) {
                cm_ast_v2_node_t* t = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, expr->line, expr->column);
                t->as.type_named.name = cm_string_new("float");
                return t;
            } else {
                cm_ast_v2_node_t* t = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, expr->line, expr->column);
                t->as.type_named.name = cm_string_new("int");
                return t;
            }
        
        case CM_AST_V2_STRING_LITERAL:
        case CM_AST_V2_INTERPOLATED_STRING: {
            cm_ast_v2_node_t* t = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, expr->line, expr->column);
            t->as.type_named.name = cm_string_new("string");
            return t;
        }
        
        case CM_AST_V2_BOOL: {
            cm_ast_v2_node_t* t = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, expr->line, expr->column);
            t->as.type_named.name = cm_string_new("bool");
            return t;
        }
        
        case CM_AST_V2_IDENTIFIER: {
            cm_type_info_t* info = cm_scope_lookup(ctx->current_scope, 
                expr->as.identifier.value->data);
            if (info) {
                return info->type_node;
            }
            return NULL;
        }
        
        case CM_AST_V2_BINARY_OP: {
            /* Infer from operands */
            cm_ast_v2_node_t* left_type = cm_infer_type(ctx, expr->as.binary_expr.left);
            if (left_type) return left_type;
            return cm_infer_type(ctx, expr->as.binary_expr.right);
        }
        
        case CM_AST_V2_CALL: {
            /* Return the return type of the function */
            if (expr->as.call_expr.callee && 
                expr->as.call_expr.callee->kind == CM_AST_V2_IDENTIFIER) {
                cm_type_info_t* info = cm_scope_lookup(ctx->current_scope,
                    expr->as.call_expr.callee->as.identifier.value->data);
                if (info && info->type_node && info->type_node->kind == CM_AST_V2_TYPE_FN) {
                    return info->type_node->as.type_fn.return_type;
                }
            }
            return NULL;
        }
        
        case CM_AST_V2_OPTION_SOME: {
            cm_ast_v2_node_t* base = cm_infer_type(ctx, expr->as.option_some.value);
            if (base) {
                cm_ast_v2_node_t* opt = cm_ast_v2_new(CM_AST_V2_TYPE_OPTION, expr->line, expr->column);
                opt->as.type_option.base = base;
                return opt;
            }
            return NULL;
        }
        
        case CM_AST_V2_OPTION_NONE: {
            /* Cannot infer the inner type from None alone */
            cm_ast_v2_node_t* opt = cm_ast_v2_new(CM_AST_V2_TYPE_OPTION, expr->line, expr->column);
            opt->as.type_option.base = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, expr->line, expr->column);
            opt->as.type_option.base->as.type_named.name = cm_string_new("void");
            return opt;
        }
        
        default:
            return NULL;
    }
}

/* Check expression and return its type */
static int cm_typecheck_expr(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* expr, cm_type_info_t** out_type) {
    if (!expr) {
        if (out_type) *out_type = NULL;
        return 1;
    }
    
    switch (expr->kind) {
        case CM_AST_V2_NUMBER:
        case CM_AST_V2_STRING_LITERAL:
        case CM_AST_V2_BOOL:
            if (out_type) *out_type = cm_type_info_new(cm_infer_type(ctx, expr), 0);
            return 1;
        
        case CM_AST_V2_IDENTIFIER: {
            const char* name = expr->as.identifier.value->data;
            cm_type_info_t* info = cm_scope_lookup(ctx->current_scope, name);
            if (!info) {
                cm_error_set(CM_ERROR_TYPE, "Undefined variable: %s", name);
                ctx->error_count++;
                return 0;
            }
            if (out_type) *out_type = cm_type_info_copy(info);
            return 1;
        }
        
        case CM_AST_V2_BINARY_OP: {
            cm_type_info_t* left = NULL;
            cm_type_info_t* right = NULL;
            int ok = cm_typecheck_expr(ctx, expr->as.binary_expr.left, &left) &&
                     cm_typecheck_expr(ctx, expr->as.binary_expr.right, &right);
            
            if (ok && left && right && !cm_types_equal(left->type_node, right->type_node)) {
                /* Allow some implicit conversions? For now, strict */
                if (strcmp(expr->as.binary_expr.op->data, "==") != 0 &&
                    strcmp(expr->as.binary_expr.op->data, "!=") != 0) {
                    cm_error_set(CM_ERROR_TYPE, "Type mismatch in binary expression: %s vs %s",
                        cm_type_name(left->type_node), cm_type_name(right->type_node));
                    ctx->error_count++;
                    ok = 0;
                }
            }
            
            if (out_type) *out_type = left ? cm_type_info_copy(left) : NULL;
            if (right) cm_type_info_free(right);
            return ok;
        }
        
        case CM_AST_V2_UNARY_OP: {
            cm_type_info_t* inner = NULL;
            int ok = cm_typecheck_expr(ctx, expr->as.unary_expr.expr, &inner);
            
            if (ok && inner) {
                /* Check ! operator on error types - this handles the error propagation */
                if (strcmp(expr->as.unary_expr.op->data, "!") == 0) {
                    if (!inner->is_error_type) {
                        cm_error_set(CM_ERROR_TYPE, 
                            "Error propagation operator (!) used on non-error type");
                        ctx->warning_count++;
                    }
                    /* The ! operator unwraps the error type */
                    if (inner->type_node && inner->type_node->kind == CM_AST_V2_TYPE_OPTION) {
                        if (out_type) {
                            *out_type = cm_type_info_new(inner->type_node->as.type_option.base, inner->is_mutable);
                            (*out_type)->is_error_type = 0;
                        }
                    } else {
                        if (out_type) *out_type = inner;
                        inner = NULL;
                    }
                } else if (strcmp(expr->as.unary_expr.op->data, "^") == 0) {
                    /* Address-of creates a pointer type */
                    cm_ast_v2_node_t* ptr_type = cm_ast_v2_new(CM_AST_V2_TYPE_PTR, 
                        expr->line, expr->column);
                    ptr_type->as.type_ptr.base = inner ? inner->type_node : NULL;
                    if (out_type) *out_type = cm_type_info_new(ptr_type, 0);
                } else {
                    if (out_type) *out_type = inner;
                    inner = NULL;
                }
            }
            
            if (inner) cm_type_info_free(inner);
            return ok;
        }
        
        case CM_AST_V2_CALL: {
            /* Check callee and arguments */
            cm_type_info_t* callee_type = NULL;
            int ok = cm_typecheck_expr(ctx, expr->as.call_expr.callee, &callee_type);
            
            /* Check arguments */
            cm_ast_v2_node_t* arg = expr->as.call_expr.args;
            while (arg && ok) {
                cm_type_info_t* arg_type = NULL;
                ok = cm_typecheck_expr(ctx, arg, &arg_type);
                if (arg_type) cm_type_info_free(arg_type);
                arg = arg->next;
            }
            
            /* Get return type */
            if (ok && callee_type && callee_type->type_node &&
                callee_type->type_node->kind == CM_AST_V2_TYPE_FN) {
                if (out_type) {
                    *out_type = cm_type_info_new(
                        callee_type->type_node->as.type_fn.return_type, 0);
                }
            } else {
                if (out_type) *out_type = callee_type ? cm_type_info_copy(callee_type) : NULL;
                if (callee_type) cm_type_info_free(callee_type);
            }
            
            return ok;
        }
        
        case CM_AST_V2_FIELD_ACCESS: {
            cm_type_info_t* obj_type = NULL;
            int ok = cm_typecheck_expr(ctx, expr->as.field_access.object, &obj_type);
            /* For now, we can't check field access without struct definitions */
            if (out_type) *out_type = obj_type;
            else if (obj_type) cm_type_info_free(obj_type);
            return ok;
        }
        
        case CM_AST_V2_INDEX: {
            cm_type_info_t* arr_type = NULL;
            cm_type_info_t* idx_type = NULL;
            int ok = cm_typecheck_expr(ctx, expr->as.index_expr.array, &arr_type) &&
                     cm_typecheck_expr(ctx, expr->as.index_expr.index, &idx_type);
            
            /* Index should be int */
            if (ok && idx_type && idx_type->type_node &&
                idx_type->type_node->kind == CM_AST_V2_TYPE_NAMED &&
                strcmp(idx_type->type_node->as.type_named.name->data, "int") != 0) {
                cm_error_set(CM_ERROR_TYPE, "Array index must be int");
                ctx->error_count++;
                ok = 0;
            }
            
            /* Return element type for array/slice types */
            if (ok && arr_type && arr_type->type_node) {
                if (arr_type->type_node->kind == CM_AST_V2_TYPE_ARRAY ||
                    arr_type->type_node->kind == CM_AST_V2_TYPE_SLICE) {
                    if (out_type) {
                        *out_type = cm_type_info_new(arr_type->type_node->as.type_array.element_type, 
                            arr_type->is_mutable);
                    }
                } else {
                    if (out_type) *out_type = cm_type_info_copy(arr_type);
                }
            }
            
            if (arr_type) cm_type_info_free(arr_type);
            if (idx_type) cm_type_info_free(idx_type);
            return ok;
        }
        
        case CM_AST_V2_DEREF: {
            cm_type_info_t* ptr_type = NULL;
            int ok = cm_typecheck_expr(ctx, expr->as.deref_expr.expr, &ptr_type);
            
            if (ok && ptr_type && ptr_type->type_node &&
                ptr_type->type_node->kind == CM_AST_V2_TYPE_PTR) {
                if (out_type) {
                    *out_type = cm_type_info_new(ptr_type->type_node->as.type_ptr.base,
                        ptr_type->is_mutable);
                }
            } else {
                cm_error_set(CM_ERROR_TYPE, "Cannot dereference non-pointer type");
                ctx->error_count++;
                ok = 0;
            }
            
            if (ptr_type) cm_type_info_free(ptr_type);
            return ok;
        }
        
        case CM_AST_V2_OPTION_SOME:
        case CM_AST_V2_RESULT_OK:
        case CM_AST_V2_RESULT_ERR: {
            cm_type_info_t* inner = NULL;
            int ok = cm_typecheck_expr(ctx, 
                expr->kind == CM_AST_V2_OPTION_SOME ? expr->as.option_some.value :
                expr->kind == CM_AST_V2_RESULT_OK ? expr->as.result_ok.value :
                expr->as.result_err.value, &inner);
            
            if (out_type) {
                cm_ast_v2_node_t* wrapper = cm_ast_v2_new(
                    expr->kind == CM_AST_V2_OPTION_SOME ? CM_AST_V2_TYPE_OPTION : CM_AST_V2_TYPE_RESULT,
                    expr->line, expr->column);
                if (expr->kind == CM_AST_V2_OPTION_SOME) {
                    wrapper->as.type_option.base = inner ? inner->type_node : NULL;
                } else {
                    wrapper->as.type_result.ok_type = inner ? inner->type_node : NULL;
                    wrapper->as.type_result.err_type = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED, 
                        expr->line, expr->column);
                    wrapper->as.type_result.err_type->as.type_named.name = cm_string_new("Error");
                }
                *out_type = cm_type_info_new(wrapper, 0);
                (*out_type)->is_error_type = 1;
            }
            
            if (inner) cm_type_info_free(inner);
            return ok;
        }
        
        case CM_AST_V2_OPTION_NONE: {
            if (out_type) {
                cm_ast_v2_node_t* opt = cm_ast_v2_new(CM_AST_V2_TYPE_OPTION, 
                    expr->line, expr->column);
                opt->as.type_option.base = cm_ast_v2_new(CM_AST_V2_TYPE_NAMED,
                    expr->line, expr->column);
                opt->as.type_option.base->as.type_named.name = cm_string_new("void");
                *out_type = cm_type_info_new(opt, 0);
                (*out_type)->is_error_type = 1;
            }
            return 1;
        }
        
        default:
            if (out_type) *out_type = NULL;
            return 1;
    }
}

/* Check type node is valid */
static int cm_typecheck_type(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* type) {
    if (!type) return 1;
    
    switch (type->kind) {
        case CM_AST_V2_TYPE_PTR:
            return cm_typecheck_type(ctx, type->as.type_ptr.base);
        case CM_AST_V2_TYPE_OPTION:
            return cm_typecheck_type(ctx, type->as.type_option.base);
        case CM_AST_V2_TYPE_RESULT:
            return cm_typecheck_type(ctx, type->as.type_result.ok_type) &&
                   cm_typecheck_type(ctx, type->as.type_result.err_type);
        case CM_AST_V2_TYPE_ARRAY:
        case CM_AST_V2_TYPE_SLICE:
            return cm_typecheck_type(ctx, type->as.type_array.element_type);
        case CM_AST_V2_TYPE_MAP:
            return cm_typecheck_type(ctx, type->as.type_map.key_type) &&
                   cm_typecheck_type(ctx, type->as.type_map.value_type);
        case CM_AST_V2_TYPE_FN: {
            cm_ast_v2_node_t* param = type->as.type_fn.params;
            while (param) {
                if (param->kind == CM_AST_V2_PARAM) {
                    if (!cm_typecheck_type(ctx, param->as.param.type)) return 0;
                }
                param = param->next;
            }
            return cm_typecheck_type(ctx, type->as.type_fn.return_type);
        }
        default:
            return 1;
    }
}

/* Check statement */
static int cm_typecheck_stmt(cm_typecheck_ctx_t* ctx, cm_ast_v2_node_t* stmt) {
    if (!stmt) return 1;
    
    switch (stmt->kind) {
        case CM_AST_V2_FN: {
            /* Add function to scope */
            cm_ast_v2_node_t* fn_type = cm_ast_v2_new(CM_AST_V2_TYPE_FN, stmt->line, stmt->column);
            fn_type->as.type_fn.params = stmt->as.fn_decl.params;
            fn_type->as.type_fn.return_type = stmt->as.fn_decl.return_type;
            
            cm_type_info_t* fn_info = cm_type_info_new(fn_type, 0);
            cm_scope_define(ctx->current_scope, stmt->as.fn_decl.name->data, fn_info);
            
            /* Check return type */
            cm_typecheck_type(ctx, stmt->as.fn_decl.return_type);
            
            /* Push function scope */
            cm_typecheck_push_scope(ctx);
            
            /* Add parameters to scope */
            cm_ast_v2_node_t* param = stmt->as.fn_decl.params;
            while (param) {
                if (param->kind == CM_AST_V2_PARAM) {
                    cm_typecheck_type(ctx, param->as.param.type);
                    cm_type_info_t* param_info = cm_type_info_new(param->as.param.type,
                        param->as.param.is_mutable);
                    cm_scope_define(ctx->current_scope, param->as.param.name->data, param_info);
                }
                param = param->next;
            }
            
            /* Set current function for return checking */
            cm_ast_v2_node_t* prev_fn = ctx->current_function;
            ctx->current_function = stmt;
            
            /* Check body */
            cm_ast_v2_node_t* body_stmt = stmt->as.fn_decl.body;
            while (body_stmt) {
                cm_typecheck_stmt(ctx, body_stmt);
                body_stmt = body_stmt->next;
            }
            
            ctx->current_function = prev_fn;
            cm_typecheck_pop_scope(ctx);
            return 1;
        }
        
        case CM_AST_V2_LET:
        case CM_AST_V2_MUT: {
            int is_mutable = (stmt->kind == CM_AST_V2_MUT);
            const char* name = stmt->kind == CM_AST_V2_LET ?
                stmt->as.let_decl.name->data : stmt->as.mut_decl.name->data;
            cm_ast_v2_node_t* declared_type = stmt->kind == CM_AST_V2_LET ?
                stmt->as.let_decl.type : stmt->as.mut_decl.type;
            cm_ast_v2_node_t* init = stmt->kind == CM_AST_V2_LET ?
                stmt->as.let_decl.init : stmt->as.mut_decl.init;
            
            /* Check init expression */
            cm_type_info_t* init_type = NULL;
            int ok = cm_typecheck_expr(ctx, init, &init_type);
            
            /* Infer or check type */
            cm_ast_v2_node_t* final_type = declared_type;
            if (!declared_type && init_type) {
                /* Type inference from init */
                final_type = init_type->type_node;
            } else if (declared_type && init_type && init_type->type_node) {
                /* Check type compatibility */
                if (!cm_types_equal(declared_type, init_type->type_node)) {
                    cm_error_set(CM_ERROR_TYPE, 
                        "Type mismatch in %s declaration: expected %s, got %s",
                        is_mutable ? "mut" : "let",
                        cm_type_name(declared_type),
                        cm_type_name(init_type->type_node));
                    ctx->error_count++;
                    ok = 0;
                }
            }
            
            /* Add to scope */
            if (ok) {
                cm_type_info_t* var_info = cm_type_info_new(final_type, is_mutable);
                cm_scope_define(ctx->current_scope, name, var_info);
            }
            
            if (init_type) cm_type_info_free(init_type);
            return ok;
        }
        
        case CM_AST_V2_ASSIGN: {
            cm_type_info_t* target_type = NULL;
            cm_type_info_t* value_type = NULL;
            
            int ok = cm_typecheck_expr(ctx, stmt->as.assign_stmt.target, &target_type) &&
                     cm_typecheck_expr(ctx, stmt->as.assign_stmt.value, &value_type);
            
            /* Check mutability */
            if (ok && target_type && !target_type->is_mutable) {
                cm_error_set(CM_ERROR_TYPE, "Cannot assign to immutable variable");
                ctx->error_count++;
                ok = 0;
            }
            
            /* Check type compatibility */
            if (ok && target_type && value_type && value_type->type_node &&
                !cm_types_equal(target_type->type_node, value_type->type_node)) {
                cm_error_set(CM_ERROR_TYPE, "Type mismatch in assignment: %s vs %s",
                    cm_type_name(target_type->type_node),
                    cm_type_name(value_type->type_node));
                ctx->error_count++;
                ok = 0;
            }
            
            /* Check mandatory error handling */
            if (ok && value_type && value_type->is_error_type) {
                cm_error_set(CM_ERROR_TYPE, 
                    "Error value must be handled with ! or match expression");
                ctx->error_count++;
                ok = 0;
            }
            
            if (target_type) cm_type_info_free(target_type);
            if (value_type) cm_type_info_free(value_type);
            return ok;
        }
        
        case CM_AST_V2_EXPR_STMT: {
            cm_type_info_t* expr_type = NULL;
            int ok = cm_typecheck_expr(ctx, stmt->as.expr_stmt.expr, &expr_type);
            
            /* Check mandatory error handling */
            if (ok && expr_type && expr_type->is_error_type) {
                /* Allow if the expression uses ! operator */
                cm_ast_v2_node_t* expr = stmt->as.expr_stmt.expr;
                if (!(expr->kind == CM_AST_V2_UNARY_OP && 
                      strcmp(expr->as.unary_expr.op->data, "!") == 0)) {
                    cm_error_set(CM_ERROR_TYPE,
                        "Error value must be handled with ! or match expression");
                    ctx->error_count++;
                    ok = 0;
                }
            }
            
            if (expr_type) cm_type_info_free(expr_type);
            return ok;
        }
        
        case CM_AST_V2_RETURN: {
            cm_type_info_t* return_type = NULL;
            int ok = cm_typecheck_expr(ctx, stmt->as.return_stmt.value, &return_type);
            
            /* Check against function return type */
            if (ok && ctx->current_function && return_type) {
                cm_ast_v2_node_t* expected = ctx->current_function->as.fn_decl.return_type;
                if (expected && !cm_types_equal(expected, return_type->type_node)) {
                    cm_error_set(CM_ERROR_TYPE, "Return type mismatch: expected %s, got %s",
                        cm_type_name(expected), cm_type_name(return_type->type_node));
                    ctx->error_count++;
                    ok = 0;
                }
            }
            
            if (return_type) cm_type_info_free(return_type);
            return ok;
        }
        
        case CM_AST_V2_IF: {
            cm_type_info_t* cond_type = NULL;
            int ok = cm_typecheck_expr(ctx, stmt->as.if_stmt.condition, &cond_type);
            
            /* Condition should be bool */
            if (ok && cond_type && cond_type->type_node &&
                cond_type->type_node->kind == CM_AST_V2_TYPE_NAMED &&
                strcmp(cond_type->type_node->as.type_named.name->data, "bool") != 0) {
                cm_error_set(CM_ERROR_TYPE, "If condition must be bool");
                ctx->error_count++;
                ok = 0;
            }
            
            if (cond_type) cm_type_info_free(cond_type);
            
            /* Check branches */
            cm_typecheck_push_scope(ctx);
            cm_ast_v2_node_t* branch_stmt = stmt->as.if_stmt.then_branch;
            while (branch_stmt) {
                cm_typecheck_stmt(ctx, branch_stmt);
                branch_stmt = branch_stmt->next;
            }
            cm_typecheck_pop_scope(ctx);
            
            if (stmt->as.if_stmt.else_branch) {
                cm_typecheck_push_scope(ctx);
                branch_stmt = stmt->as.if_stmt.else_branch;
                while (branch_stmt) {
                    cm_typecheck_stmt(ctx, branch_stmt);
                    branch_stmt = branch_stmt->next;
                }
                cm_typecheck_pop_scope(ctx);
            }
            
            return ok;
        }
        
        case CM_AST_V2_WHILE:
        case CM_AST_V2_FOR: {
            cm_type_info_t* cond_type = NULL;
            int ok = cm_typecheck_expr(ctx, 
                stmt->kind == CM_AST_V2_WHILE ? stmt->as.while_stmt.condition :
                stmt->as.for_stmt.condition, &cond_type);
            
            if (cond_type) cm_type_info_free(cond_type);
            
            cm_typecheck_push_scope(ctx);
            cm_ast_v2_node_t* body_stmt = stmt->kind == CM_AST_V2_WHILE ?
                stmt->as.while_stmt.body : stmt->as.for_stmt.body;
            while (body_stmt) {
                cm_typecheck_stmt(ctx, body_stmt);
                body_stmt = body_stmt->next;
            }
            cm_typecheck_pop_scope(ctx);
            
            return ok;
        }
        
        case CM_AST_V2_MATCH: {
            cm_type_info_t* expr_type = NULL;
            int ok = cm_typecheck_expr(ctx, stmt->as.match_expr.expr, &expr_type);
            if (expr_type) cm_type_info_free(expr_type);
            
            /* Check match arms */
            cm_ast_v2_node_t* arm = stmt->as.match_expr.arms;
            while (arm && ok) {
                if (arm->kind == CM_AST_V2_MATCH_ARM) {
                    cm_typecheck_push_scope(ctx);
                    ok = cm_typecheck_expr(ctx, arm->as.match_arm.expr, NULL);
                    cm_typecheck_pop_scope(ctx);
                }
                arm = arm->next;
            }
            
            return ok;
        }
        
        case CM_AST_V2_STRUCT:
        case CM_AST_V2_UNION:
            /* Check field types */
            return cm_typecheck_type(ctx, stmt->as.struct_decl.fields);
        
        case CM_AST_V2_BREAK:
        case CM_AST_V2_CONTINUE:
            return 1;
        
        default:
            return 1;
    }
}

/* Public API */

cm_typecheck_ctx_t* cm_typecheck_new(void) {
    cm_typecheck_ctx_t* ctx = (cm_typecheck_ctx_t*)cm_alloc(sizeof(cm_typecheck_ctx_t), "typecheck_ctx");
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->current_scope = cm_scope_new(NULL);
    
    return ctx;
}

void cm_typecheck_free(cm_typecheck_ctx_t* ctx) {
    if (!ctx) return;
    
    while (ctx->current_scope) {
        cm_scope_t* parent = ctx->current_scope->parent;
        cm_scope_free(ctx->current_scope);
        ctx->current_scope = parent;
    }
    
    cm_free(ctx);
}

int cm_typecheck_module(cm_typecheck_ctx_t* ctx, cm_ast_v2_list_t* ast) {
    if (!ctx || !ast) return 0;
    
    cm_ast_v2_node_t* stmt = ast->head;
    while (stmt) {
        cm_typecheck_stmt(ctx, stmt);
        stmt = stmt->next;
    }
    
    return ctx->error_count == 0;
}

int cm_typecheck_get_error_count(cm_typecheck_ctx_t* ctx) {
    return ctx ? ctx->error_count : 0;
}

int cm_typecheck_get_warning_count(cm_typecheck_ctx_t* ctx) {
    return ctx ? ctx->warning_count : 0;
}
