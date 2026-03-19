/**
 * @file dynarray.h
 * @brief Dynamic array (vector) with automatic memory management.
 *
 * A safe, GC-managed dynamic array that grows automatically.
 * All operations are bounds-checked.
 */
#ifndef CM_DYNARRAY_H
#define CM_DYNARRAY_H

#include "core.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dynamic array with automatic resizing.
 */
typedef struct {
    void** data;           /* Array of pointers */
    size_t size;          /* Current number of elements */
    size_t capacity;      /* Allocated capacity */
    cm_ptr_t gc_handle;
} cm_dynarray_t;

/**
 * @brief Create a new dynamic array.
 * @param initial_capacity Initial capacity (0 for default).
 * @return New array or NULL on error.
 */
cm_dynarray_t* cm_dynarray_new(size_t initial_capacity);

/**
 * @brief Free a dynamic array.
 * @param arr The array to free.
 */
void cm_dynarray_free(cm_dynarray_t* arr);

/**
 * @brief Push element to end of array.
 * @param arr The array.
 * @param data The element to push.
 * @return 0 on success, -1 on error.
 */
int cm_dynarray_push(cm_dynarray_t* arr, void* data);

/**
 * @brief Pop element from end of array.
 * @param arr The array.
 * @return The popped element or NULL if empty.
 */
void* cm_dynarray_pop(cm_dynarray_t* arr);

/**
 * @brief Get element at index.
 * @param arr The array.
 * @param index The index.
 * @return The element or NULL if out of bounds.
 */
void* cm_dynarray_get(cm_dynarray_t* arr, size_t index);

/**
 * @brief Set element at index.
 * @param arr The array.
 * @param index The index.
 * @param data The element.
 * @return 0 on success, -1 on error.
 */
int cm_dynarray_set(cm_dynarray_t* arr, size_t index, void* data);

/**
 * @brief Insert element at index.
 * @param arr The array.
 * @param index The index to insert at.
 * @param data The element.
 * @return 0 on success, -1 on error.
 */
int cm_dynarray_insert(cm_dynarray_t* arr, size_t index, void* data);

/**
 * @brief Remove element at index.
 * @param arr The array.
 * @param index The index.
 * @return 0 on success, -1 on error.
 */
int cm_dynarray_remove(cm_dynarray_t* arr, size_t index);

/**
 * @brief Resize array to new capacity.
 * @param arr The array.
 * @param new_capacity The new capacity.
 * @return 0 on success, -1 on error.
 */
int cm_dynarray_resize(cm_dynarray_t* arr, size_t new_capacity);

/**
 * @brief Get array size.
 * @param arr The array.
 * @return Number of elements.
 */
size_t cm_dynarray_size(cm_dynarray_t* arr);

/**
 * @brief Get array capacity.
 * @param arr The array.
 * @return Current capacity.
 */
size_t cm_dynarray_capacity(cm_dynarray_t* arr);

/**
 * @brief Check if array is empty.
 * @param arr The array.
 * @return 1 if empty, 0 otherwise.
 */
int cm_dynarray_empty(cm_dynarray_t* arr);

/**
 * @brief Clear all elements.
 * @param arr The array.
 */
void cm_dynarray_clear(cm_dynarray_t* arr);

/**
 * @brief Find index of element.
 * @param arr The array.
 * @param data The element to find (by pointer).
 * @return Index or -1 if not found.
 */
long cm_dynarray_index_of(cm_dynarray_t* arr, void* data);

/**
 * @brief Check if array contains element.
 * @param arr The array.
 * @param data The element.
 * @return 1 if found, 0 otherwise.
 */
int cm_dynarray_contains(cm_dynarray_t* arr, void* data);

/**
 * @brief Iterate over array.
 * @param arr The array.
 * @param callback Function called for each element.
 * @param userdata Optional userdata.
 */
typedef void (*cm_dynarray_iter_cb)(void* data, size_t index, void* userdata);
void cm_dynarray_foreach(cm_dynarray_t* arr, cm_dynarray_iter_cb callback, void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* CM_DYNARRAY_H */
