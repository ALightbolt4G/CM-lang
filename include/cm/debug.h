#ifndef CM_DEBUG_H
#define CM_DEBUG_H

#include "cm/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Runtime Debug & Variable Tracking
 * Tracks variable initialization, types, and values at runtime
 * ========================================================================== */

typedef enum {
    CM_VAR_UNINITIALIZED,
    CM_VAR_INITIALIZED,
    CM_VAR_FREED
} cm_var_state_t;

typedef struct cm_var_track {
    char name[128];
    char type[64];
    void* address;
    cm_var_state_t state;
    int line_declared;
    char file_declared[256];
    struct cm_var_track* next;
} cm_var_track_t;

/* Initialize debug tracking */
void cm_debug_init(void);
void cm_debug_shutdown(void);

/* Variable tracking */
void cm_debug_var_declare(const char* name, const char* type, void* addr, 
                          const char* file, int line);
void cm_debug_var_init(void* addr);
void cm_debug_var_free(void* addr);

/* Check variable state */
int cm_debug_var_is_initialized(void* addr);
const char* cm_debug_var_get_name(void* addr);
const char* cm_debug_var_get_type(void* addr);

/* Safety checks */
void cm_debug_check_null(const void* ptr, const char* name, 
                         const char* file, int line);
void cm_debug_check_initialized(void* addr, const char* name,
                                const char* file, int line);
void cm_debug_check_bounds(size_t index, size_t size, const char* name,
                           const char* file, int line);

/* Memory tracking */
void cm_debug_track_alloc(void* ptr, size_t size, const char* file, int line);
void cm_debug_track_free(void* ptr);
int cm_debug_is_valid_ptr(void* ptr);

/* Stack trace */
void cm_debug_print_stack_trace(void);
void cm_debug_capture_stack_trace(char** buffer, int max_depth);

/* Macros for automatic tracking */
#define CM_VAR_DECLARE(name, type, addr) \
    cm_debug_var_declare(name, type, addr, __FILE__, __LINE__)

#define CM_VAR_INIT(addr) \
    cm_debug_var_init(addr)

#define CM_CHECK_NULL(ptr) \
    cm_debug_check_null(ptr, #ptr, __FILE__, __LINE__)

#define CM_CHECK_INIT(addr, name) \
    cm_debug_check_initialized(addr, name, __FILE__, __LINE__)

#define CM_CHECK_BOUNDS(idx, size, name) \
    cm_debug_check_bounds(idx, size, name, __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* CM_DEBUG_H */
