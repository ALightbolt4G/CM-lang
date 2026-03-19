#include "cm/result.h"
#include "cm/memory.h"
#include "cm/error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * CM v2 Result Implementation
 * ==========================================================================*/

/* Create Ok result */
cm_result_t cm_result_ok(const void* value, size_t value_size) {
    if (!value || value_size == 0) {
        cm_result_t empty = {CM_RESULT_OK, NULL, NULL, 0, 0};
        return empty;
    }
    
    void* value_copy = cm_alloc(value_size, "result_value");
    if (!value_copy) {
        cm_result_t empty = {CM_RESULT_ERR, NULL, NULL, 0, 0};
        return empty;
    }
    
    memcpy(value_copy, value, value_size);
    
    cm_result_t result;
    result.kind = CM_RESULT_OK;
    result.value = value_copy;
    result.error = NULL;
    result.value_size = value_size;
    result.error_size = 0;
    
    return result;
}

/* Create Err result */
cm_result_t cm_result_err(const void* error, size_t error_size) {
    if (!error || error_size == 0) {
        cm_result_t empty = {CM_RESULT_ERR, NULL, NULL, 0, 0};
        return empty;
    }
    
    void* error_copy = cm_alloc(error_size, "result_error");
    if (!error_copy) {
        cm_result_t empty = {CM_RESULT_ERR, NULL, NULL, 0, 0};
        return empty;
    }
    
    memcpy(error_copy, error, error_size);
    
    cm_result_t result;
    result.kind = CM_RESULT_ERR;
    result.value = NULL;
    result.error = error_copy;
    result.value_size = 0;
    result.error_size = error_size;
    
    return result;
}

/* Check if result is Ok */
int cm_result_is_ok(const cm_result_t* result) {
    return result && result->kind == CM_RESULT_OK;
}

/* Check if result is Err */
int cm_result_is_err(const cm_result_t* result) {
    return !result || result->kind == CM_RESULT_ERR;
}

/* Get value from Ok result (returns NULL if Err) */
void* cm_result_unwrap(const cm_result_t* result) {
    if (cm_result_is_err(result)) {
        return NULL;
    }
    return result->value;
}

/* Get error from Err result (returns NULL if Ok) */
void* cm_result_unwrap_err(const cm_result_t* result) {
    if (cm_result_is_ok(result)) {
        return NULL;
    }
    return result->error;
}

/* Get value from Ok result or panic if Err */
void* cm_result_expect(const cm_result_t* result, const char* message) {
    if (cm_result_is_err(result)) {
        fprintf(stderr, "\n=== RESULT EXPECTATION FAILED ===\n");
        fprintf(stderr, "Message: %s\n", message ? message : "Expected Ok but got Err");
        if (result->error) {
            fprintf(stderr, "Error: ");
            // Try to print error as string if it looks like one
            char* error_str = (char*)result->error;
            if (result->error_size > 0 && error_str[result->error_size - 1] == '\0') {
                fprintf(stderr, "%s\n", error_str);
            } else {
                fprintf(stderr, "<binary data %zu bytes>\n", result->error_size);
            }
        }
        fprintf(stderr, "=================================\n");
        abort();
    }
    return result->value;
}

/* Map function over Ok value */
cm_result_t cm_result_map(const cm_result_t* result, void* (*mapper)(const void*), size_t result_size) {
    if (cm_result_is_err(result) || !mapper) {
        return cm_result_err(result->error, result->error_size);
    }
    
    void* mapped_value = mapper(result->value);
    if (!mapped_value) {
        return cm_result_err("map function failed", 19);
    }
    
    cm_result_t mapped = cm_result_ok(mapped_value, result_size);
    cm_free(mapped_value);
    
    return mapped;
}

/* Map error */
cm_result_t cm_result_map_err(const cm_result_t* result, void* (*mapper)(const void*), size_t error_size) {
    if (cm_result_is_ok(result) || !mapper) {
        return cm_result_ok(result->value, result->value_size);
    }
    
    void* mapped_error = mapper(result->error);
    if (!mapped_error) {
        return cm_result_err("error map function failed", 26);
    }
    
    cm_result_t mapped = cm_result_err(mapped_error, error_size);
    cm_free(mapped_error);
    
    return mapped;
}

/* Chain results */
cm_result_t cm_result_and_then(const cm_result_t* result, cm_result_t (*binder)(const void*)) {
    if (cm_result_is_err(result) || !binder) {
        return cm_result_err(result->error, result->error_size);
    }
    
    return binder(result->value);
}

/* Free result */
void cm_result_free(cm_result_t* result) {
    if (!result) return;
    
    if (result->kind == CM_RESULT_OK && result->value) {
        cm_free(result->value);
        result->value = NULL;
    }
    
    if (result->kind == CM_RESULT_ERR && result->error) {
        cm_free(result->error);
        result->error = NULL;
    }
    
    result->kind = CM_RESULT_ERR; // Set to invalid state
    result->value_size = 0;
    result->error_size = 0;
}

/* Clone result */
cm_result_t cm_result_clone(const cm_result_t* result) {
    if (cm_result_is_ok(result)) {
        return cm_result_ok(result->value, result->value_size);
    } else {
        return cm_result_err(result->error, result->error_size);
    }
}
