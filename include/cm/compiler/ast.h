#ifndef CM_AST_H
#define CM_AST_H
#include "cm/compiler/tokens.h"

typedef enum {
    CM_AST_REQUIRE,
    CM_AST_VAR_DECL,
    CM_AST_EXPR_STMT,
    CM_AST_POLYGLOT,
    CM_AST_NAMESPACE,
    CM_AST_CLASS,
    CM_AST_METHOD,
    CM_AST_PROPERTY,
    CM_AST_IMPORT,
    CM_AST_TRY_CATCH,
    CM_AST_THROW,
    CM_AST_ASYNC_AWAIT,
    CM_AST_ATTRIBUTE,
    CM_AST_INTERFACE,
    CM_AST_IMPLEMENTS,
    CM_AST_GENERIC_TYPE
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
        struct { cm_string_t* name; cm_ast_node_t* body; } namespace_decl;
        struct { 
            cm_string_t* name; 
            cm_string_t* parent_class;
            cm_ast_node_t* members;
            int is_public;
        } class_decl;
        struct {
            cm_string_t* name;
            cm_string_t* return_type;
            cm_ast_node_t* params;
            cm_ast_node_t* body;
            int is_public;
            int is_static;
            int is_async;
        } method_decl;
        struct {
            cm_string_t* name;
            cm_string_t* type;
            cm_ast_node_t* getter;
            cm_ast_node_t* setter;
            int is_public;
        } property_decl;
        struct { cm_string_t* module; cm_string_t* symbol; } import_stmt;
        struct {
            cm_ast_node_t* try_body;
            cm_ast_node_t* catch_body;
            cm_string_t* catch_var;
        } try_catch;
        struct { cm_string_t* expr; } throw_stmt;
        struct { cm_ast_node_t* expr; } async_await;
        struct { cm_string_t* name; cm_ast_node_t* args; } attribute_decl;
        struct { cm_string_t* name; cm_ast_node_t* methods; } interface_decl;
        struct { cm_string_t* base; cm_ast_node_t* interfaces; } implements_decl;
        struct { cm_string_t* name; cm_ast_node_t* type_params; } generic_type;
    } as;
    cm_ast_node_t* next;
};

typedef struct {
    cm_ast_node_t* head;
    cm_ast_node_t* tail;
} cm_ast_list_t;

/* Function declarations */
cm_ast_node_t* cm_ast_new(cm_ast_kind_t kind, size_t line, size_t col);
void cm_ast_list_append(cm_ast_list_t* list, cm_ast_node_t* n);
void cm_ast_free_list(cm_ast_list_t* list);

#endif
