#ifndef CM_PACKAGES_H
#define CM_PACKAGES_H

#include "cm/core.h"
#include "cm/string.h"

/* ============================================================================
 * CM Package Manager
 * A modern package manager for CM language (similar to npm/cargo)
 * ========================================================================== */

#define CM_PACKAGES_DIR "~/.cm/packages"
#define CM_REGISTRY_URL "https://packages.cm-lang.org"
#define CM_PACKAGE_FILE "cm.json"

/* Package manifest structure */
typedef struct {
    cm_string_t* name;
    cm_string_t* version;
    cm_string_t* description;
    cm_string_t* main;
    cm_map_t* dependencies;      /* name -> version constraint */
    cm_map_t* dev_dependencies;   /* name -> version constraint */
    cm_string_t* registry;
} cm_package_manifest_t;

/* Package installation result */
typedef struct {
    int success;
    cm_string_t* package_name;
    cm_string_t* version;
    cm_string_t* install_path;
    cm_string_t* error_message;
} cm_package_result_t;

/* Package manager context */
typedef struct {
    cm_string_t* project_root;
    cm_string_t* packages_dir;
    cm_string_t* registry_url;
    cm_package_manifest_t* manifest;
} cm_package_manager_t;

/* Initialize package manager */
cm_package_manager_t* cm_packages_init(const char* project_root);
void cm_packages_free(cm_package_manager_t* pm);

/* Manifest operations */
cm_package_manifest_t* cm_packages_load_manifest(const char* path);
int cm_packages_save_manifest(const char* path, cm_package_manifest_t* manifest);
void cm_packages_manifest_free(cm_package_manifest_t* manifest);

/* Package operations */
int cm_packages_install(cm_package_manager_t* pm, const char* package_name, const char* version);
int cm_packages_install_all(cm_package_manager_t* pm);
int cm_packages_remove(cm_package_manager_t* pm, const char* package_name);
int cm_packages_update(cm_package_manager_t* pm, const char* package_name);

/* Registry operations */
cm_string_t* cm_packages_fetch_package_info(const char* registry_url, const char* package_name);
cm_string_t* cm_packages_download_package(const char* registry_url, const char* package_name, 
                                           const char* version, const char* dest_path);

/* Dependency resolution */
cm_map_t* cm_packages_resolve_dependencies(cm_package_manager_t* pm);
int cm_packages_check_conflicts(cm_map_t* resolved_deps);

/* CLI commands */
int cm_packages_cmd_init(const char* project_name);
int cm_packages_cmd_install(const char* package_name, const char* version);
int cm_packages_cmd_remove(const char* package_name);
int cm_packages_cmd_update(const char* package_name);
int cm_packages_cmd_list(void);
int cm_packages_cmd_search(const char* query);

/* Utility functions */
cm_string_t* cm_packages_get_cache_dir(void);
cm_string_t* cm_packages_expand_path(const char* path);
int cm_packages_ensure_dir(const char* path);
cm_string_t* cm_packages_hash_file(const char* path);

#endif /* CM_PACKAGES_H */
