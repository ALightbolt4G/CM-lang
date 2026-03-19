#include "cm/packages.h"
#include "cm/file.h"
#include "cm/json.h"
#include "cm/map.h"
#include "cm/memory.h"
#include "cm/error.h"
#include "cm/cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

cm_string_t* cm_packages_expand_path(const char* path) {
    if (!path) return NULL;
    
    /* Handle tilde expansion for home directory */
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) home = getenv("USERPROFILE");
        if (home) {
            return cm_string_format("%s%s", home, path + 1);
        }
    }
    return cm_string_new(path);
}

int cm_packages_ensure_dir(const char* path) {
    if (!path) return -1;
    
#ifdef _WIN32
    return _mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
#endif
}

cm_string_t* cm_packages_get_cache_dir(void) {
    const char* home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = ".";
    
    return cm_string_format("%s/.cm/packages", home);
}

/* ============================================================================
 * Manifest Operations
 * ========================================================================== */

cm_package_manifest_t* cm_packages_manifest_new(void) {
    cm_package_manifest_t* manifest = (cm_package_manifest_t*)cm_alloc(sizeof(cm_package_manifest_t), "cm_package_manifest");
    if (!manifest) return NULL;
    
    memset(manifest, 0, sizeof(*manifest));
    manifest->dependencies = cm_map_new();
    manifest->dev_dependencies = cm_map_new();
    
    return manifest;
}

void cm_packages_manifest_free(cm_package_manifest_t* manifest) {
    if (!manifest) return;
    
    if (manifest->name) cm_string_free(manifest->name);
    if (manifest->version) cm_string_free(manifest->version);
    if (manifest->description) cm_string_free(manifest->description);
    if (manifest->main) cm_string_free(manifest->main);
    if (manifest->registry) cm_string_free(manifest->registry);
    if (manifest->dependencies) cm_map_free(manifest->dependencies);
    if (manifest->dev_dependencies) cm_map_free(manifest->dev_dependencies);
    
    cm_free(manifest);
}

cm_package_manifest_t* cm_packages_load_manifest(const char* path) {
    if (!path) return NULL;
    
    cm_string_t* content = cm_file_read(path);
    if (!content) {
        cm_error_set(CM_ERROR_IO, "Failed to read package manifest");
        return NULL;
    }
    
    struct CMJsonNode* root = cm_json_parse(content->data);
    cm_string_free(content);
    
    if (!root || root->type != CM_JSON_OBJECT) {
        cm_error_set(CM_ERROR_PARSE, "Invalid package manifest format");
        if (root) CMJsonNode_delete(root);
        return NULL;
    }
    
    cm_package_manifest_t* manifest = cm_packages_manifest_new();
    if (!manifest) {
        CMJsonNode_delete(root);
        return NULL;
    }
    
    cm_map_t* obj = root->value.object_val;
    
    /* Parse basic fields */
    struct CMJsonNode** node;
    
    node = (struct CMJsonNode**)cm_map_get(obj, "name");
    if (node && (*node)->type == CM_JSON_STRING) {
        manifest->name = cm_string_new((*node)->value.string_val->data);
    }
    
    node = (struct CMJsonNode**)cm_map_get(obj, "version");
    if (node && (*node)->type == CM_JSON_STRING) {
        manifest->version = cm_string_new((*node)->value.string_val->data);
    }
    
    node = (struct CMJsonNode**)cm_map_get(obj, "description");
    if (node && (*node)->type == CM_JSON_STRING) {
        manifest->description = cm_string_new((*node)->value.string_val->data);
    }
    
    node = (struct CMJsonNode**)cm_map_get(obj, "main");
    if (node && (*node)->type == CM_JSON_STRING) {
        manifest->main = cm_string_new((*node)->value.string_val->data);
    }
    
    node = (struct CMJsonNode**)cm_map_get(obj, "registry");
    if (node && (*node)->type == CM_JSON_STRING) {
        manifest->registry = cm_string_new((*node)->value.string_val->data);
    }
    
    /* Parse dependencies */
    node = (struct CMJsonNode**)cm_map_get(obj, "dependencies");
    if (node && (*node)->type == CM_JSON_OBJECT) {
        cm_map_t* deps = (*node)->value.object_val;
        (void)deps; /* TODO: Implement dependency parsing */
        /* Iterate through dependencies */
        /* Note: cm_map iteration would need to be implemented */
    }
    
    CMJsonNode_delete(root);
    return manifest;
}

int cm_packages_save_manifest(const char* path, cm_package_manifest_t* manifest) {
    if (!path || !manifest) return -1;
    
    cm_string_t* json = cm_string_new("{\n");
    
    /* Build JSON manually since we don't have cm_string_append_format */
    cm_string_append(json, "  \"name\": \"");
    cm_string_append(json, manifest->name ? manifest->name->data : "unknown");
    cm_string_append(json, "\",\n");
    
    cm_string_append(json, "  \"version\": \"");
    cm_string_append(json, manifest->version ? manifest->version->data : "1.0.0");
    cm_string_append(json, "\",\n");
    
    cm_string_append(json, "  \"description\": \"");
    cm_string_append(json, manifest->description ? manifest->description->data : "");
    cm_string_append(json, "\",\n");
    
    cm_string_append(json, "  \"main\": \"");
    cm_string_append(json, manifest->main ? manifest->main->data : "src/main.cm");
    cm_string_append(json, "\",\n");
    
    cm_string_append(json, "  \"registry\": \"");
    cm_string_append(json, manifest->registry ? manifest->registry->data : CM_REGISTRY_URL);
    cm_string_append(json, "\",\n");
    
    /* Dependencies */
    cm_string_append(json, "  \"dependencies\": {\n");
    cm_string_append(json, "  },\n");
    
    /* Dev dependencies */
    cm_string_append(json, "  \"devDependencies\": {\n");
    cm_string_append(json, "  }\n");
    
    cm_string_append(json, "}\n");
    
    int result = cm_file_write(path, json->data);
    cm_string_free(json);
    
    return result;
}

/* ============================================================================
 * Package Manager Context
 * ========================================================================== */

cm_package_manager_t* cm_packages_init(const char* project_root) {
    if (!project_root) return NULL;
    
    cm_package_manager_t* pm = (cm_package_manager_t*)cm_alloc(sizeof(cm_package_manager_t), "cm_package_manager");
    if (!pm) return NULL;
    
    memset(pm, 0, sizeof(*pm));
    pm->project_root = cm_string_new(project_root);
    pm->packages_dir = cm_packages_get_cache_dir();
    pm->registry_url = cm_string_new(CM_REGISTRY_URL);
    
    /* Ensure packages directory exists */
    cm_packages_ensure_dir(pm->packages_dir->data);
    
    /* Load manifest if exists */
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s", project_root, CM_PACKAGE_FILE);
    pm->manifest = cm_packages_load_manifest(manifest_path);
    
    return pm;
}

void cm_packages_free(cm_package_manager_t* pm) {
    if (!pm) return;
    
    if (pm->project_root) cm_string_free(pm->project_root);
    if (pm->packages_dir) cm_string_free(pm->packages_dir);
    if (pm->registry_url) cm_string_free(pm->registry_url);
    if (pm->manifest) cm_packages_manifest_free(pm->manifest);
    
    cm_free(pm);
}

/* ============================================================================
 * CLI Commands
 * ========================================================================== */

int cm_packages_cmd_init(const char* project_name) {
    if (!project_name) {
        fprintf(stderr, "Error: Project name required\n");
        return -1;
    }
    
    /* Create project directory */
    if (cm_packages_ensure_dir(project_name) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: Failed to create project directory\n");
        return -1;
    }
    
    /* Create subdirectories */
    char path[1024];
    snprintf(path, sizeof(path), "%s/src", project_name);
    cm_packages_ensure_dir(path);
    
    snprintf(path, sizeof(path), "%s/packages", project_name);
    cm_packages_ensure_dir(path);
    
    /* Create default manifest */
    cm_package_manifest_t* manifest = cm_packages_manifest_new();
    if (!manifest) return -1;
    
    manifest->name = cm_string_new(project_name);
    manifest->version = cm_string_new("1.0.0");
    manifest->description = cm_string_format("CM project: %s", project_name);
    manifest->main = cm_string_new("src/main.cm");
    manifest->registry = cm_string_new(CM_REGISTRY_URL);
    
    snprintf(path, sizeof(path), "%s/%s", project_name, CM_PACKAGE_FILE);
    int result = cm_packages_save_manifest(path, manifest);
    
    cm_packages_manifest_free(manifest);
    
    /* Create default main.cm */
    snprintf(path, sizeof(path), "%s/src/main.cm", project_name);
    cm_string_t* main_content = cm_string_new("// Main entry point for ");
    cm_string_append(main_content, project_name);
    cm_string_append(main_content, "\n"
        "using System;\n"
        "using Http;\n"
        "\n"
        "namespace ");
    cm_string_append(main_content, project_name);
    cm_string_append(main_content, " {\n"
        "    class Program {\n"
        "        static async Task Main() {\n"
        "            Console.WriteLine(\"Hello from ");
    cm_string_append(main_content, project_name);
    cm_string_append(main_content, "!\");\n"
        "        }\n"
        "    }\n"
        "}\n");
    
    cm_file_write(path, main_content->data);
    cm_string_free(main_content);
    
    if (result == 0) {
        printf("Initialized CM project '%s'\n", project_name);
        printf("  - cm.json created\n");
        printf("  - src/main.cm created\n");
        printf("  - packages/ directory ready\n");
    }
    
    return result;
}

int cm_packages_cmd_install(const char* package_name, const char* version) {
    if (!package_name) {
        /* Install all dependencies from manifest */
        cm_package_manager_t* pm = cm_packages_init(".");
        if (!pm) return -1;
        
        int result = cm_packages_install_all(pm);
        cm_packages_free(pm);
        return result;
    }
    
    /* Install specific package */
    cm_package_manager_t* pm = cm_packages_init(".");
    if (!pm) return -1;
    
    int result = cm_packages_install(pm, package_name, version);
    cm_packages_free(pm);
    return result;
}

/* ============================================================================
 * Package Installation
 * ========================================================================== */

int cm_packages_install(cm_package_manager_t* pm, const char* package_name, const char* version) {
    if (!pm || !package_name) return -1;
    
    printf("Installing %s", package_name);
    if (version) printf("@%s", version);
    printf("...\n");
    
    /* TODO: Implement actual package download and installation */
    /* This would involve:
     * 1. Fetching package info from registry
     * 2. Downloading package archive
     * 3. Extracting to packages directory
     * 4. Updating manifest
     * 5. Resolving and installing transitive dependencies
     */
    
    /* Placeholder for now */
    printf("Package installation not yet fully implemented\n");
    return 0;
}

int cm_packages_install_all(cm_package_manager_t* pm) {
    if (!pm || !pm->manifest) {
        fprintf(stderr, "No manifest found. Run 'cm packages init' first.\n");
        return -1;
    }
    
    printf("Installing dependencies for %s...\n", 
           pm->manifest->name ? pm->manifest->name->data : "project");
    
    /* TODO: Iterate through manifest dependencies and install each */
    
    return 0;
}

int cm_packages_cmd_remove(const char* package_name) {
    if (!package_name) {
        fprintf(stderr, "Error: Package name required\n");
        return -1;
    }
    
    printf("Removing %s...\n", package_name);
    
    /* TODO: Implement package removal */
    /* 1. Remove from manifest
     * 2. Remove from packages directory
     * 3. Check for orphaned dependencies
     */
    
    return 0;
}

int cm_packages_cmd_update(const char* package_name) {
    if (!package_name) {
        /* Update all packages */
        printf("Updating all packages...\n");
    } else {
        printf("Updating %s...\n", package_name);
    }
    
    /* TODO: Implement package update */
    /* 1. Check for updates from registry
     * 2. Update manifest versions
     * 3. Reinstall packages
     */
    
    return 0;
}

int cm_packages_cmd_list(void) {
    cm_package_manager_t* pm = cm_packages_init(".");
    if (!pm || !pm->manifest) {
        fprintf(stderr, "No manifest found.\n");
        return -1;
    }
    
    printf("Project: %s@%s\n",
           pm->manifest->name ? pm->manifest->name->data : "unknown",
           pm->manifest->version ? pm->manifest->version->data : "1.0.0");
    
    printf("\nDependencies:\n");
    /* TODO: List installed dependencies */
    
    cm_packages_free(pm);
    return 0;
}

int cm_packages_cmd_search(const char* query) {
    if (!query) {
        fprintf(stderr, "Error: Search query required\n");
        return -1;
    }
    
    printf("Searching for '%s'...\n", query);
    
    /* TODO: Implement package search via registry API */
    
    return 0;
}
