#ifndef CM_SAFE_PTR_H
#define CM_SAFE_PTR_H

#include "cm/core.h"
#include "cm/error.h"
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * CM v2 Safe Pointer System with Bounds Checking
 * ==========================================================================*/

/* Safe pointer structure with metadata */
typedef struct {
    void* base;          /* Actual pointer to data */
    size_t size;         /* Size of allocation in bytes */
    size_t count;        /* Number of elements (for arrays) */
    size_t element_size; /* Size of each element */
    uint32_t magic;      /* Canary for use-after-free detection */
    uint32_t flags;      /* Flags for mutability, etc. */
} cm_safe_ptr_t;

/* Safe pointer flags */
#define CM_PTR_FLAG_MUTABLE      (1 << 0)  /* Pointer is mutable */
#define CM_PTR_FLAG_IMMUTABLE    (1 << 1)  /* Pointer is immutable */
#define CM_PTR_FLAG_ARRAY        (1 << 2)  /* Pointer is an array */
#define CM_PTR_FLAG_GC_MANAGED   (1 << 3)  /* Managed by garbage collector */

/* Magic number for corruption detection */
#define CM_SAFE_PTR_MAGIC        0xDEADBEEF
#define CM_SAFE_PTR_FREED        0xFEEDFACE

/* Bounds checking macro */
#define CM_BOUNDS_CHECK(ptr, index) \
    do { \
        if (!(ptr) || (ptr)->magic != CM_SAFE_PTR_MAGIC) { \
            cm_panic("use-after-free or invalid pointer"); \
        } \
        if ((size_t)(index) >= (ptr)->count) { \
            cm_panic("index out of bounds: %zu >= %zu", \
                     (size_t)(index), (ptr)->count); \
        } \
    } while(0)

/* Safe dereference macro */
#define CM_SAFE_DEREF(ptr) \
    (((ptr) && (ptr)->magic == CM_SAFE_PTR_MAGIC) ? (ptr)->base : NULL)

/* Safe address-of macro */
#define CM_SAFE_ADDR_OF(var) \
    cm_safe_ptr_new(&(var), sizeof(var), 1, sizeof(var), CM_PTR_FLAG_IMMUTABLE)

/* Function declarations */

/* Create a new safe pointer */
cm_safe_ptr_t* cm_safe_ptr_new(void* base, size_t size, size_t count, 
                               size_t element_size, uint32_t flags);

/* Create a safe pointer from malloc */
cm_safe_ptr_t* cm_safe_ptr_malloc(size_t count, size_t element_size, uint32_t flags);

/* Free a safe pointer */
void cm_safe_ptr_free(cm_safe_ptr_t* ptr);

/* Get element at index (with bounds checking) */
void* cm_safe_ptr_get(cm_safe_ptr_t* ptr, size_t index);

/* Set element at index (with bounds checking and mutability check) */
int cm_safe_ptr_set(cm_safe_ptr_t* ptr, size_t index, const void* value);

/* Get pointer to element (for passing to C functions) */
void* cm_safe_ptr_data(cm_safe_ptr_t* ptr);

/* Get array length */
size_t cm_safe_ptr_len(cm_safe_ptr_t* ptr);

/* Check if pointer is valid */
int cm_safe_ptr_is_valid(cm_safe_ptr_t* ptr);

/* Make pointer immutable */
void cm_safe_ptr_make_immutable(cm_safe_ptr_t* ptr);

/* Clone a safe pointer (deep copy if array) */
cm_safe_ptr_t* cm_safe_ptr_clone(cm_safe_ptr_t* ptr);

/* Slice a safe pointer (creates view into original) */
cm_safe_ptr_t* cm_safe_ptr_slice(cm_safe_ptr_t* ptr, size_t start, size_t len);

/* Debug functions */
void cm_safe_ptr_debug_print(cm_safe_ptr_t* ptr);

/* Panic function for runtime errors */
void cm_panic(const char* format, ...);

#endif
