#include "cm/cm_safety.h"
#include "cm/memory.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Simple strdup wrapper */
static char* cm_safety_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)cm_alloc(len, "safety_string");
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* ============================================================================
 * Dangerous Function Blacklist
 * ========================================================================== */

typedef struct {
    const char* func;
    const char* alternative;
    const char* reason;
} cm_blacklist_entry_t;

static const cm_blacklist_entry_t CM_BLACKLIST[] = {
    /* String operations - unsafe */
    {"strcpy", "cm_string_copy", "Unbounded string copy"},
    {"strcat", "cm_string_append", "Unbounded string concatenation"},
    {"strncpy", "cm_string_ncopy", "Potentially non-null-terminated copy"},
    {"strncat", "cm_string_nappend", "Potentially non-null-terminated concat"},
    {"sprintf", "cm_string_format", "Format string vulnerability"},
    {"vsprintf", "cm_string_vformat", "Format string vulnerability"},
    {"gets", "cm_input", "Buffer overflow guaranteed"},
    {"fgets", "cm_input_line", "Use CM safe input instead"},
    
    /* Memory operations - unsafe */
    {"alloca", "cm_alloc", "Stack allocation unsafe"},
    {"memcpy", "cm_safe_memcpy", "Bounds-checked copy required"},
    {"memset", "cm_safe_memset", "Bounds-checked set required"},
    
    /* Note: malloc/free are allowed in CM - they get converted to cm_alloc/cm_free */
    
    /* File operations - restrict */
    {"fopen", "cm_file_open", "Use CM file abstraction"},
    {"freopen", NULL, "File reopen not allowed"},
    {"fclose", "cm_file_close", "Use CM file abstraction"},
    {"fread", "cm_file_read", "Use CM file abstraction"},
    {"fwrite", "cm_file_write", "Use CM file abstraction"},
    {"fscanf", NULL, "Format string vulnerability"},
    {"fprintf", "cm_file_write", "Use CM file abstraction"},
    
    /* System operations - dangerous */
    {"system", NULL, "Arbitrary command execution"},
    {"popen", NULL, "Arbitrary command execution"},
    {"pclose", NULL, "Shell execution"},
    {"execl", NULL, "Process execution"},
    {"execv", NULL, "Process execution"},
    {"execle", NULL, "Process execution"},
    {"execve", NULL, "Process execution"},
    
    /* Network - restrict */
    {"socket", "cm_socket_create", "Use CM network abstraction"},
    {"connect", "cm_socket_connect", "Use CM network abstraction"},
    {"bind", "cm_socket_bind", "Use CM network abstraction"},
    {"listen", "cm_socket_listen", "Use CM network abstraction"},
    {"accept", "cm_socket_accept", "Use CM network abstraction"},
    
    /* Pointer arithmetic patterns */
    {"offsetof", NULL, "Low-level memory manipulation"},
    
    {NULL, NULL, NULL}
};

cm_safety_opts_t CM_SAFETY_STRICT = {
    .check_blacklist = 1,
    .check_static = 1,
    .sandbox_level = 2,
    .allow_file_ops = 0,
    .allow_network = 0,
    .allow_system_calls = 0
};

cm_safety_opts_t CM_SAFETY_MODERATE = {
    .check_blacklist = 1,
    .check_static = 1,
    .sandbox_level = 1,
    .allow_file_ops = 1,
    .allow_network = 1,
    .allow_system_calls = 0
};

int cm_safety_is_blacklisted(const char* func_name) {
    if (!func_name) return 0;
    for (const cm_blacklist_entry_t* entry = CM_BLACKLIST; entry->func; entry++) {
        if (strcmp(func_name, entry->func) == 0) return 1;
    }
    return 0;
}

const char* cm_safety_blacklist_suggestion(const char* func_name) {
    if (!func_name) return NULL;
    for (const cm_blacklist_entry_t* entry = CM_BLACKLIST; entry->func; entry++) {
        if (strcmp(func_name, entry->func) == 0) return entry->alternative;
    }
    return NULL;
}

/* Static Analysis - Simple Pattern Matching - Patterns currently unused but reserved for future */
#if 0
typedef struct {
    const char* pattern;
    cm_safety_result_t type;
    const char* message;
} cm_pattern_t;

static const cm_pattern_t CM_PATTERNS[] = {
    {"*p++", CM_SAFETY_UNSAFE_PTR_ARITH, "Unchecked pointer increment"},
    {"*p--", CM_SAFETY_UNSAFE_PTR_ARITH, "Unchecked pointer decrement"},
    {"p[i]", CM_SAFETY_BUFFER_OVERFLOW_RISK, "Array access without bounds check"},
    {"(char*)", CM_SAFETY_UNSAFE_CAST, "Dangerous pointer cast"},
    {"(void*)", CM_SAFETY_UNSAFE_CAST, "Dangerous pointer cast"},
    {"free(", CM_SAFETY_USE_AFTER_FREE, "Manual memory management"},
    {"#include <", CM_SAFETY_UNSAFE_INCLUDE, "Custom includes may be unsafe"},
    {"system(", CM_SAFETY_SYSTEM_CALL, "System command execution"},
    {"socket(", CM_SAFETY_NETWORK_OP, "Raw network access"},
    {NULL, 0, NULL}
};
#endif

static int cm_safety_check_blacklist(const char* code, cm_safety_violation_t** violations, size_t* count) {
    size_t capacity = 8;
    *violations = (cm_safety_violation_t*)cm_alloc(capacity * sizeof(cm_safety_violation_t), "safety_violations");
    if (!violations) return -1;
    *count = 0;
    
    /* Simple token-based search for blacklisted functions */
    const char* p = code;
    size_t line = 1;
    size_t col = 1;
    
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            /* Start of identifier */
            const char* start = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            
            size_t len = (size_t)(p - start);
            char ident[64] = {0};
            if (len < sizeof(ident)) {
                memcpy(ident, start, len);
                
                /* Check if followed by '(' - it's a function call */
                const char* after = p;
                while (*after == ' ' || *after == '\t') after++;
                
                if (*after == '(' && cm_safety_is_blacklisted(ident)) {
                    /* Expand violations if needed */
                    if (*count >= capacity) {
                        capacity *= 2;
                        cm_safety_violation_t* new_v = (cm_safety_violation_t*)cm_alloc(capacity * sizeof(cm_safety_violation_t), "safety_violations");
                        if (!new_v) return -1;
                        memcpy(new_v, *violations, *count * sizeof(cm_safety_violation_t));
                        cm_free(*violations);
                        *violations = new_v;
                    }
                    
                    cm_safety_violation_t* v = &(*violations)[*count];
                    v->code = CM_SAFETY_UNSAFE_FUNC;
                    v->line = line;
                    v->column = col;
                    
                    const char* alt = cm_safety_blacklist_suggestion(ident);
                    char msg[256];
                    if (alt) {
                        snprintf(msg, sizeof(msg), "Dangerous function '%s' - use '%s' instead", ident, alt);
                    } else {
                        snprintf(msg, sizeof(msg), "Dangerous function '%s' - not allowed in CM", ident);
                    }
                    v->message = cm_safety_strdup(msg);
                    v->suggestion = alt ? cm_safety_strdup(alt) : NULL;
                    
                    (*count)++;
                }
            }
            
            col += (int)len;
        } else {
            if (*p == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
            p++;
        }
    }
    
    return (*count > 0) ? -1 : 0;
}

/* Pattern checking - currently integrated into blacklist check */
static int cm_safety_check_patterns(const char* code, cm_safety_violation_t** violations, size_t* count) {
    (void)code;
    (void)violations;
    (void)count;
    /* Full static analysis would use a proper C parser */
    return 0;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

int cm_safety_check(const char* code, cm_safety_opts_t* opts,
                    cm_safety_violation_t** violations, size_t* violation_count) {
    if (!code || !violations || !violation_count) return -1;
    
    cm_safety_opts_t actual_opts = opts ? *opts : CM_SAFETY_STRICT;
    
    *violations = NULL;
    *violation_count = 0;
    
    /* Check blacklist if enabled */
    if (actual_opts.check_blacklist) {
        if (cm_safety_check_blacklist(code, violations, violation_count) != 0) {
            /* Violations found, but we continue to collect all */
        }
    }
    
    /* Check patterns if enabled */
    if (actual_opts.check_static) {
        cm_safety_check_patterns(code, violations, violation_count);
    }
    
    return (*violation_count > 0) ? -1 : 0;
}

void cm_safety_free_violations(cm_safety_violation_t* violations, size_t count) {
    if (!violations) return;
    for (size_t i = 0; i < count; i++) {
        if (violations[i].message) cm_free(violations[i].message);
        if (violations[i].suggestion) cm_free(violations[i].suggestion);
    }
    cm_free(violations);
}

void cm_safety_print_report(const char* code, cm_safety_violation_t* violations, size_t count) {
    (void)code; /* Unused but kept for API compatibility */
    if (!violations || count == 0) {
        printf("[CM Safety] No violations found. Code is safe.\n");
        return;
    }
    
    fprintf(stderr, "[CM Safety] Found %zu violation(s):\n", count);
    for (size_t i = 0; i < count; i++) {
        cm_safety_violation_t* v = &violations[i];
        fprintf(stderr, "  Line %zu, Col %zu: %s\n", v->line, v->column, v->message);
        if (v->suggestion) {
            fprintf(stderr, "    Suggestion: Use '%s'\n", v->suggestion);
        }
    }
}

/* ============================================================================
 * Sandbox (Platform-specific)
 * ========================================================================== */

int cm_sandbox_init(int level) {
    if (level == 0) return 0; /* No sandbox */
    
    /* Platform-specific sandbox setup would go here:
     * - Linux: seccomp-bpf, namespaces, chroot
     * - Windows: AppContainer, job objects
     * - macOS: seatbelt
     */
    
    (void)level;
    return 0; /* Placeholder - full implementation would be OS-specific */
}

int cm_sandbox_exec(const char* compiled_path, int level) {
    if (level == 0) {
        /* No sandbox - direct execution */
        /* Would use system() or execve() here */
        (void)compiled_path;
        return 0;
    }
    
    /* Sandboxed execution would go here */
    (void)compiled_path;
    (void)level;
    return 0;
}

void cm_sandbox_cleanup(void) {
    /* Cleanup sandbox resources */
}
