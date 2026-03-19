#include "cm/cm_lang.h"
#include "cm/memory.h"
#include "cm/error.h"
#include "cm/cmd.h"
#include "cm/file.h"
#include "cm_codegen.h"
#include "cm/compiler/ast_v2.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#else
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#endif

/* ============================================================================
 * Parser
 * ==========================================================================*/



/* Legacy parser has been replaced by v2. */

/* ============================================================================
 * Directory / module loading helpers
 * ==========================================================================*/

static int cm_is_directory(const char* path) {
#ifndef _WIN32
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
#else
    struct _stat st;
    if (_stat(path, &st) != 0) return 0;
    return (st.st_mode & _S_IFDIR) != 0;
#endif
}

typedef struct cm_module_list {
    cm_string_t** items;
    size_t        count;
    size_t        capacity;
} cm_module_list_t;

static void cm_module_list_init(cm_module_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void cm_module_list_append(cm_module_list_t* list, const char* path) {
    if (!list || !path) return;
    if (list->count == list->capacity) {
        size_t new_cap = list->capacity ? list->capacity * 2 : 8;
        cm_string_t** tmp = (cm_string_t**)cm_alloc(new_cap * sizeof(cm_string_t*), "cm_module_list");
        if (!tmp) return;
        if (list->items && list->count) {
            memcpy(tmp, list->items, list->count * sizeof(cm_string_t*));
        }
        list->items = tmp;
        list->capacity = new_cap;
    }
    list->items[list->count++] = cm_string_new(path);
}

static void cm_module_list_free(cm_module_list_t* list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; ++i) {
        if (list->items[i]) cm_string_free(list->items[i]);
    }
    if (list->items) cm_free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

static void cm_scan_directory_for_cm(const char* dir, cm_module_list_t* out) {
    if (!dir || !out) return;
#ifndef _WIN32
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        size_t len = strlen(name);
        if (len > 3 && strcmp(name + len - 3, ".cm") == 0) {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", dir, name);
            cm_module_list_append(out, full);
        }
    }
    closedir(d);
#else
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.cm", dir);
    struct _finddata_t data;
    intptr_t handle = _findfirst(pattern, &data);
    if (handle == -1L) return;
    do {
        char full[1024];
        snprintf(full, sizeof(full), "%s\\%s", dir, data.name);
        cm_module_list_append(out, full);
    } while (_findnext(handle, &data) == 0);
    _findclose(handle);
#endif
}

/* ============================================================================
 * Transpiler / Codegen: CM AST -> Hardened C
 * ==========================================================================*/

/* Legacy blacklist checks removed */

/* ============================================================================
 * High-level compile pipeline
 * ==========================================================================*/

static char* cm_read_file_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)sz, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

static int cm_write_text_file(const char* path, const char* text) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    if (text) {
        fwrite(text, 1, strlen(text), f);
    }
    fclose(f);
    return 0;
}

static void cm_try_remove_output(const char* output_exe) {
    if (!output_exe || !output_exe[0]) return;
    remove(output_exe);
#ifdef _WIN32
    /* MinGW produces .exe when -o is basename */
    char buf[512];
    snprintf(buf, sizeof(buf), "%s.exe", output_exe);
    remove(buf);
#endif
}

/* Removed legacy cm_collect_require_modules */

static int cm_invoke_system_compiler(const char* c_path, const char* output_exe) {
    if (!c_path || !output_exe) return -1;

    /* Enterprise default: compile generated C together with the CM runtime sources,
       so `cm main.cm app` works even without a pre-built libcm present. */
    const char* candidates[] = { "gcc", "clang", "cc", NULL };
    const char* cc = NULL;

    for (int i = 0; candidates[i]; ++i) {
        cm_cmd_t* probe = cm_cmd_new(candidates[i]);
        if (!probe) continue;
        cm_cmd_arg(probe, "--version");
        cm_cmd_result_t* r = cm_cmd_run(probe);
        cm_cmd_free(probe);
        if (r && r->exit_code == 0) {
            cc = candidates[i];
            cm_cmd_result_free(r);
            break;
        }
        if (r) cm_cmd_result_free(r);
    }

    if (!cc) {
        cm_error_set(CM_ERROR_IO, "no C compiler found (expected gcc/clang/cc in PATH)");
        return -1;
    }

    cm_cmd_t* cmd = cm_cmd_new(cc);
    if (!cmd) return -1;

    cm_cmd_arg(cmd, c_path);
    cm_cmd_arg(cmd, "src/runtime/memory.c");
    cm_cmd_arg(cmd, "src/runtime/error.c");
    cm_cmd_arg(cmd, "src/runtime/error_detail.c");
    cm_cmd_arg(cmd, "src/runtime/string.c");
    cm_cmd_arg(cmd, "src/runtime/array.c");
    cm_cmd_arg(cmd, "src/runtime/map.c");
    cm_cmd_arg(cmd, "src/runtime/json.c");
    cm_cmd_arg(cmd, "src/runtime/http.c");
    cm_cmd_arg(cmd, "src/runtime/file.c");
    cm_cmd_arg(cmd, "src/runtime/thread.c");
    cm_cmd_arg(cmd, "src/runtime/core.c");

    cm_cmd_arg(cmd, "-Iinclude");
    cm_cmd_arg(cmd, "-o");
    cm_cmd_arg(cmd, output_exe);
    cm_cmd_arg(cmd, "-Wall");
    cm_cmd_arg(cmd, "-Wextra");

#ifdef _WIN32
    cm_cmd_arg(cmd, "-lws2_32");
#endif

    cm_cmd_result_t* res = cm_cmd_run(cmd);
    int exit_code = res ? res->exit_code : -1;
    if (exit_code != 0) {
        const char* msg = res && res->stderr_output
            ? res->stderr_output->data
            : "system compiler error";
        cm_error_set(CM_ERROR_IO, msg);
    }
    if (res) cm_cmd_result_free(res);
    cm_cmd_free(cmd);
    return exit_code;
}

extern cm_string_t* cm_codegen_v2_to_c(const cm_ast_v2_list_t* ast);

static void cm_check_blacklist_ast_v2(cm_ast_v2_list_t* ast) {
    (void)ast;
    /* TODO: Implement comprehensive blacklist checks for v2 AST */
}

static int cm_collect_require_modules_v2(cm_ast_v2_list_t* ast, cm_module_list_t* modules) {
    if (!ast || !modules) return 0;
    for (cm_ast_v2_node_t* n = ast->head; n; n = n->next) {
        if (n->kind != CM_AST_V2_IMPORT) continue;
        if (n->as.let_decl.init && n->as.let_decl.init->kind == CM_AST_V2_STRING_LITERAL) {
            const char* path = n->as.let_decl.init->as.string_literal.value->data;
            if (cm_is_directory(path)) {
                cm_scan_directory_for_cm(path, modules);
                continue;
            }

            cm_module_list_append(modules, path);

            char buf[1024];
            snprintf(buf, sizeof(buf), "src/%s", path);
            cm_module_list_append(modules, buf);
            snprintf(buf, sizeof(buf), "include/cm/%s", path);
            cm_module_list_append(modules, buf);
        }
    }
    return 0;
}

int cm_compile_file(const char* entry_path, const char* output_exe) {
    if (!entry_path || !output_exe) return -1;

    char* src = cm_read_file_all(entry_path);
    if (!src) {
        cm_error_set(CM_ERROR_IO, "failed to read entry .cm file");
        return -1;
    }

    cm_ast_v2_list_t ast = {0};
    CM_TRY() {
        ast = cm_parse_v2(src);
        if (cm_error_get_last() == CM_ERROR_PARSE) {
            CM_THROW(CM_ERROR_PARSE, cm_error_get_message());
        }
        cm_check_blacklist_ast_v2(&ast);
    } CM_CATCH() {
        free(src);
        cm_ast_v2_free_list(&ast);
        return -1;
    }

    cm_module_list_t modules;
    cm_module_list_init(&modules);
    cm_collect_require_modules_v2(&ast, &modules);

    cm_string_t* c_code = cm_codegen_v2_to_c(&ast);

    const char* c_path = "cm_out.c";
    if (cm_write_text_file(c_path, c_code->data) != 0) {
        cm_string_free(c_code);
        cm_module_list_free(&modules);
        cm_ast_v2_free_list(&ast);
        free(src);
        cm_error_set(CM_ERROR_IO, "failed to write intermediate C file");
        return -1;
    }

    cm_try_remove_output(output_exe);
    int rc = cm_invoke_system_compiler(c_path, output_exe);

    cm_string_free(c_code);
    cm_module_list_free(&modules);
    cm_ast_v2_free_list(&ast);
    free(src);

    return rc;
}

int cm_emit_c_file(const char* entry_path, const char* output_c_path) {
    if (!entry_path || !output_c_path) return -1;

    char* src = cm_read_file_all(entry_path);
    if (!src) {
        cm_error_set(CM_ERROR_IO, "failed to read entry .cm file");
        return -1;
    }

    cm_ast_v2_list_t ast = {0};
    CM_TRY() {
        ast = cm_parse_v2(src);
        if (cm_error_get_last() == CM_ERROR_PARSE) {
            CM_THROW(CM_ERROR_PARSE, cm_error_get_message());
        }
        cm_check_blacklist_ast_v2(&ast);
    } CM_CATCH() {
        free(src);
        cm_ast_v2_free_list(&ast);
        return -1;
    }

    cm_string_t* c_code = cm_codegen_v2_to_c(&ast);
    if (cm_write_text_file(output_c_path, c_code->data) != 0) {
        cm_string_free(c_code);
        cm_ast_v2_free_list(&ast);
        free(src);
        cm_error_set(CM_ERROR_IO, "failed to write generated C file");
        return -1;
    }

    cm_string_free(c_code);
    cm_ast_v2_free_list(&ast);
    free(src);
    return 0;
}

