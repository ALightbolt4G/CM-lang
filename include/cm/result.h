#ifndef CM_RESULT_H
#define CM_RESULT_H

#include "cm/core.h"
#include <stddef.h>

/* ============================================================================
 * CM v2 Result Type - Ok(T) | Err(E)
 * ==========================================================================*/

/* Result discriminants */
typedef enum {
    CM_RESULT_OK,
    CM_RESULT_ERR
} cm_result_kind_t;

/* Result type */
typedef struct {
    cm_result_kind_t kind;
    void* value;        /* Valid only when kind == CM_RESULT_OK */
    void* error;        /* Valid only when kind == CM_RESULT_ERR */
    size_t value_size;   /* Size of the value */
    size_t error_size;   /* Size of the error */
} cm_result_t;

/* Macros for creating results */
#define CM_RESULT_OK_INIT(v)    {CM_RESULT_OK, &(v), NULL, sizeof(v), 0}
#define CM_RESULT_ERR_INIT(e)   {CM_RESULT_ERR, NULL, &(e), 0, sizeof(e)}

/* Function declarations */

/* Create Ok result */
cm_result_t cm_result_ok(const void* value, size_t value_size);

/* Create Err result */
cm_result_t cm_result_err(const void* error, size_t error_size);

/* Check if result is Ok */
int cm_result_is_ok(const cm_result_t* result);

/* Check if result is Err */
int cm_result_is_err(const cm_result_t* result);

/* Get value from Ok result (returns NULL if Err) */
void* cm_result_unwrap(const cm_result_t* result);

/* Get error from Err result (returns NULL if Ok) */
void* cm_result_unwrap_err(const cm_result_t* result);

/* Get value from Ok result or panic if Err */
void* cm_result_expect(const cm_result_t* result, const char* message);

/* Map function over Ok value */
cm_result_t cm_result_map(const cm_result_t* result, void* (*mapper)(const void*), size_t result_size);

/* Map error */
cm_result_t cm_result_map_err(const cm_result_t* result, void* (*mapper)(const void*), size_t error_size);

/* Chain results */
cm_result_t cm_result_and_then(const cm_result_t* result, cm_result_t (*binder)(const void*));

/* Free result */
void cm_result_free(cm_result_t* result);

/* Clone result */
cm_result_t cm_result_clone(const cm_result_t* result);

#endif
