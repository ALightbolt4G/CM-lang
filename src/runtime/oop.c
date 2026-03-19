#include "cm/oop.h"
#include "cm/string.h"
#include "cm/memory.h"
#include <string.h>
#include <stdlib.h>

/* Helper for cm_strdup if not in string.h */
static char* cm_strdup_local(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* d = (char*)cm_alloc(len, "string");
    if (d) memcpy(d, s, len);
    return d;
}

cm_class_t* cm_class_new(const char* name, cm_class_t* parent) {
    if (!name) return NULL;
    
    cm_class_t* class = (cm_class_t*)cm_alloc(sizeof(cm_class_t), "class");
    if (!class) return NULL;
    
    class->name = cm_strdup_local(name);
    class->parent = parent;
    class->methods = cm_map_new();
    class->fields = cm_map_new();
    class->gc_handle.index = 0;
    class->gc_handle.generation = 0;
    
    if (!class->name || !class->methods || !class->fields) {
        cm_class_free(class);
        return NULL;
    }
    
    /* Inherit parent's fields and methods */
    if (parent) {
        /* Copy parent fields */
        cm_map_foreach(parent->fields, key, value) {
            cm_map_set(class->fields, key, value, 0);
        }
        /* Copy parent methods */
        cm_map_foreach(parent->methods, key, value) {
            cm_map_set(class->methods, key, value, 0);
        }
    }
    
    return class;
}

void cm_class_free(cm_class_t* class) {
    if (!class) return;
    if (class->name) cm_free(class->name);
    if (class->methods) cm_map_free(class->methods);
    if (class->fields) cm_map_free(class->fields);
    cm_free(class);
}

int cm_class_add_method(cm_class_t* class, const char* name, cm_method_fn fn) {
    if (!class || !name || !fn) return -1;
    
    /* Safely convert function pointer to void* using a union to satisfy -Wpedantic */
    union {
        cm_method_fn fn;
        const void* ptr;
    } cast;
    cast.fn = fn;
    
    cm_map_set(class->methods, name, cast.ptr, 0);
    return 0;
}

int cm_class_add_field(cm_class_t* class, const char* name, void* default_value) {
    if (!class || !name) return -1;
    cm_map_set(class->fields, name, default_value, 0);
    return 0;
}

int cm_class_has_method(cm_class_t* class, const char* name) {
    if (!class || !name) return 0;
    return cm_map_get(class->methods, name) != NULL;
}

int cm_class_has_field(cm_class_t* class, const char* name) {
    if (!class || !name) return 0;
    return cm_map_get(class->fields, name) != NULL;
}

cm_object_t* cm_object_new(cm_class_t* class) {
    if (!class) return NULL;
    
    cm_object_t* obj = (cm_object_t*)cm_alloc(sizeof(struct cm_object), "object");
    if (!obj) return NULL;
    
    obj->class = class;
    obj->fields = cm_map_new();
    obj->gc_handle.index = 0;
    obj->gc_handle.generation = 0;
    
    if (!obj->fields) {
        cm_object_free(obj);
        return NULL;
    }
    
    /* Initialize fields with defaults from class */
    cm_map_foreach(class->fields, key, value) {
        cm_map_set(obj->fields, key, value, 0);
    }
    
    return obj;
}

void cm_object_free(cm_object_t* obj) {
    if (!obj) return;
    if (obj->fields) cm_map_free(obj->fields);
    cm_free(obj);
}

void* cm_object_call(cm_object_t* obj, const char* name, void** args, size_t arg_count) {
    if (!obj || !name) return NULL;
    
    void* ptr = cm_map_get(obj->class->methods, name);
    if (!ptr) return NULL;
    
    /* Safely convert void* back to function pointer */
    union {
        void* ptr;
        cm_method_fn fn;
    } cast;
    cast.ptr = ptr;
    
    return cast.fn(obj, args, arg_count);
}

void* cm_object_get_field(cm_object_t* obj, const char* name) {
    if (!obj || !name) return NULL;
    return cm_map_get(obj->fields, name);
}

int cm_object_set_field(cm_object_t* obj, const char* name, void* value) {
    if (!obj || !name) return -1;
    /* Check if field exists in class */
    if (!cm_class_has_field(obj->class, name)) return -1;
    cm_map_set(obj->fields, name, value, 0);
    return 0;
}

int cm_object_is_instance(cm_object_t* obj, cm_class_t* class) {
    if (!obj || !class) return 0;
    
    cm_class_t* current = obj->class;
    while (current) {
        if (current == class) return 1;
        current = current->parent;
    }
    return 0;
}
