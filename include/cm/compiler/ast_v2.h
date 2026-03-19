#ifndef CM_AST_V2_H
#define CM_AST_V2_H
#include "cm/compiler/tokens.h"

/* CM v2 AST - Simplified structure for new syntax */

typedef enum {
    /* Declarations */
    CM_AST_V2_FN,           // fn name() -> T { ... }
    CM_AST_V2_LET,          // let name: T = expr
    CM_AST_V2_MUT,          // mut name: T = expr
    CM_AST_V2_STRUCT,       // struct Name { fields }
    CM_AST_V2_UNION,        // union Name { variants }
    CM_AST_V2_IMPL,         // impl Type { methods }
    CM_AST_V2_IMPORT,       // import "module"
    
    /* Statements */
    CM_AST_V2_EXPR_STMT,    // expr;
    CM_AST_V2_ASSIGN,       // name = expr;
    CM_AST_V2_RETURN,       // return expr;
    CM_AST_V2_IF,           // if cond { } else { }
    CM_AST_V2_WHILE,        // while cond { }
    CM_AST_V2_FOR,          // for i in 0..10 { }
    CM_AST_V2_MATCH,        // match expr { arms }
    CM_AST_V2_BREAK,        // break;
    CM_AST_V2_CONTINUE,     // continue;
    
    /* Expressions */
    CM_AST_V2_CALL,         // fn(args)
    CM_AST_V2_FIELD_ACCESS, // obj.field
    CM_AST_V2_INDEX,        // arr[index]  (bounds checked)
    CM_AST_V2_DEREF,        // ptr^
    CM_AST_V2_ADDR_OF,      // ^var
    CM_AST_V2_UNARY_OP,     // -expr, !expr
    CM_AST_V2_BINARY_OP,    // a + b, a && b
    CM_AST_V2_INTERPOLATED_STRING, // "hello {name}"
    CM_AST_V2_STRING_LITERAL, // "hello"
    CM_AST_V2_NUMBER,       // 42, 3.14
    CM_AST_V2_BOOL,         // true, false
    CM_AST_V2_IDENTIFIER,   // variable name
    CM_AST_V2_OPTION_SOME,  // Some(value)
    CM_AST_V2_OPTION_NONE,  // None
    CM_AST_V2_RESULT_OK,    // Ok(value)
    CM_AST_V2_RESULT_ERR,   // Err(error)
    
    /* Types */
    CM_AST_V2_TYPE_NAMED,   // int, string, User
    CM_AST_V2_TYPE_PTR,     // ^T (safe pointer)
    CM_AST_V2_TYPE_OPTION,  // ?T
    CM_AST_V2_TYPE_RESULT,  // Result<T, E>
    CM_AST_V2_TYPE_ARRAY,   // array<T>
    CM_AST_V2_TYPE_SLICE,   // slice<T>
    CM_AST_V2_TYPE_MAP,     // map<K,V>
    CM_AST_V2_TYPE_FN,      // fn(T) -> U
    
    /* Parameters */
    CM_AST_V2_PARAM,        // name: type or mut name: type
    
    /* Match arms */
    CM_AST_V2_MATCH_ARM,    // pattern => expr
    
    /* Attributes */
    CM_AST_V2_ATTRIBUTE,    // @route("/path")
    
    /* Legacy support */
    CM_AST_V2_POLYGLOT,     // c{...} or cpp{...}
} cm_ast_v2_kind_t;

/* Forward declarations */
typedef struct cm_ast_v2_node cm_ast_v2_node_t;
typedef struct cm_ast_v2_list cm_ast_v2_list_t;

/* AST node structure */
struct cm_ast_v2_node {
    cm_ast_v2_kind_t kind;
    size_t line;
    size_t column;
    union {
        /* Function declaration */
        struct {
            cm_string_t* name;
            cm_ast_v2_node_t* params;
            cm_ast_v2_node_t* return_type;
            cm_ast_v2_node_t* body;
            cm_ast_v2_node_t* attributes;
        } fn_decl;
        
        /* Variable declarations */
        struct {
            cm_string_t* name;
            cm_ast_v2_node_t* type;
            cm_ast_v2_node_t* init;
        } let_decl, mut_decl;
        
        /* Struct/Union declaration */
        struct {
            cm_string_t* name;
            cm_ast_v2_node_t* fields;
            cm_ast_v2_node_t* attributes;
        } struct_decl, union_decl;
        
        /* Implementation block */
        struct {
            cm_ast_v2_node_t* target_type;
            cm_ast_v2_node_t* methods;
        } impl_decl;
        
        /* Import statement */
        struct {
            cm_string_t* path;
        } import_decl;
        
        /* Expression statement */
        struct {
            cm_ast_v2_node_t* expr;
        } expr_stmt;
        
        /* Assignment */
        struct {
            cm_ast_v2_node_t* target;
            cm_ast_v2_node_t* value;
        } assign_stmt;
        
        /* Return statement */
        struct {
            cm_ast_v2_node_t* value;
        } return_stmt;
        
        /* If statement */
        struct {
            cm_ast_v2_node_t* condition;
            cm_ast_v2_node_t* then_branch;
            cm_ast_v2_node_t* else_branch;
        } if_stmt;
        
        /* While loop */
        struct {
            cm_ast_v2_node_t* condition;
            cm_ast_v2_node_t* body;
        } while_stmt;
        
        /* For loop: for var_name in iterable { body } */
        struct {
            cm_string_t*      var_name;  /* loop variable name */
            cm_ast_v2_node_t* iterable; /* the collection/range to iterate */
            cm_ast_v2_node_t* body;
        } for_stmt;
        
        /* Match expression */
        struct {
            cm_ast_v2_node_t* expr;
            cm_ast_v2_node_t* arms;
        } match_expr;
        
        /* Function call */
        struct {
            cm_ast_v2_node_t* callee;
            cm_ast_v2_node_t* args;
        } call_expr;
        
        /* Field access and indexing */
        struct {
            cm_ast_v2_node_t* object;
            cm_string_t* field;
        } field_access;
        
        struct {
            cm_ast_v2_node_t* array;
            cm_ast_v2_node_t* index;
        } index_expr;
        
        /* Pointer operations */
        struct {
            cm_ast_v2_node_t* expr;
        } deref_expr, addr_of_expr;
        
        /* Unary and binary operations */
        struct {
            cm_string_t* op;
            cm_ast_v2_node_t* expr;
        } unary_expr;
        
        struct {
            cm_string_t* op;
            cm_ast_v2_node_t* left;
            cm_ast_v2_node_t* right;
        } binary_expr;
        
        /* Literals */
        struct {
            cm_string_t* value;
        } string_literal, identifier;
        
        struct {
            cm_string_t* value;
            int is_float;
        } number_literal;
        
        struct {
            int value;
        } bool_literal;
        
        /* Option/Result constructors */
        struct {
            cm_ast_v2_node_t* value;
        } option_some, result_ok, result_err;
        
        /* Types */
        struct {
            cm_string_t* name;
        } type_named;
        
        struct {
            cm_ast_v2_node_t* base;
        } type_ptr, type_option;
        
        struct {
            cm_ast_v2_node_t* ok_type;
            cm_ast_v2_node_t* err_type;
        } type_result;
        
        struct {
            cm_ast_v2_node_t* element_type;
        } type_array, type_slice;
        
        struct {
            cm_ast_v2_node_t* key_type;
            cm_ast_v2_node_t* value_type;
        } type_map;
        
        struct {
            cm_ast_v2_node_t* params;
            cm_ast_v2_node_t* return_type;
        } type_fn;
        
        /* Parameters */
        struct {
            cm_string_t* name;
            cm_ast_v2_node_t* type;
            int is_mutable;
        } param;
        
        /* Match arms */
        struct {
            cm_ast_v2_node_t* pattern;
            cm_ast_v2_node_t* expr;
        } match_arm;
        
        /* Attributes */
        struct {
            cm_string_t* name;
            cm_ast_v2_node_t* args;
        } attribute;
        
        /* Interpolated strings */
        struct {
            cm_string_t* template;
            cm_ast_v2_node_t* parts;
        } interpolated_string;
        
        /* Polyglot blocks */
        struct {
            cm_string_t* code;
            int is_cpp;
        } polyglot;
    } as;
    cm_ast_v2_node_t* next;
};

/* AST list structure */
struct cm_ast_v2_list {
    cm_ast_v2_node_t* head;
    cm_ast_v2_node_t* tail;
};

/* Function declarations */
cm_ast_v2_node_t* cm_ast_v2_new(cm_ast_v2_kind_t kind, size_t line, size_t col);
void cm_ast_v2_list_append(cm_ast_v2_list_t* list, cm_ast_v2_node_t* n);
void cm_ast_v2_free_list(cm_ast_v2_list_t* list);
void cm_ast_v2_free_node(cm_ast_v2_node_t* node);

/* Utility functions */
const char* cm_ast_v2_kind_to_string(cm_ast_v2_kind_t kind);

#endif
