#include "cm/dynarray.h"
#include "cm/error.h"
#include <string.h>

#define CM_DYNARRAY_DEFAULT_CAPACITY 16

cm_dynarray_t* cm_dynarray_new(size_t initial_capacity) {
    cm_dynarray_t* arr = (cm_dynarray_t*)cm_alloc(sizeof(cm_dynarray_t), "dynarray");
    if (!arr) return NULL;
    
    arr->size = 0;
    arr->capacity = initial_capacity > 0 ? initial_capacity : CM_DYNARRAY_DEFAULT_CAPACITY;
    arr->data = (void**)cm_alloc(arr->capacity * sizeof(void*), "dynarray_data");
    arr->gc_handle.index = 0;
    arr->gc_handle.generation = 0;
    
    if (!arr->data) {
        cm_free(arr);
        return NULL;
    }
    
    return arr;
}

void cm_dynarray_free(cm_dynarray_t* arr) {
    if (!arr) return;
    if (arr->data) cm_free(arr->data);
    cm_free(arr);
}

static int cm_dynarray_ensure_capacity(cm_dynarray_t* arr, size_t min_capacity) {
    if (arr->capacity >= min_capacity) return 0;
    
    size_t new_capacity = arr->capacity * 2;
    if (new_capacity < min_capacity) new_capacity = min_capacity;
    
    void** new_data = (void**)cm_alloc(new_capacity * sizeof(void*), "dynarray_data");
    if (!new_data) return -1;
    
    memcpy(new_data, arr->data, arr->size * sizeof(void*));
    cm_free(arr->data);
    arr->data = new_data;
    arr->capacity = new_capacity;
    return 0;
}

int cm_dynarray_push(cm_dynarray_t* arr, void* data) {
    if (!arr) return -1;
    if (cm_dynarray_ensure_capacity(arr, arr->size + 1) != 0) return -1;
    arr->data[arr->size++] = data;
    return 0;
}

void* cm_dynarray_pop(cm_dynarray_t* arr) {
    if (!arr || arr->size == 0) return NULL;
    return arr->data[--arr->size];
}

void* cm_dynarray_get(cm_dynarray_t* arr, size_t index) {
    if (!arr || index >= arr->size) {
        cm_error_set(CM_ERROR_OUT_OF_BOUNDS, "array index out of bounds");
        return NULL;
    }
    return arr->data[index];
}

int cm_dynarray_set(cm_dynarray_t* arr, size_t index, void* data) {
    if (!arr || index >= arr->size) {
        cm_error_set(CM_ERROR_OUT_OF_BOUNDS, "array index out of bounds");
        return -1;
    }
    arr->data[index] = data;
    return 0;
}

int cm_dynarray_insert(cm_dynarray_t* arr, size_t index, void* data) {
    if (!arr || index > arr->size) {
        cm_error_set(CM_ERROR_OUT_OF_BOUNDS, "array insert index out of bounds");
        return -1;
    }
    if (cm_dynarray_ensure_capacity(arr, arr->size + 1) != 0) return -1;
    
    /* Shift elements right */
    for (size_t i = arr->size; i > index; i--) {
        arr->data[i] = arr->data[i - 1];
    }
    arr->data[index] = data;
    arr->size++;
    return 0;
}

int cm_dynarray_remove(cm_dynarray_t* arr, size_t index) {
    if (!arr || index >= arr->size) {
        cm_error_set(CM_ERROR_OUT_OF_BOUNDS, "array remove index out of bounds");
        return -1;
    }
    
    /* Shift elements left */
    for (size_t i = index; i < arr->size - 1; i++) {
        arr->data[i] = arr->data[i + 1];
    }
    arr->size--;
    return 0;
}

int cm_dynarray_resize(cm_dynarray_t* arr, size_t new_capacity) {
    if (!arr) return -1;
    if (new_capacity < arr->size) new_capacity = arr->size;
    
    void** new_data = (void**)cm_alloc(new_capacity * sizeof(void*), "dynarray_data");
    if (!new_data) return -1;
    
    memcpy(new_data, arr->data, arr->size * sizeof(void*));
    cm_free(arr->data);
    arr->data = new_data;
    arr->capacity = new_capacity;
    return 0;
}

size_t cm_dynarray_size(cm_dynarray_t* arr) {
    return arr ? arr->size : 0;
}

size_t cm_dynarray_capacity(cm_dynarray_t* arr) {
    return arr ? arr->capacity : 0;
}

int cm_dynarray_empty(cm_dynarray_t* arr) {
    return !arr || arr->size == 0;
}

void cm_dynarray_clear(cm_dynarray_t* arr) {
    if (!arr) return;
    arr->size = 0;
}

long cm_dynarray_index_of(cm_dynarray_t* arr, void* data) {
    if (!arr) return -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (arr->data[i] == data) return (long)i;
    }
    return -1;
}

int cm_dynarray_contains(cm_dynarray_t* arr, void* data) {
    return cm_dynarray_index_of(arr, data) >= 0;
}

void cm_dynarray_foreach(cm_dynarray_t* arr, cm_dynarray_iter_cb callback, void* userdata) {
    if (!arr || !callback) return;
    for (size_t i = 0; i < arr->size; i++) {
        callback(arr->data[i], i, userdata);
    }
}
