#include "cm/project.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static int cm_json_parse_simple(const char* json, const char* key, char* out, size_t out_size) {
    const char* p = strstr(json, key);
    if (!p) return -1;
    
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < out_size - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return 0;
    }
    
    return -1;
}

static int cm_file_read_all(const char* path, char** out, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0) {
        fclose(f);
        return -1;
    }
    
    *out = (char*)malloc(size + 1);
    if (!*out) {
        fclose(f);
        return -1;
    }
    
    fread(*out, 1, size, f);
    (*out)[size] = '\0';
    *out_len = size;
    
    fclose(f);
    return 0;
}

int cm_project_load_config(cm_project_config_t* config, const char* project_dir) {
    if (!config) return -1;
    
    memset(config, 0, sizeof(*config));
    strncpy(config->cm_root, project_dir, sizeof(config->cm_root) - 1);
    strncpy(config->entry, "src/main.cm", sizeof(config->entry) - 1);
    strncpy(config->output, "a.out", sizeof(config->output) - 1);
    strncpy(config->version, "1.0.0", sizeof(config->version) - 1);
    
    /* Try cm.json first */
    char path[512];
    snprintf(path, sizeof(path), "%s/cm.json", project_dir);
    
    char* content = NULL;
    size_t len = 0;
    
    if (cm_file_read_all(path, &content, &len) == 0) {
        cm_json_parse_simple(content, "name", config->name, sizeof(config->name));
        cm_json_parse_simple(content, "version", config->version, sizeof(config->version));
        cm_json_parse_simple(content, "description", config->description, sizeof(config->description));
        cm_json_parse_simple(content, "entry", config->entry, sizeof(config->entry));
        cm_json_parse_simple(content, "output", config->output, sizeof(config->output));
        free(content);
        return 0;
    }
    
    /* Fall back to package.json */
    snprintf(path, sizeof(path), "%s/package.json", project_dir);
    if (cm_file_read_all(path, &content, &len) == 0) {
        /* Try cm.name first, then name */
        if (cm_json_parse_simple(content, "cm.name", config->name, sizeof(config->name)) != 0) {
            cm_json_parse_simple(content, "name", config->name, sizeof(config->name));
        }
        cm_json_parse_simple(content, "version", config->version, sizeof(config->version));
        cm_json_parse_simple(content, "description", config->description, sizeof(config->description));
        free(content);
        return 0;
    }
    
    /* Use directory name as fallback */
    const char* last_sep = strrchr(project_dir, '/');
    if (!last_sep) last_sep = strrchr(project_dir, '\\');
    if (last_sep) {
        strncpy(config->name, last_sep + 1, sizeof(config->name) - 1);
    } else {
        strncpy(config->name, project_dir, sizeof(config->name) - 1);
    }
    
    return -1;
}

const char* cm_project_get_name(cm_project_config_t* config) {
    if (!config || !config->name[0]) return "unknown";
    return config->name;
}

const char* cm_project_get_entry(cm_project_config_t* config) {
    if (!config || !config->entry[0]) return "src/main.cm";
    return config->entry;
}

const char* cm_project_get_output(cm_project_config_t* config) {
    if (!config || !config->output[0]) return "a.out";
    return config->output;
}

int cm_project_detect(const char* path) {
    char test_path[512];
    snprintf(test_path, sizeof(test_path), "%s/cm.json", path);
    
    FILE* f = fopen(test_path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    
    snprintf(test_path, sizeof(test_path), "%s/package.json", path);
    f = fopen(test_path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    
    return 0;
}

cm_string_t* cm_project_generate_cm_json(const char* name, const char* description) {
    cm_string_t* json = cm_string_new("{\n");
    cm_string_append(json, "  \"name\": \"");
    cm_string_append(json, name);
    cm_string_append(json, "\",\n");
    cm_string_append(json, "  \"version\": \"1.0.0\",\n");
    cm_string_append(json, "  \"description\": \"");
    cm_string_append(json, description ? description : "A CM project");
    cm_string_append(json, "\",\n");
    cm_string_append(json, "  \"entry\": \"src/main.cm\",\n");
    cm_string_append(json, "  \"output\": \"dist/app.exe\",\n");
    cm_string_append(json, "  \"dependencies\": {\n");
    cm_string_append(json, "    \"cm-core\": \"^5.0.0\"\n");
    cm_string_append(json, "  }\n");
    cm_string_append(json, "}\n");
    return json;
}

cm_string_t* cm_project_generate_gitignore(void) {
    cm_string_t* gitignore = cm_string_new(
        "# CM build outputs\n"
        "*.exe\n"
        "*.out\n"
        "a.out\n"
        "cm_out.c\n"
        "dist/\n"
        "build/\n"
        "\n"
        "# CM cache\n"
        ".cache/\n"
        ".cm/\n"
        "\n"
        "# IDE\n"
        ".vscode/settings.json\n"
        ".idea/\n"
        "\n"
        "# OS\n"
        ".DS_Store\n"
        "Thumbs.db\n"
        "*.tmp\n"
    );
    return gitignore;
}

cm_string_t* cm_project_generate_readme(const char* name) {
    cm_string_t* readme = cm_string_new("# ");
    cm_string_append(readme, name);
    cm_string_append(readme, "\n\n");
    cm_string_append(readme, "A CM (C Managed) project with C#-like syntax.\n\n");
    cm_string_append(readme, "## Getting Started\n\n");
    cm_string_append(readme, "### Build\n");
    cm_string_append(readme, "```bash\n");
    cm_string_append(readme, "cm build src/main.cm\n");
    cm_string_append(readme, "```\n\n");
    cm_string_append(readme, "### Run\n");
    cm_string_append(readme, "```bash\n");
    cm_string_append(readme, "cm run src/main.cm\n");
    cm_string_append(readme, "```\n\n");
    cm_string_append(readme, "## Project Structure\n\n");
    cm_string_append(readme, "```\n");
    cm_string_append(readme, "src/\n");
    cm_string_append(readme, "  main.cm          # Entry point\n");
    cm_string_append(readme, "  models/            # Data models\n");
    cm_string_append(readme, "  services/          # Business logic\n");
    cm_string_append(readme, "tests/               # Test files\n");
    cm_string_append(readme, "public_html/         # Web assets\n");
    cm_string_append(readme, "```\n");
    return readme;
}
