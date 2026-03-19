#include "cm/option.h"
#include "cm/memory.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * CM v2 Option Implementation
 * ==========================================================================*/

/* Create None option */
cm_option_t cm_option_none(void) {
    cm_option_t opt = CM_OPTION_NONE_INIT;
    return opt;
}

/* Create Some option */
cm_option_t cm_option_some(const void* value, size_t value_size) {
    if (!value || value_size == 0) {
        return cm_option_none();
    }
    
    void* value_copy = cm_alloc(value_size, "option_value");
    if (!value_copy) {
        return cm_option_none();
    }
    
    memcpy(value_copy, value, value_size);
    
    cm_option_t opt;
    opt.kind = CM_OPTION_SOME;
    opt.value = value_copy;
    opt.value_size = value_size;
    
    return opt;
}

/* Check if option is Some */
int cm_option_is_some(const cm_option_t* opt) {
    return opt && opt->kind == CM_OPTION_SOME;
}

/* Check if option is None */
int cm_option_is_none(const cm_option_t* opt) {
    return !opt || opt->kind == CM_OPTION_NONE;
}

/* Get value from Some option (returns NULL if None) */
void* cm_option_unwrap(const cm_option_t* opt) {
    if (cm_option_is_none(opt)) {
        return NULL;
    }
    return opt->value;
}

/* Get value from Some option or return default if None */
void* cm_option_unwrap_or(const cm_option_t* opt, const void* default_value, void* output, size_t output_size) {
    if (cm_option_is_some(opt)) {
        if (output && opt->value) {
            memcpy(output, opt->value, output_size < opt->value_size ? output_size : opt->value_size);
        }
        return output ? output : opt->value;
    }
    
    if (output && default_value) {
        memcpy(output, default_value, output_size);
    }
    return output;
}

/* Map function over Some value */
cm_option_t cm_option_map(const cm_option_t* opt, void* (*mapper)(const void*), size_t result_size) {
    if (cm_option_is_none(opt) || !mapper) {
        return cm_option_none();
    }
    
    void* result = mapper(opt->value);
    if (!result) {
        return cm_option_none();
    }
    
    cm_option_t mapped = cm_option_some(result, result_size);
    cm_free(result);
    
    return mapped;
}

/* Free option value */
void cm_option_free(cm_option_t* opt) {
    if (!opt) return;
    
    if (opt->kind == CM_OPTION_SOME && opt->value) {
        cm_free(opt->value);
        opt->value = NULL;
    }
    
    opt->kind = CM_OPTION_NONE;
    opt->value_size = 0;
}

/* Clone option */
cm_option_t cm_option_clone(const cm_option_t* opt) {
    if (cm_option_is_none(opt)) {
        return cm_option_none();
    }
    
    return cm_option_some(opt->value, opt->value_size);
}
