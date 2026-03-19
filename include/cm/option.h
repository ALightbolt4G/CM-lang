#ifndef CM_OPTION_H
#define CM_OPTION_H

#include "cm/core.h"
#include <stddef.h>

/* ============================================================================
 * CM v2 Option Type - Some(T) | None
 * ==========================================================================*/

/* Option discriminants */
typedef enum {
    CM_OPTION_NONE,
    CM_OPTION_SOME
} cm_option_kind_t;

/* Option type */
typedef struct {
    cm_option_kind_t kind;
    void* value;        /* Valid only when kind == CM_OPTION_SOME */
    size_t value_size;   /* Size of the value */
} cm_option_t;

/* Macros for creating options */
#define CM_OPTION_NONE_INIT    {CM_OPTION_NONE, NULL, 0}
#define CM_OPTION_SOME_INIT(v) {CM_OPTION_SOME, &(v), sizeof(v)}

/* Function declarations */

/* Create None option */
cm_option_t cm_option_none(void);

/* Create Some option */
cm_option_t cm_option_some(const void* value, size_t value_size);

/* Check if option is Some */
int cm_option_is_some(const cm_option_t* opt);

/* Check if option is None */
int cm_option_is_none(const cm_option_t* opt);

/* Get value from Some option (returns NULL if None) */
void* cm_option_unwrap(const cm_option_t* opt);

/* Get value from Some option or return default if None */
void* cm_option_unwrap_or(const cm_option_t* opt, const void* default_value, void* output, size_t output_size);

/* Map function over Some value */
cm_option_t cm_option_map(const cm_option_t* opt, void* (*mapper)(const void*), size_t result_size);

/* Free option value */
void cm_option_free(cm_option_t* opt);

/* Clone option */
cm_option_t cm_option_clone(const cm_option_t* opt);

#endif
