#include "cm/list.h"
#include "cm/error.h"
#include <stdio.h>
#include <string.h>

static cm_list_node_t* cm_list_node_new(void* data) {
    cm_list_node_t* node = (cm_list_node_t*)cm_alloc(sizeof(cm_list_node_t), "list_node");
    if (!node) return NULL;
    node->data = data;
    node->next = NULL;
    node->prev = NULL;
    return node;
}

static void cm_list_node_free(cm_list_node_t* node) {
    if (node) cm_free(node);
}

cm_list_t* cm_list_new(void) {
    cm_list_t* list = (cm_list_t*)cm_alloc(sizeof(cm_list_t), "list");
    if (!list) return NULL;
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->gc_handle.index = 0;
    list->gc_handle.generation = 0;
    return list;
}

void cm_list_free(cm_list_t* list) {
    if (!list) return;
    cm_list_clear(list);
    cm_free(list);
}

int cm_list_append(cm_list_t* list, void* data) {
    if (!list) return -1;
    
    cm_list_node_t* node = cm_list_node_new(data);
    if (!node) return -1;
    
    if (list->tail) {
        node->prev = list->tail;
        list->tail->next = node;
        list->tail = node;
    } else {
        list->head = list->tail = node;
    }
    list->count++;
    return 0;
}

int cm_list_prepend(cm_list_t* list, void* data) {
    if (!list) return -1;
    
    cm_list_node_t* node = cm_list_node_new(data);
    if (!node) return -1;
    
    if (list->head) {
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    } else {
        list->head = list->tail = node;
    }
    list->count++;
    return 0;
}

int cm_list_remove(cm_list_t* list, void* data) {
    if (!list) return -1;
    
    cm_list_node_t* current = list->head;
    while (current) {
        if (current->data == data) {
            if (current->prev) {
                current->prev->next = current->next;
            } else {
                list->head = current->next;
            }
            if (current->next) {
                current->next->prev = current->prev;
            } else {
                list->tail = current->prev;
            }
            cm_list_node_free(current);
            list->count--;
            return 0;
        }
        current = current->next;
    }
    return -1;
}

int cm_list_remove_at(cm_list_t* list, size_t index) {
    if (!list || index >= list->count) return -1;
    
    cm_list_node_t* current = list->head;
    for (size_t i = 0; i < index && current; i++) {
        current = current->next;
    }
    
    if (!current) return -1;
    
    if (current->prev) {
        current->prev->next = current->next;
    } else {
        list->head = current->next;
    }
    if (current->next) {
        current->next->prev = current->prev;
    } else {
        list->tail = current->prev;
    }
    cm_list_node_free(current);
    list->count--;
    return 0;
}

void* cm_list_get(cm_list_t* list, size_t index) {
    if (!list || index >= list->count) {
        cm_error_set(CM_ERROR_OUT_OF_BOUNDS, "list index out of bounds");
        return NULL;
    }
    
    cm_list_node_t* current = list->head;
    for (size_t i = 0; i < index && current; i++) {
        current = current->next;
    }
    
    return current ? current->data : NULL;
}

int cm_list_set(cm_list_t* list, size_t index, void* data) {
    if (!list || index >= list->count) {
        cm_error_set(CM_ERROR_OUT_OF_BOUNDS, "list index out of bounds");
        return -1;
    }
    
    cm_list_node_t* current = list->head;
    for (size_t i = 0; i < index && current; i++) {
        current = current->next;
    }
    
    if (!current) return -1;
    current->data = data;
    return 0;
}

long cm_list_index_of(cm_list_t* list, void* data) {
    if (!list) return -1;
    
    cm_list_node_t* current = list->head;
    long index = 0;
    while (current) {
        if (current->data == data) return index;
        current = current->next;
        index++;
    }
    return -1;
}

int cm_list_contains(cm_list_t* list, void* data) {
    return cm_list_index_of(list, data) >= 0;
}

size_t cm_list_size(cm_list_t* list) {
    return list ? list->count : 0;
}

int cm_list_empty(cm_list_t* list) {
    return !list || list->count == 0;
}

void cm_list_clear(cm_list_t* list) {
    if (!list) return;
    
    cm_list_node_t* current = list->head;
    while (current) {
        cm_list_node_t* next = current->next;
        cm_list_node_free(current);
        current = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

void cm_list_foreach(cm_list_t* list, cm_list_iter_cb callback, void* userdata) {
    if (!list || !callback) return;
    
    cm_list_node_t* current = list->head;
    size_t index = 0;
    while (current) {
        callback(current->data, index++, userdata);
        current = current->next;
    }
}

void cm_list_print(cm_list_t* list, cm_list_to_string_fn to_string) {
    if (!list) {
        printf("list: null\n");
        return;
    }
    
    printf("list[%zu] { ", list->count);
    cm_list_node_t* current = list->head;
    int first = 1;
    while (current) {
        if (!first) printf(", ");
        first = 0;
        
        if (to_string && current->data) {
            cm_string_t* str = to_string(current->data);
            if (str && str->data) {
                printf("%s", str->data);
            } else {
                printf("null");
            }
            if (str) cm_string_free(str);
        } else {
            printf("%p", current->data);
        }
        current = current->next;
    }
    printf(" }\n");
}
