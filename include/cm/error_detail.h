#ifndef CM_ERROR_DETAIL_H
#define CM_ERROR_DETAIL_H

#include "cm/core.h"
#include "cm/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Enhanced Error Detail System
 * Provides detailed error information with context and suggestions
 * ========================================================================== */

typedef enum {
    CM_SEVERITY_WARNING,
    CM_SEVERITY_ERROR,
    CM_SEVERITY_FATAL
} cm_error_severity_t;

typedef struct {
    cm_error_code_t code;
    cm_error_severity_t severity;
    char file[256];
    int line;
    int column;
    char object_name[128];
    char object_type[64];
    char message[512];
    char suggestion[256];
    
    /* Context lines for syntax errors */
    char context_before[3][256];
    char context_line[256];
    char context_after[3][256];
    int context_count;
} cm_error_detail_t;

/* Initialize error detail with defaults */
void cm_error_detail_init(cm_error_detail_t* detail);

/* Set basic error info */
void cm_error_detail_set_location(cm_error_detail_t* detail, const char* file, int line, int column);
void cm_error_detail_set_object(cm_error_detail_t* detail, const char* type, const char* name);
void cm_error_detail_set_message(cm_error_detail_t* detail, const char* fmt, ...);
void cm_error_detail_set_suggestion(cm_error_detail_t* detail, const char* fmt, ...);

/* Set context lines for syntax errors */
void cm_error_detail_set_context(cm_error_detail_t* detail, 
                                  const char* before[3], int before_count,
                                  const char* current,
                                  const char* after[3], int after_count);

/* Print formatted error to stderr */
void cm_error_detail_print(const cm_error_detail_t* detail);

/* Print syntax error with line numbers and caret */
void cm_error_detail_print_syntax(const cm_error_detail_t* detail);

/* Print runtime error with stack trace */
void cm_error_detail_print_runtime(const cm_error_detail_t* detail);

/* Convert to JSON string */
cm_string_t* cm_error_detail_to_json(const cm_error_detail_t* detail);

/* Current error detail (thread-local) */
cm_error_detail_t* cm_error_detail_current(void);
void cm_error_detail_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* CM_ERROR_DETAIL_H */
