#ifndef CM_ERROR_OBJ_H
#define CM_ERROR_OBJ_H

#include "cm/core.h"
#include "cm/error.h"
#include "cm/error_detail.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Object API
 * Programmatic error handling with rich error information
 * ========================================================================== */

typedef struct cm_error_obj cm_error_obj_t;

/* Create error object from detail */
cm_error_obj_t* cm_error_obj_create(const cm_error_detail_t* detail);
cm_error_obj_t* cm_error_obj_create_simple(cm_error_code_t code, const char* message);

/* Getters */
cm_error_code_t cm_error_obj_get_code(const cm_error_obj_t* err);
const char* cm_error_obj_get_message(const cm_error_obj_t* err);
const char* cm_error_obj_get_file(const cm_error_obj_t* err);
int cm_error_obj_get_line(const cm_error_obj_t* err);
int cm_error_obj_get_column(const cm_error_obj_t* err);
const char* cm_error_obj_get_object_name(const cm_error_obj_t* err);
const char* cm_error_obj_get_object_type(const cm_error_obj_t* err);
const char* cm_error_obj_get_suggestion(const cm_error_obj_t* err);
cm_error_severity_t cm_error_obj_get_severity(const cm_error_obj_t* err);

/* Check if error matches code */
int cm_error_obj_is(const cm_error_obj_t* err, cm_error_code_t code);

/* Print error */
void cm_error_obj_print(const cm_error_obj_t* err);

/* Convert to JSON */
cm_string_t* cm_error_obj_to_json(const cm_error_obj_t* err);

/* Free error object */
void cm_error_obj_free(cm_error_obj_t* err);

/* Error chain (multiple errors) */
typedef struct cm_error_chain cm_error_chain_t;

cm_error_chain_t* cm_error_chain_create(void);
void cm_error_chain_add(cm_error_chain_t* chain, cm_error_obj_t* err);
int cm_error_chain_count(const cm_error_chain_t* chain);
cm_error_obj_t* cm_error_chain_get(const cm_error_chain_t* chain, int index);
void cm_error_chain_print_all(const cm_error_chain_t* chain);
void cm_error_chain_free(cm_error_chain_t* chain);

#ifdef __cplusplus
}
#endif

#endif /* CM_ERROR_OBJ_H */
