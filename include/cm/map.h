/**
 * @file map.h
 * @brief Hash map structures mapping securely.
 */
#ifndef CM_MAP_H
#define CM_MAP_H

#include "core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cm_map_entry {
    char* key;
    void* value;
    size_t value_size;
    uint32_t hash;
    struct cm_map_entry* next;
} cm_map_entry_t;

struct cm_map {
    cm_map_entry_t** buckets;
    int bucket_count;
    int size;
    float load_factor;
    int growth_factor;
};

/**
 * @brief allocate structural hash base natively.
 */
cm_map_t* cm_map_new(void);

/**
 * @brief resolve dictionary bounds naturally completely.
 */
void cm_map_free(cm_map_t* map);

/**
 * @brief map values internally parsing boundaries aggressively intuitively.
 */
void cm_map_set(cm_map_t* map, const char* key, const void* value, size_t value_size);

/**
 * @brief extract buckets inherently searching mappings sequentially.
 */
void* cm_map_get(cm_map_t* map, const char* key);

/**
 * @brief probe available hash links safely explicitly.
 */
int cm_map_has(cm_map_t* map, const char* key);

/**
 * @brief fetches structural capacities implicitly safely.
 */
size_t cm_map_size(cm_map_t* map);

/**
 * @brief serialize string maps to JSON.
 */
int cm_map_save_to_json(struct cm_map* map, const char* filepath);

/**
 * @brief deserialize map from JSON file natively.
 */
struct cm_map* cm_map_load_from_json(const char* filepath);

#ifdef __cplusplus
}
#endif

/**
 * @brief Macro for iterating over map entries.
 */
#define cm_map_foreach(map, key_var, value_var) \
    for (int _i = 0; _i < (map)->bucket_count; _i++) \
        for (cm_map_entry_t* _e = (map)->buckets[_i]; _e; _e = _e->next) \
            for (const char* key_var = _e->key; key_var; key_var = NULL) \
                for (void* value_var = _e->value; value_var; value_var = NULL)

#endif /* CM_MAP_H */
