#include "cm/compiler/ast_v2.h"
#include "cm/memory.h"
#include "cm/error.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * CM v2 AST Implementation
 * ==========================================================================*/

cm_ast_v2_node_t* cm_ast_v2_new(cm_ast_v2_kind_t kind, size_t line, size_t col) {
    cm_ast_v2_node_t* node = (cm_ast_v2_node_t*)cm_alloc(sizeof(cm_ast_v2_node_t), "ast_node");
    if (!node) return NULL;
    
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->line = line;
    node->column = col;
    return node;
}

void cm_ast_v2_list_append(cm_ast_v2_list_t* list, cm_ast_v2_node_t* n) {
    if (!list || !n) return;
    
    if (!list->head) {
        list->head = list->tail = n;
    } else {
        list->tail->next = n;
        list->tail = n;
    }
}

static void cm_ast_v2_free_node_recursive(cm_ast_v2_node_t* node) {
    if (!node) return;
    
    // Free children first
    switch (node->kind) {
        case CM_AST_V2_FN:
            if (node->as.fn_decl.name) cm_string_free(node->as.fn_decl.name);
            cm_ast_v2_free_node_recursive(node->as.fn_decl.params);
            cm_ast_v2_free_node_recursive(node->as.fn_decl.return_type);
            cm_ast_v2_free_node_recursive(node->as.fn_decl.body);
            cm_ast_v2_free_node_recursive(node->as.fn_decl.attributes);
            break;
            
        case CM_AST_V2_LET:
            if (node->as.let_decl.name) cm_string_free(node->as.let_decl.name);
            cm_ast_v2_free_node_recursive(node->as.let_decl.type);
            cm_ast_v2_free_node_recursive(node->as.let_decl.init);
            break;
            
        case CM_AST_V2_MUT:
            if (node->as.mut_decl.name) cm_string_free(node->as.mut_decl.name);
            cm_ast_v2_free_node_recursive(node->as.mut_decl.type);
            cm_ast_v2_free_node_recursive(node->as.mut_decl.init);
            break;
            
        case CM_AST_V2_STRUCT:
        case CM_AST_V2_UNION:
            if (node->as.struct_decl.name) cm_string_free(node->as.struct_decl.name);
            cm_ast_v2_free_node_recursive(node->as.struct_decl.fields);
            cm_ast_v2_free_node_recursive(node->as.struct_decl.attributes);
            break;
            
        case CM_AST_V2_IMPL:
            cm_ast_v2_free_node_recursive(node->as.impl_decl.target_type);
            cm_ast_v2_free_node_recursive(node->as.impl_decl.methods);
            break;
            
        case CM_AST_V2_IMPORT:
            if (node->as.import_decl.path) cm_string_free(node->as.import_decl.path);
            break;
            
        case CM_AST_V2_EXPR_STMT:
            cm_ast_v2_free_node_recursive(node->as.expr_stmt.expr);
            break;
            
        case CM_AST_V2_ASSIGN:
            cm_ast_v2_free_node_recursive(node->as.assign_stmt.target);
            cm_ast_v2_free_node_recursive(node->as.assign_stmt.value);
            break;
            
        case CM_AST_V2_RETURN:
            cm_ast_v2_free_node_recursive(node->as.return_stmt.value);
            break;
            
        case CM_AST_V2_IF:
            cm_ast_v2_free_node_recursive(node->as.if_stmt.condition);
            cm_ast_v2_free_node_recursive(node->as.if_stmt.then_branch);
            cm_ast_v2_free_node_recursive(node->as.if_stmt.else_branch);
            break;
            
        case CM_AST_V2_WHILE:
            cm_ast_v2_free_node_recursive(node->as.while_stmt.condition);
            cm_ast_v2_free_node_recursive(node->as.while_stmt.body);
            break;

        case CM_AST_V2_FOR:
            if (node->as.for_stmt.var_name) cm_string_free(node->as.for_stmt.var_name);
            cm_ast_v2_free_node_recursive(node->as.for_stmt.iterable);
            cm_ast_v2_free_node_recursive(node->as.for_stmt.body);
            break;
            
        case CM_AST_V2_MATCH:
            cm_ast_v2_free_node_recursive(node->as.match_expr.expr);
            cm_ast_v2_free_node_recursive(node->as.match_expr.arms);
            break;
            
        case CM_AST_V2_CALL:
            cm_ast_v2_free_node_recursive(node->as.call_expr.callee);
            cm_ast_v2_free_node_recursive(node->as.call_expr.args);
            break;
            
        case CM_AST_V2_FIELD_ACCESS:
            cm_ast_v2_free_node_recursive(node->as.field_access.object);
            if (node->as.field_access.field) cm_string_free(node->as.field_access.field);
            break;
            
        case CM_AST_V2_INDEX:
            cm_ast_v2_free_node_recursive(node->as.index_expr.array);
            cm_ast_v2_free_node_recursive(node->as.index_expr.index);
            break;
            
        case CM_AST_V2_DEREF:
        case CM_AST_V2_ADDR_OF:
            cm_ast_v2_free_node_recursive(node->as.deref_expr.expr);
            break;
            
        case CM_AST_V2_UNARY_OP:
            if (node->as.unary_expr.op) cm_string_free(node->as.unary_expr.op);
            cm_ast_v2_free_node_recursive(node->as.unary_expr.expr);
            break;
            
        case CM_AST_V2_BINARY_OP:
            if (node->as.binary_expr.op) cm_string_free(node->as.binary_expr.op);
            cm_ast_v2_free_node_recursive(node->as.binary_expr.left);
            cm_ast_v2_free_node_recursive(node->as.binary_expr.right);
            break;
            
        case CM_AST_V2_STRING_LITERAL:
        case CM_AST_V2_IDENTIFIER:
            if (node->as.string_literal.value) cm_string_free(node->as.string_literal.value);
            break;
            
        case CM_AST_V2_NUMBER:
            if (node->as.number_literal.value) cm_string_free(node->as.number_literal.value);
            break;
            
        case CM_AST_V2_OPTION_SOME:
        case CM_AST_V2_RESULT_OK:
        case CM_AST_V2_RESULT_ERR:
            cm_ast_v2_free_node_recursive(node->as.option_some.value);
            break;
            
        case CM_AST_V2_TYPE_NAMED:
            if (node->as.type_named.name) cm_string_free(node->as.type_named.name);
            break;
            
        case CM_AST_V2_TYPE_PTR:
        case CM_AST_V2_TYPE_OPTION:
            cm_ast_v2_free_node_recursive(node->as.type_ptr.base);
            break;
            
        case CM_AST_V2_TYPE_RESULT:
            cm_ast_v2_free_node_recursive(node->as.type_result.ok_type);
            cm_ast_v2_free_node_recursive(node->as.type_result.err_type);
            break;
            
        case CM_AST_V2_TYPE_ARRAY:
        case CM_AST_V2_TYPE_SLICE:
            cm_ast_v2_free_node_recursive(node->as.type_array.element_type);
            break;
            
        case CM_AST_V2_TYPE_MAP:
            cm_ast_v2_free_node_recursive(node->as.type_map.key_type);
            cm_ast_v2_free_node_recursive(node->as.type_map.value_type);
            break;
            
        case CM_AST_V2_TYPE_FN:
            cm_ast_v2_free_node_recursive(node->as.type_fn.params);
            cm_ast_v2_free_node_recursive(node->as.type_fn.return_type);
            break;
            
        case CM_AST_V2_PARAM:
            if (node->as.param.name) cm_string_free(node->as.param.name);
            cm_ast_v2_free_node_recursive(node->as.param.type);
            break;
            
        case CM_AST_V2_MATCH_ARM:
            cm_ast_v2_free_node_recursive(node->as.match_arm.pattern);
            cm_ast_v2_free_node_recursive(node->as.match_arm.expr);
            break;
            
        case CM_AST_V2_ATTRIBUTE:
            if (node->as.attribute.name) cm_string_free(node->as.attribute.name);
            cm_ast_v2_free_node_recursive(node->as.attribute.args);
            break;
            
        case CM_AST_V2_INTERPOLATED_STRING:
            if (node->as.interpolated_string.template) cm_string_free(node->as.interpolated_string.template);
            cm_ast_v2_free_node_recursive(node->as.interpolated_string.parts);
            break;
            
        case CM_AST_V2_POLYGLOT:
            if (node->as.polyglot.code) cm_string_free(node->as.polyglot.code);
            break;
            
        case CM_AST_V2_BOOL:
        case CM_AST_V2_OPTION_NONE:
        case CM_AST_V2_BREAK:
        case CM_AST_V2_CONTINUE:
            // No additional cleanup needed
            break;
    }
    
    // Free the node itself
    cm_free(node);
}

void cm_ast_v2_free_node(cm_ast_v2_node_t* node) {
    cm_ast_v2_free_node_recursive(node);
}

void cm_ast_v2_free_list(cm_ast_v2_list_t* list) {
    if (!list) return;
    
    cm_ast_v2_node_t* current = list->head;
    while (current) {
        cm_ast_v2_node_t* next = current->next;
        cm_ast_v2_free_node_recursive(current);
        current = next;
    }
    
    list->head = list->tail = NULL;
}

const char* cm_ast_v2_kind_to_string(cm_ast_v2_kind_t kind) {
    switch (kind) {
        case CM_AST_V2_FN: return "fn";
        case CM_AST_V2_LET: return "let";
        case CM_AST_V2_MUT: return "mut";
        case CM_AST_V2_STRUCT: return "struct";
        case CM_AST_V2_UNION: return "union";
        case CM_AST_V2_IMPL: return "impl";
        case CM_AST_V2_IMPORT: return "import";
        case CM_AST_V2_EXPR_STMT: return "expr_stmt";
        case CM_AST_V2_ASSIGN: return "assign";
        case CM_AST_V2_RETURN: return "return";
        case CM_AST_V2_IF: return "if";
        case CM_AST_V2_WHILE: return "while";
        case CM_AST_V2_FOR: return "for";
        case CM_AST_V2_MATCH: return "match";
        case CM_AST_V2_BREAK: return "break";
        case CM_AST_V2_CONTINUE: return "continue";
        case CM_AST_V2_CALL: return "call";
        case CM_AST_V2_FIELD_ACCESS: return "field_access";
        case CM_AST_V2_INDEX: return "index";
        case CM_AST_V2_DEREF: return "deref";
        case CM_AST_V2_ADDR_OF: return "addr_of";
        case CM_AST_V2_UNARY_OP: return "unary_op";
        case CM_AST_V2_BINARY_OP: return "binary_op";
        case CM_AST_V2_INTERPOLATED_STRING: return "interpolated_string";
        case CM_AST_V2_STRING_LITERAL: return "string_literal";
        case CM_AST_V2_NUMBER: return "number";
        case CM_AST_V2_BOOL: return "bool";
        case CM_AST_V2_IDENTIFIER: return "identifier";
        case CM_AST_V2_OPTION_SOME: return "Some";
        case CM_AST_V2_OPTION_NONE: return "None";
        case CM_AST_V2_RESULT_OK: return "Ok";
        case CM_AST_V2_RESULT_ERR: return "Err";
        case CM_AST_V2_TYPE_NAMED: return "type_named";
        case CM_AST_V2_TYPE_PTR: return "type_ptr";
        case CM_AST_V2_TYPE_OPTION: return "type_option";
        case CM_AST_V2_TYPE_RESULT: return "type_result";
        case CM_AST_V2_TYPE_ARRAY: return "type_array";
        case CM_AST_V2_TYPE_SLICE: return "type_slice";
        case CM_AST_V2_TYPE_MAP: return "type_map";
        case CM_AST_V2_TYPE_FN: return "type_fn";
        case CM_AST_V2_PARAM: return "param";
        case CM_AST_V2_MATCH_ARM: return "match_arm";
        case CM_AST_V2_ATTRIBUTE: return "attribute";
        case CM_AST_V2_POLYGLOT: return "polyglot";
        default: return "unknown";
    }
}
