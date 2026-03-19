/**
 * @file oop.h
 * @brief Object-oriented programming support for CM.
 *
 * Simple OOP system with classes, objects, methods, and inheritance.
 * All memory is managed by the CM garbage collector.
 */
#ifndef CM_OOP_H
#define CM_OOP_H

#include "core.h"
#include "memory.h"
#include "map.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Method function type.
 */
typedef void* (*cm_method_fn)(void* self, void** args, size_t arg_count);

/**
 * @brief Class definition.
 */
typedef struct cm_class {
    char* name;
    struct cm_class* parent;
    cm_map_t* methods;      /* name -> cm_method_fn */
    cm_map_t* fields;       /* name -> default value */
    cm_ptr_t gc_handle;
} cm_class_t;

/**
 * @brief Object instance.
 * Mapping to the opaque cm_object_t defined in core.h
 */
struct cm_object {
    cm_class_t* class;
    cm_map_t* fields;       /* instance field values */
    cm_ptr_t gc_handle;
};

/**
 * @brief Create a new class.
 * @param name Class name.
 * @param parent Parent class (NULL for none).
 * @return New class or NULL on error.
 */
cm_class_t* cm_class_new(const char* name, cm_class_t* parent);

/**
 * @brief Free a class.
 * @param class The class to free.
 */
void cm_class_free(cm_class_t* class);

/**
 * @brief Add method to class.
 * @param class The class.
 * @param name Method name.
 * @param fn Method function.
 * @return 0 on success, -1 on error.
 */
int cm_class_add_method(cm_class_t* class, const char* name, cm_method_fn fn);

/**
 * @brief Add field to class.
 * @param class The class.
 * @param name Field name.
 * @param default_value Default value (can be NULL).
 * @return 0 on success, -1 on error.
 */
int cm_class_add_field(cm_class_t* class, const char* name, void* default_value);

/**
 * @brief Check if class has method.
 * @param class The class.
 * @param name Method name.
 * @return 1 if found, 0 otherwise.
 */
int cm_class_has_method(cm_class_t* class, const char* name);

/**
 * @brief Check if class has field.
 * @param class The class.
 * @param name Field name.
 * @return 1 if found, 0 otherwise.
 */
int cm_class_has_field(cm_class_t* class, const char* name);

/**
 * @brief Create new object instance.
 * @param class The class to instantiate.
 * @return New object or NULL on error.
 */
cm_object_t* cm_object_new(cm_class_t* class);

/**
 * @brief Free an object.
 * @param obj The object.
 */
void cm_object_free(cm_object_t* obj);

/**
 * @brief Call method on object.
 * @param obj The object.
 * @param name Method name.
 * @param args Arguments array.
 * @param arg_count Number of arguments.
 * @return Method return value or NULL.
 */
void* cm_object_call(cm_object_t* obj, const char* name, void** args, size_t arg_count);

/**
 * @brief Get field value.
 * @param obj The object.
 * @param name Field name.
 * @return Field value or NULL.
 */
void* cm_object_get_field(cm_object_t* obj, const char* name);

/**
 * @brief Set field value.
 * @param obj The object.
 * @param name Field name.
 * @param value New value.
 * @return 0 on success, -1 on error.
 */
int cm_object_set_field(cm_object_t* obj, const char* name, void* value);

/**
 * @brief Check if object is instance of class.
 * @param obj The object.
 * @param class The class to check.
 * @return 1 if instance, 0 otherwise.
 */
int cm_object_is_instance(cm_object_t* obj, cm_class_t* class);

#ifdef __cplusplus
}
#endif

#endif /* CM_OOP_H */
