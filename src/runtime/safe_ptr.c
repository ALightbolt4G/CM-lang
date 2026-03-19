#include "cm/safe_ptr.h"
#include "cm/memory.h"
#include "cm/error.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

/* ============================================================================
 * CM v2 Safe Pointer Implementation
 * ==========================================================================*/

/* Global counter for debugging */
static size_t g_safe_ptr_count = 0;

/* Panic function for runtime errors */
void cm_panic(const char* format, ...) {
    fprintf(stderr, "\n=== CM RUNTIME PANIC ===\n");
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n========================\n");
    
    // In debug mode, show stack trace or additional info
    #ifdef DEBUG
    fprintf(stderr, "Safe pointers allocated: %zu\n", g_safe_ptr_count);
    #endif
    
    abort();
}

/* Create a new safe pointer */
cm_safe_ptr_t* cm_safe_ptr_new(void* base, size_t size, size_t count, 
                               size_t element_size, uint32_t flags) {
    if (!base && size > 0) {
        cm_panic("cannot create safe pointer with NULL base and size > 0");
        return NULL;
    }
    
    cm_safe_ptr_t* ptr = (cm_safe_ptr_t*)cm_alloc(sizeof(cm_safe_ptr_t), "safe_ptr");
    if (!ptr) return NULL;
    
    ptr->base = base;
    ptr->size = size;
    ptr->count = count;
    ptr->element_size = element_size;
    ptr->magic = CM_SAFE_PTR_MAGIC;
    ptr->flags = flags;
    
    g_safe_ptr_count++;
    
    return ptr;
}

/* Create a safe pointer from malloc */
cm_safe_ptr_t* cm_safe_ptr_malloc(size_t count, size_t element_size, uint32_t flags) {
    if (count == 0 || element_size == 0) {
        return cm_safe_ptr_new(NULL, 0, 0, 0, flags);
    }
    
    size_t total_size = count * element_size;
    
    // Check for overflow
    if (count > SIZE_MAX / element_size) {
        cm_panic("allocation size overflow: %zu * %zu", count, element_size);
        return NULL;
    }
    
    void* base = cm_alloc(total_size, "safe_ptr_data");
    if (!base) return NULL;
    
    // Zero-initialize the memory
    memset(base, 0, total_size);
    
    uint32_t ptr_flags = flags | CM_PTR_FLAG_GC_MANAGED;
    if (flags & CM_PTR_FLAG_MUTABLE) {
        ptr_flags &= ~CM_PTR_FLAG_IMMUTABLE;
    } else {
        ptr_flags |= CM_PTR_FLAG_IMMUTABLE;
    }
    
    return cm_safe_ptr_new(base, total_size, count, element_size, ptr_flags);
}

/* Free a safe pointer */
void cm_safe_ptr_free(cm_safe_ptr_t* ptr) {
    if (!ptr) return;
    
    if (ptr->magic != CM_SAFE_PTR_MAGIC) {
        cm_panic("attempting to free invalid or already freed pointer");
        return;
    }
    
    // Mark as freed for debugging
    ptr->magic = CM_SAFE_PTR_FREED;
    
    // Free the underlying data if it's GC-managed
    if (ptr->base && (ptr->flags & CM_PTR_FLAG_GC_MANAGED)) {
        cm_free(ptr->base);
    }
    
    // Free the pointer structure
    cm_free(ptr);
    g_safe_ptr_count--;
}

/* Get element at index (with bounds checking) */
void* cm_safe_ptr_get(cm_safe_ptr_t* ptr, size_t index) {
    CM_BOUNDS_CHECK(ptr, index);
    
    if (!ptr->base) return NULL;
    
    // Calculate element offset
    char* element_ptr = (char*)ptr->base + (index * ptr->element_size);
    return element_ptr;
}

/* Set element at index (with bounds checking and mutability check) */
int cm_safe_ptr_set(cm_safe_ptr_t* ptr, size_t index, const void* value) {
    CM_BOUNDS_CHECK(ptr, index);
    
    if (!ptr->base || !value) return 0;
    
    // Check mutability
    if (ptr->flags & CM_PTR_FLAG_IMMUTABLE) {
        cm_panic("attempt to modify immutable pointer");
        return 0;
    }
    
    // Copy the value
    char* element_ptr = (char*)ptr->base + (index * ptr->element_size);
    memcpy(element_ptr, value, ptr->element_size);
    
    return 1;
}

/* Get pointer to element (for passing to C functions) */
void* cm_safe_ptr_data(cm_safe_ptr_t* ptr) {
    if (!ptr || ptr->magic != CM_SAFE_PTR_MAGIC) {
        return NULL;
    }
    return ptr->base;
}

/* Get array length */
size_t cm_safe_ptr_len(cm_safe_ptr_t* ptr) {
    if (!ptr || ptr->magic != CM_SAFE_PTR_MAGIC) {
        return 0;
    }
    return ptr->count;
}

/* Check if pointer is valid */
int cm_safe_ptr_is_valid(cm_safe_ptr_t* ptr) {
    return ptr && ptr->magic == CM_SAFE_PTR_MAGIC;
}

/* Make pointer immutable */
void cm_safe_ptr_make_immutable(cm_safe_ptr_t* ptr) {
    if (!ptr || ptr->magic != CM_SAFE_PTR_MAGIC) {
        return;
    }
    
    ptr->flags |= CM_PTR_FLAG_IMMUTABLE;
    ptr->flags &= ~CM_PTR_FLAG_MUTABLE;
}

/* Clone a safe pointer (deep copy if array) */
cm_safe_ptr_t* cm_safe_ptr_clone(cm_safe_ptr_t* ptr) {
    if (!ptr || ptr->magic != CM_SAFE_PTR_MAGIC) {
        return NULL;
    }
    
    // Allocate new memory
    cm_safe_ptr_t* clone = cm_safe_ptr_malloc(ptr->count, ptr->element_size, ptr->flags);
    if (!clone) return NULL;
    
    // Copy the data
    if (ptr->base && ptr->size > 0) {
        memcpy(clone->base, ptr->base, ptr->size);
    }
    
    return clone;
}

/* Slice a safe pointer (creates view into original) */
cm_safe_ptr_t* cm_safe_ptr_slice(cm_safe_ptr_t* ptr, size_t start, size_t len) {
    if (!ptr || ptr->magic != CM_SAFE_PTR_MAGIC) {
        return NULL;
    }
    
    // Check bounds
    if (start + len > ptr->count) {
        cm_panic("slice out of bounds: %zu + %zu > %zu", start, len, ptr->count);
        return NULL;
    }
    
    // Calculate new base pointer
    char* new_base = (char*)ptr->base + (start * ptr->element_size);
    
    // Create slice (not GC-managed, just a view)
    cm_safe_ptr_t* slice = cm_safe_ptr_new(new_base, len * ptr->element_size, 
                                          len, ptr->element_size, ptr->flags);
    if (slice) {
        slice->flags &= ~CM_PTR_FLAG_GC_MANAGED; // Slices are views, not owners
    }
    
    return slice;
}

/* Debug functions */
void cm_safe_ptr_debug_print(cm_safe_ptr_t* ptr) {
    if (!ptr) {
        printf("NULL pointer\n");
        return;
    }
    
    printf("Safe Pointer@%p:\n", (void*)ptr);
    printf("  magic: 0x%08X %s\n", ptr->magic, 
           ptr->magic == CM_SAFE_PTR_MAGIC ? "(valid)" : 
           ptr->magic == CM_SAFE_PTR_FREED ? "(freed)" : "(corrupt)");
    printf("  base: %p\n", ptr->base);
    printf("  size: %zu bytes\n", ptr->size);
    printf("  count: %zu elements\n", ptr->count);
    printf("  element_size: %zu bytes\n", ptr->element_size);
    printf("  flags: 0x%08X", ptr->flags);
    
    if (ptr->flags & CM_PTR_FLAG_MUTABLE) printf(" (mutable)");
    if (ptr->flags & CM_PTR_FLAG_IMMUTABLE) printf(" (immutable)");
    if (ptr->flags & CM_PTR_FLAG_ARRAY) printf(" (array)");
    if (ptr->flags & CM_PTR_FLAG_GC_MANAGED) printf(" (gc_managed)");
    
    printf("\n");
    printf("  total safe pointers: %zu\n", g_safe_ptr_count);
}

/* Runtime statistics */
size_t cm_safe_ptr_get_allocated_count(void) {
    return g_safe_ptr_count;
}

/* Initialize safe pointer system */
void cm_safe_ptr_init(void) {
    g_safe_ptr_count = 0;
}

/* Shutdown safe pointer system (check for leaks) */
void cm_safe_ptr_shutdown(void) {
    #ifdef DEBUG
    if (g_safe_ptr_count > 0) {
        fprintf(stderr, "WARNING: %zu safe pointers leaked\n", g_safe_ptr_count);
    }
    #endif
    g_safe_ptr_count = 0;
}
