#include "cm/core.h"
#include "cm/error.h"
#include "cm/memory.h"
#include "cm/cm_lang.h"
#include "cm/cm_highlight.h"
#include "cm/cmd.h"
#include "cm/project.h"
#include "cm/packages.h"
#include "cm/compiler/lexer.h"
#include "cm/compiler/ast_v2.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#define cm_mkdir(dir) mkdir((dir), 0755)
#define cm_access access
#define CM_DEFAULT_OUT "a.out"
#else
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#define cm_mkdir(dir) _mkdir(dir)
#define cm_access _access
#define CM_DEFAULT_OUT "a.exe"
#endif

static void cm_print_usage(void) {
    fprintf(stderr,
        "CM Language v2.0.0 - Modern Systems Programming\n"
        "Usage:\n"
        "  cm init   <project-name>            Create new CM v2 project\n"
        "  cm build  [entry.cm] [-o output]    Build CM v2 program\n"
        "  cm run    [entry.cm] [-o output]    Build and run CM program\n"
        "  cm check  [file.cm]                 Type check only (fast)\n"
        "  cm doctor [project-dir]             Diagnose project health\n"
        "  cm test                             Run project tests\n"
        "  cm fmt    [file.cm]                 Format source files\n"
        "  cm install [-o path]                Install binary to system\n"
        "  cm emitc  <entry.cm> [-o output.c]  Emit C code only\n"
        "\n"
        "Legacy (v1) commands:\n"
        "  cm highlight <file.cm>              Syntax highlight CM file\n"
        "\n"
        "Package Manager:\n"
        "  cm packages init [<name>]           Initialize new project\n"
        "  cm packages install [name@version]  Install package(s)\n"
        "  cm packages remove <name>           Remove package\n"
        "  cm packages update [name]           Update package(s)\n"
        "  cm packages list                    List installed packages\n"
        "  cm packages search <query>          Search registry\n"
        "\n"
        "Options:\n"
        "  -o <path>                           Specify output path\n"
        "  --emit-c                            Emit C code, don't compile\n"
        "  -v, --version                       Show version\n"
    );
}

static int cm_use_colors(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdout));
#else
    return isatty(fileno(stdout));
#endif
}

static void cm_print_neon_header(const char* project_name) {
    int colors = cm_use_colors();
    const char* cyan = colors ? "\x1b[36m" : "";
    const char* bold = colors ? "\x1b[1m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    
    printf("\n%s⚡ %sCM Language v2.0.0%s\n", bold, cyan, reset);
    printf("  %s═══════════════════════%s\n", cyan, reset);
    if (project_name && project_name[0]) {
        printf("  📂 Project: %s%s%s\n", bold, project_name, reset);
    }
}

static void cm_print_neon_progress(int percent) {
    int colors = cm_use_colors();
    const char* green = colors ? "\x1b[32m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    
    int filled = percent / 10;
    printf("  🔨 Building... [");
    for (int i = 0; i < 10; i++) {
        if (i < filled) {
            printf("%s█%s", green, reset);
        } else {
            printf("░");
        }
    }
    printf("] %d%%\n", percent);
}

static void cm_print_neon_success(const char* output, double build_time) {
    int colors = cm_use_colors();
    const char* green = colors ? "\x1b[32m" : "";
    const char* yellow = colors ? "\x1b[33m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    const char* bold = colors ? "\x1b[1m" : "";
    
    /* Get file size */
    long size = 0;
    FILE* f = fopen(output, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    
    const char* size_str = "";
    char size_buf[32];
    if (size > 0) {
        if (size < 1024) {
            snprintf(size_buf, sizeof(size_buf), " (%ldB)", size);
        } else if (size < 1024 * 1024) {
            snprintf(size_buf, sizeof(size_buf), " (%.1fKB)", size / 1024.0);
        } else {
            snprintf(size_buf, sizeof(size_buf), " (%.1fMB)", size / (1024.0 * 1024.0));
        }
        size_str = size_buf;
    }
    
    printf("  %s✅ Success:%s %s%s%s generated%s\n", green, reset, bold, output, reset, size_str);
    if (build_time > 0) {
        printf("  ⏱️  Build time: %s%.2fs%s\n", yellow, build_time, reset);
    }
    printf("  ✨ Happy coding!\n\n");
}

static void cm_print_neon_error(const char* message) {
    int colors = cm_use_colors();
    const char* red = colors ? "\x1b[31m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    const char* bold = colors ? "\x1b[1m" : "";
    
    printf("\n  %s❌ Error:%s %s%s%s\n\n", bold, reset, red, message, reset);
}

static double cm_get_time(void) {
    return (double)clock() / CLOCKS_PER_SEC;
}

/* ============================================================================
 * cm doctor — comprehensive project health checks
 *
 * Checks (Rust-safety-inspired: catch problems early before they cause crashes):
 *   1. cm.json / cm_config.json config file present & readable
 *   2. src/ directory + at least one .cm source file exists
 *   3. A C compiler (gcc or clang) is in PATH
 *   4. CM runtime header (cm/core.h) is accessible from include/
 *   5. Each .cm file lexes & parses without errors (v2 pipeline)
 *   6. Raw malloc/free without gc namespace detected (memory safety warning)
 * ==========================================================================*/

/* Helper: try to find a command in PATH by attempting a version flag */
static int cm_doctor_cmd_exists(const char* cmd) {
    char buf[256];
#ifdef _WIN32
    snprintf(buf, sizeof(buf), "where %s >NUL 2>&1", cmd);
#else
    snprintf(buf, sizeof(buf), "which %s >/dev/null 2>&1", cmd);
#endif
    return system(buf) == 0;
}

/* Helper: read entire file into a newly allocated buffer (caller must free) */
static char* cm_doctor_read_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (out_len) *out_len = rd;
    return buf;
}

typedef struct {
    int colors;
    const char* green;
    const char* yellow;
    const char* red;
    const char* cyan;
    const char* bold;
    const char* reset;
} cm_doctor_theme_t;

static cm_doctor_theme_t cm_doctor_theme(void) {
    cm_doctor_theme_t t;
    t.colors = cm_use_colors();
    t.green  = t.colors ? "\x1b[32m" : "";
    t.yellow = t.colors ? "\x1b[33m" : "";
    t.red    = t.colors ? "\x1b[31m" : "";
    t.cyan   = t.colors ? "\x1b[36m" : "";
    t.bold   = t.colors ? "\x1b[1m"  : "";
    t.reset  = t.colors ? "\x1b[0m"  : "";
    return t;
}

#define DR_OK(fmt, ...)  printf("  %s✅%s " fmt "\n", th.green,  th.reset, ##__VA_ARGS__)
#define DR_WARN(fmt, ...) printf("  %s⚠️ %s " fmt "\n", th.yellow, th.reset, ##__VA_ARGS__)
#define DR_FAIL(fmt, ...) printf("  %s❌%s " fmt "\n", th.red,    th.reset, ##__VA_ARGS__); issues++
#define DR_INFO(fmt, ...) printf("  %s🔍%s " fmt "\n", th.cyan,   th.reset, ##__VA_ARGS__)

/* Scan a CM source string for bare malloc/free (memory safety check) */
static int cm_doctor_has_raw_malloc(const char* src) {
    /* Look for malloc( or free( NOT preceded by cm_ or gc. */
    const char* p = src;
    while ((p = strstr(p, "malloc(")) != NULL) {
        /* Check the 4 chars before 'malloc' */
        if (p >= src + 4 &&
            strncmp(p - 4, "cm_",  3) != 0 &&
            strncmp(p - 3, "gc.",  3) != 0) {
            return 1;
        }
        p++;
    }
    return 0;
}
static int cm_doctor_has_raw_free(const char* src) {
    const char* p = src;
    while ((p = strstr(p, "free(")) != NULL) {
        if (p >= src + 4 &&
            strncmp(p - 4, "cm_",  3) != 0 &&
            strncmp(p - 3, "gc.",  3) != 0) {
            return 1;
        }
        p++;
    }
    return 0;
}

static int cm_doctor(const char* project_dir) {
    cm_doctor_theme_t th = cm_doctor_theme();
    int issues = 0;
    char path[512];

    printf("\n%s⚕️  CM Doctor — Project Health Report%s\n", th.bold, th.reset);
    printf("  %s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", th.cyan, th.reset);
    printf("  Scanning: %s%s%s\n\n", th.bold, project_dir, th.reset);

    /* ── Check 1: Config file ──────────────────────────────────────────── */
    snprintf(path, sizeof(path), "%s/cm.json", project_dir);
    FILE* cfg = fopen(path, "r");
    if (!cfg) {
        snprintf(path, sizeof(path), "%s/cm_config.json", project_dir);
        cfg = fopen(path, "r");
    }
    if (cfg) {
        /* Quick JSON validity: look for opening { */
        int ch = fgetc(cfg);
        while (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') ch = fgetc(cfg);
        if (ch == '{') {
            DR_OK("Config file found and looks valid (%s)", path);
        } else {
            DR_FAIL("Config file exists but may be malformed (no opening '{') — %s", path);
        }
        fclose(cfg);
    } else {
        DR_WARN("No cm.json found in %s (run 'cm init <name>' to create one)", project_dir);
    }

    /* ── Check 2: Source files ─────────────────────────────────────────── */
    {
        char src_dir[512];
        snprintf(src_dir, sizeof(src_dir), "%s/src", project_dir);
        int src_found = 0;
        int cm_files  = 0;

#ifdef _WIN32
        /* Use dir /b to count .cm files */
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "dir /b /s \"%s\\*.cm\" >NUL 2>&1", src_dir);
        src_found = (cm_access(src_dir, 0) == 0);
        if (src_found) {
            /* Count via a temp file trick */
            snprintf(cmd, sizeof(cmd),
                "dir /b \"%s\\*.cm\" 2>NUL | find /v \"\" /c > %s\\__cm_count__.tmp 2>NUL",
                src_dir, src_dir);
            system(cmd);
            char tmp_path[600];
            snprintf(tmp_path, sizeof(tmp_path), "%s\\__cm_count__.tmp", src_dir);
            FILE* tf = fopen(tmp_path, "r");
            if (tf) { 
                if (fscanf(tf, "%d", &cm_files) != 1) cm_files = 0; 
                fclose(tf); 
                remove(tmp_path); 
            }
        }
#else
        src_found = (cm_access(src_dir, 0) == 0);
        if (src_found) {
            char count_cmd[2048];
            snprintf(count_cmd, sizeof(count_cmd),
                "find '%s' -name '*.cm' 2>/dev/null | wc -l", src_dir);
            FILE* fp = popen(count_cmd, "r");
            if (fp) { if (fscanf(fp, "%d", &cm_files) != 1) cm_files = 0; pclose(fp); }
        }
#endif
        if (!src_found) {
            DR_FAIL("src/ directory missing — create it and add .cm files");
        } else if (cm_files == 0) {
            DR_WARN("src/ exists but contains no .cm files");
        } else {
            DR_OK("%d .cm source file(s) found in src/", cm_files);
        }
    }

    /* ── Check 3: C compiler ──────────────────────────────────────────── */
    if (cm_doctor_cmd_exists("gcc")) {
        DR_OK("gcc found in PATH");
    } else if (cm_doctor_cmd_exists("clang")) {
        DR_OK("clang found in PATH");
    } else if (cm_doctor_cmd_exists("cc")) {
        DR_OK("cc found in PATH");
    } else {
        DR_FAIL("No C compiler found (gcc/clang/cc) — CM needs one to compile output");
    }

    /* ── Check 4: CM runtime headers ──────────────────────────────────── */
    snprintf(path, sizeof(path), "%s/include/cm/core.h", project_dir);
    if (cm_access(path, 0) == 0) {
        DR_OK("CM runtime headers found (include/cm/core.h)");
    } else {
        DR_WARN("include/cm/core.h not found — make sure the CM library is in your project");
    }

    /* ── Check 5: Syntax check each .cm file ──────────────────────────── */
    printf("\n  %sSyntax Check%s\n", th.bold, th.reset);
    {
        static const char* const scan_dirs[] = {
            "src", "src/models", "src/services", "src/utils", NULL
        };
        int files_checked = 0;
        int parse_errors  = 0;
        int i;

        for (i = 0; scan_dirs[i]; i++) {
            char dir_path[512];
            snprintf(dir_path, sizeof(dir_path), "%s/%s", project_dir, scan_dirs[i]);

#ifdef _WIN32
            char find_cmd[2048];
            snprintf(find_cmd, sizeof(find_cmd), "dir /b \"%s\\*.cm\" 2>NUL", dir_path);
            FILE* dp = popen(find_cmd, "r");
            if (!dp) continue;
            char fname[512];
            while (fgets(fname, sizeof(fname), dp)) {
                fname[strcspn(fname, "\r\n")] = '\0';
                if (!fname[0]) continue;
                char full[1024];
                snprintf(full, sizeof(full), "%s/%s", dir_path, fname);
#else
            char find_cmd[2048];
            snprintf(find_cmd, sizeof(find_cmd), "find '%s' -maxdepth 1 -name '*.cm' 2>/dev/null", dir_path);
            FILE* dp = popen(find_cmd, "r");
            if (!dp) continue;
            char full[1024];
            while (fgets(full, sizeof(full), dp)) {
                full[strcspn(full, "\r\n")] = '\0';
                if (!full[0]) continue;
#endif
                {
                    size_t src_len = 0;
                    char*  src     = cm_doctor_read_file(full, &src_len);
                    if (!src) {
                        DR_FAIL("Cannot read %s", full);
                        parse_errors++;
                        files_checked++;
                        continue;
                    }

                    if (cm_doctor_has_raw_malloc(src) || cm_doctor_has_raw_free(src)) {
                        DR_WARN("%s — uses raw malloc/free", full);
                    }

                    cm_lexer_t lx;
                    cm_lexer_v2_init(&lx, src);
                    cm_token_t tok;
                    do { tok = cm_lexer_v2_next_token(&lx); } while (tok.kind != CM_TOK_EOF);

                    cm_error_clear();
                    cm_ast_v2_list_t ast = cm_parse_v2(src);
                    const char* err_msg = cm_error_get_message();

                    if (err_msg && err_msg[0] != '\0') {
                        DR_FAIL("%s — parse error: %s", full, err_msg);
                        parse_errors++;
                    } else {
                        DR_OK("%s — OK", full);
                    }
                    cm_ast_v2_free_list(&ast);
                    free(src);
                    files_checked++;
                }
            }
            pclose(dp);
        }

        if (files_checked == 0) {
            DR_WARN("No .cm files found to syntax-check");
        } else if (parse_errors == 0) {
            printf("  %s✨ All %d file(s) parsed successfully%s\n", th.green, files_checked, th.reset);
        }
    }

    /* ── Summary ──────────────────────────────────────────────────────── */
    printf("\n  %s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n", th.cyan, th.reset);
    if (issues == 0) {
        printf("  %s%s✅ Project looks healthy! No issues found.%s\n\n", th.bold, th.green, th.reset);
    } else {
        printf("  %s%s❌ Found %d issue(s).%s\n\n", th.bold, th.red, issues, th.reset);
    }

    return issues > 0 ? 1 : 0;
}

static const char* cm_arg_value(int argc, char** argv, const char* flag, const char* fallback) {
    for (int i = 0; i < argc - 1; ++i) {
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return fallback;
}

static int cm_scaffold_new_project(const char* name) {
    if (!name || !name[0]) return 1;
    
    /* Create enterprise folder structure */
    cm_mkdir(name);
    
    char path[512];
    
    /* Core source directories */
    snprintf(path, sizeof(path), "%s/src", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/src/models", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/src/services", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/src/utils", name);
    cm_mkdir(path);
    
    /* Test directory */
    snprintf(path, sizeof(path), "%s/tests", name);
    cm_mkdir(path);
    
    /* Public assets */
    snprintf(path, sizeof(path), "%s/public_html", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/public_html/styles", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/public_html/scripts", name);
    cm_mkdir(path);
    
    /* Documentation and build output */
    snprintf(path, sizeof(path), "%s/docs", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/dist", name);
    cm_mkdir(path);
    snprintf(path, sizeof(path), "%s/.cm", name);
    cm_mkdir(path);
    
    /* Generate cm.json */
    snprintf(path, sizeof(path), "%s/cm.json", name);
    FILE* f = fopen(path, "wb");
    if (f) {
        cm_string_t* cm_json = cm_project_generate_cm_json(name, "A CM project");
        fwrite(cm_json->data, 1, cm_json->length, f);
        cm_string_free(cm_json);
        fclose(f);
    }
    
    /* Generate .gitignore */
    snprintf(path, sizeof(path), "%s/.gitignore", name);
    f = fopen(path, "wb");
    if (f) {
        cm_string_t* gitignore = cm_project_generate_gitignore();
        fwrite(gitignore->data, 1, gitignore->length, f);
        cm_string_free(gitignore);
        fclose(f);
    }
    
    /* Generate README.md */
    snprintf(path, sizeof(path), "%s/README.md", name);
    f = fopen(path, "wb");
    if (f) {
        cm_string_t* readme = cm_project_generate_readme(name);
        fwrite(readme->data, 1, readme->length, f);
        cm_string_free(readme);
        fclose(f);
    }
    
    /* Generate main.cm with v2 syntax */
    snprintf(path, sizeof(path), "%s/src/main.cm", name);
    f = fopen(path, "wb");
    if (f) {
        const char* main_cm =
            "// CM v2 Project Entry Point\n"
            "\n"
            "fn main() {\n"
            "    println(\"Hello, CM v2!\");\n"
            "}\n";
        fwrite(main_cm, 1, strlen(main_cm), f);
        fclose(f);
    }
    
    /* Generate index.html */
    snprintf(path, sizeof(path), "%s/public_html/index.html", name);
    f = fopen(path, "wb");
    if (f) {
        const char* html =
            "<!doctype html>\n"
            "<html>\n"
            "<head>\n"
            "  <meta charset=\"utf-8\" />\n"
            "  <title>";
        fwrite(html, 1, strlen(html), f);
        fwrite(name, 1, strlen(name), f);
        const char* html2 = "</title>\n"
            "  <link rel=\"stylesheet\" href=\"/styles/main.css\" />\n"
            "</head>\n"
            "<body>\n"
            "  <h1>";
        fwrite(html2, 1, strlen(html2), f);
        fwrite(name, 1, strlen(name), f);
        const char* html3 = "</h1>\n"
            "  <div id=\"app\"></div>\n"
            "  <script src=\"/scripts/main.js\"></script>\n"
            "</body>\n"
            "</html>\n";
        fwrite(html3, 1, strlen(html3), f);
        fclose(f);
    }
    
    /* Generate CSS */
    snprintf(path, sizeof(path), "%s/public_html/styles/main.css", name);
    f = fopen(path, "wb");
    if (f) {
        const char* css =
            "body {\n"
            "  font-family: system-ui, -apple-system, sans-serif;\n"
            "  margin: 0;\n"
            "  padding: 40px;\n"
            "  background: #f5f5f5;\n"
            "}\n"
            "h1 { color: #333; }\n";
        fwrite(css, 1, strlen(css), f);
        fclose(f);
    }
    
    /* Generate JS */
    snprintf(path, sizeof(path), "%s/public_html/scripts/main.js", name);
    f = fopen(path, "wb");
    if (f) {
        const char* js =
            "document.addEventListener('DOMContentLoaded', () => {\n"
            "  document.getElementById('app').textContent = 'CM App Ready';\n"
            "});\n";
        fwrite(js, 1, strlen(js), f);
        fclose(f);
    }
    
    /* Print success with Neon styling */
    cm_print_neon_header(name);
    cm_print_neon_progress(100);
    printf("  📁 Created enterprise structure:\n");
    printf("     src/, tests/, docs/, dist/, public_html/\n");
    cm_print_neon_success(name, 0);
    
    return 0;
}

int main(int argc, char** argv) {
    cm_gc_init();
    cm_init_error_detector();

    if (argc >= 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        printf("2.0.0 V2\n");
        cm_gc_shutdown();
        return 0;
    }

    if (argc < 2) {
        cm_print_usage();
        cm_gc_shutdown();
        return 1;
    }

    /* Compatibility mode: cm <entry.cm> [output] */
    if (argc >= 2 && argv[1][0] != '-') {
        const char* cmd = argv[1];
        if (strcmp(cmd, "build") != 0 &&
            strcmp(cmd, "run") != 0 &&
            strcmp(cmd, "new") != 0 &&
            strcmp(cmd, "init") != 0 &&
            strcmp(cmd, "doctor") != 0 &&
            strcmp(cmd, "emitc") != 0 &&
            strcmp(cmd, "highlight") != 0) {
            const char* entry = argv[1];
            const char* out   = (argc >= 3) ? argv[2] : CM_DEFAULT_OUT;
            
            /* Load project config to get name */
            cm_project_config_t config;
            cm_project_load_config(&config, ".");
            const char* project_name = cm_project_get_name(&config);
            
            double start_time = cm_get_time();
            cm_print_neon_header(project_name);
            cm_print_neon_progress(50);
            
            int rc = cm_compile_file(entry, out);
            if (rc != 0) {
                cm_print_neon_error(cm_error_get_message());
                cm_gc_shutdown();
                return rc;
            }
            
            cm_print_neon_progress(100);
            double build_time = cm_get_time() - start_time;
            cm_print_neon_success(out, build_time);
            cm_gc_shutdown();
            return 0;
        }
    }

    const char* sub = argv[1];
    if (strcmp(sub, "init") == 0 || strcmp(sub, "new") == 0) {
        if (argc < 3) { cm_print_usage(); cm_gc_shutdown(); return 1; }
        int rc = cm_scaffold_new_project(argv[2]);
        cm_gc_shutdown();
        return rc;
    }

    if (strcmp(sub, "check") == 0 || strcmp(sub, "doctor") == 0) {
        if (strcmp(sub, "doctor") == 0) {
            const char* project_dir = (argc >= 3) ? argv[2] : ".";
            int rc = cm_doctor(project_dir);
            cm_gc_shutdown();
            return rc;
        }

        /* check subcommand */
        const char* entry = (argc >= 3) ? argv[2] : "src/main.cm";
        
        /* Check if file exists */
        FILE* f = fopen(entry, "r");
        if (!f) {
            fprintf(stderr, "cm: cannot open file '%s'\n", entry);
            cm_gc_shutdown();
            return 1;
        }
        fclose(f);
        
        double start_time = cm_get_time();
        cm_print_neon_header("typecheck");
        cm_print_neon_progress(50);
        
        /* TODO: Run v2 pipeline: lexer_v2 -> parser_v2 -> typecheck only */
        /* For now, just indicate the file was found */
        printf("  📄 Checking: %s\n", entry);
        
        cm_print_neon_progress(100);
        double check_time = cm_get_time() - start_time;
        printf("  ✅ File found (v2 pipeline not yet integrated)\n");
        printf("  ⏱️  Check time: %.2fs\n\n", check_time);
        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "test") == 0) {
        cm_print_neon_header("tests");
        printf("  🧪 Running tests...\n");
        /* TODO: Find and compile *_test.cm files, run them */
        printf("  ⚠️  Test runner not yet implemented for v2\n\n");
        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "fmt") == 0) {
        const char* path = (argc >= 3) ? argv[2] : "src/";
        printf("  📝 Formatting %s...\n", path);
        /* TODO: Implement v2 code formatter */
        printf("  ⚠️  Formatter not yet implemented for v2\n\n");
        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "install") == 0) {
        const char* out = cm_arg_value(argc, argv, "-o", "a.out");
        printf("  📦 Installing %s...\n", out);
        /* TODO: Copy binary to system path */
        #ifdef _WIN32
        printf("  Windows: Copy to %%LOCALAPPDATA%%\\bin or C:\\Windows\\System32\\\n");
        #else
        printf("  Unix: Copy to /usr/local/bin/ or ~/.local/bin/\n");
        #endif
        printf("  ⚠️  Install command not yet fully implemented\n\n");
        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "emitc") == 0) {
        if (argc < 3) { cm_print_usage(); cm_gc_shutdown(); return 1; }
        const char* entry = argv[2];
        const char* outc = cm_arg_value(argc, argv, "-o", "cm_out.c");
        int rc = cm_emit_c_file(entry, outc);
        if (rc != 0) {
            fprintf(stderr, "cm: emitc failed: %s\n", cm_error_get_message());
            cm_gc_shutdown();
            return rc;
        }
        printf("cm: wrote C output to '%s'\n", outc);
        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "build") == 0 || strcmp(sub, "run") == 0) {
        if (argc < 3) { cm_print_usage(); cm_gc_shutdown(); return 1; }
        const char* entry = argv[2];
        const char* out = cm_arg_value(argc, argv, "-o", CM_DEFAULT_OUT);

        /* Load project config */
        cm_project_config_t config;
        cm_project_load_config(&config, ".");
        const char* project_name = cm_project_get_name(&config);

        double start_time = cm_get_time();
        cm_print_neon_header(project_name);
        cm_print_neon_progress(50);

        int rc = cm_compile_file(entry, out);
        if (rc != 0) {
            cm_print_neon_error(cm_error_get_message());
            cm_gc_shutdown();
            return rc;
        }
        
        cm_print_neon_progress(100);
        double build_time = cm_get_time() - start_time;
        cm_print_neon_success(out, build_time);

        if (strcmp(sub, "run") == 0) {
            cm_cmd_t* c = cm_cmd_new(out);
            cm_cmd_result_t* r = c ? cm_cmd_run(c) : NULL;
            if (c) cm_cmd_free(c);
            if (r) {
                if (r->stdout_output) printf("%s", r->stdout_output->data);
                if (r->stderr_output && r->stderr_output->length) fprintf(stderr, "%s", r->stderr_output->data);
                rc = r->exit_code;
                cm_cmd_result_free(r);
            }
            cm_gc_shutdown();
            return rc;
        }

        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "highlight") == 0) {
        if (argc < 3) { cm_print_usage(); cm_gc_shutdown(); return 1; }
        const char* path = argv[2];
        cm_string_t* output = NULL;
        int rc = cm_highlight_file(path, &output);
        if (rc != 0) {
            fprintf(stderr, "cm: highlight failed: could not read file '%s'\n", path);
            cm_gc_shutdown();
            return 1;
        }
        if (output && output->data) {
            printf("%s\n", output->data);
            cm_string_free(output);
        }
        cm_gc_shutdown();
        return 0;
    }

    if (strcmp(sub, "packages") == 0) {
        if (argc < 3) { cm_print_usage(); cm_gc_shutdown(); return 1; }
        const char* pkg_cmd = argv[2];
        int rc = 1;
        
        if (strcmp(pkg_cmd, "init") == 0) {
            const char* name = (argc >= 4) ? argv[3] : NULL;
            rc = cm_packages_cmd_init(name);
        }
        else if (strcmp(pkg_cmd, "install") == 0) {
            const char* pkg_spec = (argc >= 4) ? argv[3] : NULL;
            const char* version = NULL;
            if (pkg_spec) {
                /* Parse name@version format */
                char* at = strchr(pkg_spec, '@');
                if (at) {
                    *at = '\0';
                    version = at + 1;
                }
            }
            rc = cm_packages_cmd_install(pkg_spec, version);
        }
        else if (strcmp(pkg_cmd, "remove") == 0) {
            if (argc < 4) { fprintf(stderr, "Error: Package name required\n"); cm_gc_shutdown(); return 1; }
            rc = cm_packages_cmd_remove(argv[3]);
        }
        else if (strcmp(pkg_cmd, "update") == 0) {
            const char* name = (argc >= 4) ? argv[3] : NULL;
            rc = cm_packages_cmd_update(name);
        }
        else if (strcmp(pkg_cmd, "list") == 0) {
            rc = cm_packages_cmd_list();
        }
        else if (strcmp(pkg_cmd, "search") == 0) {
            if (argc < 4) { fprintf(stderr, "Error: Search query required\n"); cm_gc_shutdown(); return 1; }
            rc = cm_packages_cmd_search(argv[3]);
        }
        else {
            fprintf(stderr, "Unknown packages command: %s\n", pkg_cmd);
            cm_print_usage();
        }
        
        cm_gc_shutdown();
        return rc;
    }

    cm_print_usage();
    cm_gc_shutdown();
    return 1;
}

