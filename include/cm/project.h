#ifndef CM_PROJECT_H
#define CM_PROJECT_H

#include "cm/core.h"
#include "cm/string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Project Configuration API
 * Handles cm.json and package.json parsing for project metadata
 * ========================================================================== */

typedef struct {
    char name[128];
    char version[32];
    char description[256];
    char entry[256];
    char output[256];
    char cm_root[512];
} cm_project_config_t;

/* Load project configuration from cm.json or package.json
 * Returns 0 on success, -1 if no config found
 */
int cm_project_load_config(cm_project_config_t* config, const char* project_dir);

/* Get project name from config or directory name
 * Returns pointer to name (owned by config), never NULL
 */
const char* cm_project_get_name(cm_project_config_t* config);

/* Get entry point from config or default
 * Returns pointer to entry path, defaults to "src/main.cm"
 */
const char* cm_project_get_entry(cm_project_config_t* config);

/* Get output path from config or default
 * Returns pointer to output path, defaults to "a.out"
 */
const char* cm_project_get_output(cm_project_config_t* config);

/* Check if running in a project directory
 * Returns 1 if cm.json or package.json found, 0 otherwise
 */
int cm_project_detect(const char* path);

/* Generate default cm.json content
 * Returns newly allocated string (caller must free)
 */
cm_string_t* cm_project_generate_cm_json(const char* name, const char* description);

/* Generate default .gitignore content for CM projects
 * Returns newly allocated string (caller must free)
 */
cm_string_t* cm_project_generate_gitignore(void);

/* Generate README.md template
 * Returns newly allocated string (caller must free)
 */
cm_string_t* cm_project_generate_readme(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* CM_PROJECT_H */
