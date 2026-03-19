#include "cm/error_obj.h"
#include "cm/string.h"
#include "cm/memory.h"
#include <stdlib.h>
#include <string.h>

struct cm_error_obj {
    cm_error_detail_t detail;
    int refcount;
};

struct cm_error_chain {
    cm_error_obj_t** errors;
    int count;
    int capacity;
};

cm_error_obj_t* cm_error_obj_create(const cm_error_detail_t* detail) {
    if (!detail) return NULL;
    
    cm_error_obj_t* err = (cm_error_obj_t*)malloc(sizeof(cm_error_obj_t));
    if (!err) return NULL;
    
    memcpy(&err->detail, detail, sizeof(cm_error_detail_t));
    err->refcount = 1;
    return err;
}

cm_error_obj_t* cm_error_obj_create_simple(cm_error_code_t code, const char* message) {
    cm_error_detail_t detail;
    cm_error_detail_init(&detail);
    detail.code = code;
    cm_error_detail_set_message(&detail, "%s", message ? message : "Unknown error");
    return cm_error_obj_create(&detail);
}

cm_error_code_t cm_error_obj_get_code(const cm_error_obj_t* err) {
    return err ? err->detail.code : CM_ERROR_UNKNOWN;
}

const char* cm_error_obj_get_message(const cm_error_obj_t* err) {
    return err ? err->detail.message : "";
}

const char* cm_error_obj_get_file(const cm_error_obj_t* err) {
    return err ? err->detail.file : "";
}

int cm_error_obj_get_line(const cm_error_obj_t* err) {
    return err ? err->detail.line : -1;
}

int cm_error_obj_get_column(const cm_error_obj_t* err) {
    return err ? err->detail.column : -1;
}

const char* cm_error_obj_get_object_name(const cm_error_obj_t* err) {
    return err ? err->detail.object_name : "";
}

const char* cm_error_obj_get_object_type(const cm_error_obj_t* err) {
    return err ? err->detail.object_type : "";
}

const char* cm_error_obj_get_suggestion(const cm_error_obj_t* err) {
    return err ? err->detail.suggestion : "";
}

cm_error_severity_t cm_error_obj_get_severity(const cm_error_obj_t* err) {
    return err ? err->detail.severity : CM_SEVERITY_ERROR;
}

int cm_error_obj_is(const cm_error_obj_t* err, cm_error_code_t code) {
    return err && err->detail.code == code;
}

void cm_error_obj_print(const cm_error_obj_t* err) {
    if (!err) return;
    cm_error_detail_print(&err->detail);
}

cm_string_t* cm_error_obj_to_json(const cm_error_obj_t* err) {
    if (!err) return cm_string_new("null");
    return cm_error_detail_to_json(&err->detail);
}

void cm_error_obj_free(cm_error_obj_t* err) {
    if (!err) return;
    err->refcount--;
    if (err->refcount <= 0) {
        free(err);
    }
}

cm_error_chain_t* cm_error_chain_create(void) {
    cm_error_chain_t* chain = (cm_error_chain_t*)malloc(sizeof(cm_error_chain_t));
    if (!chain) return NULL;
    
    chain->capacity = 8;
    chain->count = 0;
    chain->errors = (cm_error_obj_t**)malloc(sizeof(cm_error_obj_t*) * chain->capacity);
    
    if (!chain->errors) {
        free(chain);
        return NULL;
    }
    
    return chain;
}

void cm_error_chain_add(cm_error_chain_t* chain, cm_error_obj_t* err) {
    if (!chain || !err) return;
    
    if (chain->count >= chain->capacity) {
        chain->capacity *= 2;
        cm_error_obj_t** new_errors = (cm_error_obj_t**)realloc(chain->errors, 
                                                                  sizeof(cm_error_obj_t*) * chain->capacity);
        if (!new_errors) return;
        chain->errors = new_errors;
    }
    
    chain->errors[chain->count++] = err;
}

int cm_error_chain_count(const cm_error_chain_t* chain) {
    return chain ? chain->count : 0;
}

cm_error_obj_t* cm_error_chain_get(const cm_error_chain_t* chain, int index) {
    if (!chain || index < 0 || index >= chain->count) return NULL;
    return chain->errors[index];
}

void cm_error_chain_print_all(const cm_error_chain_t* chain) {
    if (!chain) return;
    
    for (int i = 0; i < chain->count; i++) {
        fprintf(stderr, "\n[%d/%d] ", i + 1, chain->count);
        cm_error_obj_print(chain->errors[i]);
    }
}

void cm_error_chain_free(cm_error_chain_t* chain) {
    if (!chain) return;
    
    for (int i = 0; i < chain->count; i++) {
        cm_error_obj_free(chain->errors[i]);
    }
    
    free(chain->errors);
    free(chain);
}
