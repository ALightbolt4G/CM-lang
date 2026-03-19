/**
 * @file list.h
 * @brief Doubly linked list with automatic memory management.
 *
 * A safe, GC-managed doubly linked list implementation.
 * All operations are bounds-checked and memory-safe.
 */
#ifndef CM_LIST_H
#define CM_LIST_H

#include "core.h"
#include "memory.h"
#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cm_list_node {
    void* data;
    struct cm_list_node* next;
    struct cm_list_node* prev;
} cm_list_node_t;

/**
 * @brief Doubly linked list with GC integration.
 */
typedef struct {
    cm_list_node_t* head;
    cm_list_node_t* tail;
    size_t count;
    cm_ptr_t gc_handle;
} cm_list_t;

/**
 * @brief Create a new empty list.
 * @return New list instance or NULL on error.
 */
cm_list_t* cm_list_new(void);

/**
 * @brief Free a list and all its nodes.
 * @param list The list to free.
 */
void cm_list_free(cm_list_t* list);

/**
 * @brief Append data to end of list.
 * @param list The list.
 * @param data The data to append.
 * @return 0 on success, -1 on error.
 */
int cm_list_append(cm_list_t* list, void* data);

/**
 * @brief Prepend data to beginning of list.
 * @param list The list.
 * @param data The data to prepend.
 * @return 0 on success, -1 on error.
 */
int cm_list_prepend(cm_list_t* list, void* data);

/**
 * @brief Remove first occurrence of data from list.
 * @param list The list.
 * @param data The data to remove (compares by pointer).
 * @return 0 if found and removed, -1 if not found.
 */
int cm_list_remove(cm_list_t* list, void* data);

/**
 * @brief Remove node at index.
 * @param list The list.
 * @param index The index to remove.
 * @return 0 on success, -1 on error.
 */
int cm_list_remove_at(cm_list_t* list, size_t index);

/**
 * @brief Get data at index.
 * @param list The list.
 * @param index The index.
 * @return The data or NULL if out of bounds.
 */
void* cm_list_get(cm_list_t* list, size_t index);

/**
 * @brief Set data at index.
 * @param list The list.
 * @param index The index.
 * @param data The new data.
 * @return 0 on success, -1 on error.
 */
int cm_list_set(cm_list_t* list, size_t index, void* data);

/**
 * @brief Find index of data in list.
 * @param list The list.
 * @param data The data to find (compares by pointer).
 * @return Index or -1 if not found.
 */
long cm_list_index_of(cm_list_t* list, void* data);

/**
 * @brief Check if list contains data.
 * @param list The list.
 * @param data The data to check.
 * @return 1 if found, 0 otherwise.
 */
int cm_list_contains(cm_list_t* list, void* data);

/**
 * @brief Get list size.
 * @param list The list.
 * @return Number of elements.
 */
size_t cm_list_size(cm_list_t* list);

/**
 * @brief Check if list is empty.
 * @param list The list.
 * @return 1 if empty, 0 otherwise.
 */
int cm_list_empty(cm_list_t* list);

/**
 * @brief Clear all elements from list.
 * @param list The list.
 */
void cm_list_clear(cm_list_t* list);

/**
 * @brief Iterate over list with callback.
 * @param list The list.
 * @param callback Function called for each element.
 * @param userdata Optional userdata.
 */
typedef void (*cm_list_iter_cb)(void* data, size_t index, void* userdata);
void cm_list_foreach(cm_list_t* list, cm_list_iter_cb callback, void* userdata);

/**
 * @brief Print list contents (for debugging).
 * @param list The list.
 * @param to_string Optional function to convert data to string.
 */
typedef cm_string_t* (*cm_list_to_string_fn)(void* data);
void cm_list_print(cm_list_t* list, cm_list_to_string_fn to_string);

#ifdef __cplusplus
}
#endif

#endif /* CM_LIST_H */
