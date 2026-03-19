#include "cm/error_detail.h"
#include "cm/string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <io.h>
#define cm_isatty _isatty
#define cm_fileno _fileno
#else
#include <unistd.h>
#define cm_isatty isatty
#define cm_fileno fileno
#endif

static __thread cm_error_detail_t cm_current_error_detail;

void cm_error_detail_init(cm_error_detail_t* detail) {
    if (!detail) return;
    memset(detail, 0, sizeof(*detail));
    detail->code = CM_SUCCESS;
    detail->severity = CM_SEVERITY_ERROR;
    detail->line = -1;
    detail->column = -1;
}

void cm_error_detail_set_location(cm_error_detail_t* detail, const char* file, int line, int column) {
    if (!detail) return;
    if (file) strncpy(detail->file, file, sizeof(detail->file) - 1);
    detail->line = line;
    detail->column = column;
}

void cm_error_detail_set_object(cm_error_detail_t* detail, const char* type, const char* name) {
    if (!detail) return;
    if (type) strncpy(detail->object_type, type, sizeof(detail->object_type) - 1);
    if (name) strncpy(detail->object_name, name, sizeof(detail->object_name) - 1);
}

void cm_error_detail_set_message(cm_error_detail_t* detail, const char* fmt, ...) {
    if (!detail || !fmt) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail->message, sizeof(detail->message), fmt, args);
    va_end(args);
}

void cm_error_detail_set_suggestion(cm_error_detail_t* detail, const char* fmt, ...) {
    if (!detail || !fmt) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail->suggestion, sizeof(detail->suggestion), fmt, args);
    va_end(args);
}

void cm_error_detail_set_context(cm_error_detail_t* detail,
                                  const char* before[3], int before_count,
                                  const char* current,
                                  const char* after[3], int after_count) {
    if (!detail) return;
    
    for (int i = 0; i < before_count && i < 3; i++) {
        if (before[i]) strncpy(detail->context_before[i], before[i], sizeof(detail->context_before[0]) - 1);
    }
    
    if (current) strncpy(detail->context_line, current, sizeof(detail->context_line) - 1);
    
    for (int i = 0; i < after_count && i < 3; i++) {
        if (after[i]) strncpy(detail->context_after[i], after[i], sizeof(detail->context_after[0]) - 1);
    }
    
    detail->context_count = before_count + 1 + after_count;
}

static int cm_use_colors(void) {
    return cm_isatty(cm_fileno(stderr));
}

void cm_error_detail_print(const cm_error_detail_t* detail) {
    if (!detail) return;
    
    int colors = cm_use_colors();
    
    const char* red = colors ? "\x1b[31m" : "";
    const char* yellow = colors ? "\x1b[33m" : "";
    const char* cyan = colors ? "\x1b[36m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    const char* bold = colors ? "\x1b[1m" : "";
    
    const char* emoji = detail->severity == CM_SEVERITY_FATAL ? "💥" :
                        detail->severity == CM_SEVERITY_ERROR ? "❌" : "⚠️";
    const char* severity = detail->severity == CM_SEVERITY_FATAL ? "Fatal Error" :
                           detail->severity == CM_SEVERITY_ERROR ? "Error" : "Warning";
    
    fprintf(stderr, "\n%s%s %s%s: %s%s%s\n", 
            bold, emoji, severity, reset,
            red, detail->message, reset);
    
    if (detail->file[0]) {
        fprintf(stderr, "   %sLocation:%s %s", cyan, reset, detail->file);
        if (detail->line > 0) fprintf(stderr, ":%d", detail->line);
        if (detail->column > 0) fprintf(stderr, ":%d", detail->column);
        fprintf(stderr, "\n");
    }
    
    if (detail->object_name[0]) {
        fprintf(stderr, "   %sObject:%s %s %s%s%s\n",
                cyan, reset, detail->object_type,
                yellow, detail->object_name, reset);
    }
    
    if (detail->suggestion[0]) {
        fprintf(stderr, "   %s💡 Fix:%s %s\n", cyan, reset, detail->suggestion);
    }
    
    fprintf(stderr, "\n");
}

void cm_error_detail_print_syntax(const cm_error_detail_t* detail) {
    if (!detail) return;
    
    int colors = cm_use_colors();
    const char* red = colors ? "\x1b[31m" : "";
    const char* cyan = colors ? "\x1b[36m" : "";
    const char* gray = colors ? "\x1b[90m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    const char* bold = colors ? "\x1b[1m" : "";
    
    fprintf(stderr, "\n%s❌ Syntax Error:%s %s%s%s", bold, reset, red, detail->message, reset);
    
    if (detail->file[0]) {
        fprintf(stderr, "\n   %s→%s %s", cyan, reset, detail->file);
        if (detail->line > 0) fprintf(stderr, ":%d", detail->line);
        if (detail->column > 0) fprintf(stderr, ":%d", detail->column);
    }
    fprintf(stderr, "\n\n");
    
    /* Print context lines */
    for (int i = 0; i < 3; i++) {
        if (detail->context_before[i][0]) {
            fprintf(stderr, "   %s%4d |%s %s\n", gray, detail->line - (3 - i), reset, detail->context_before[i]);
        }
    }
    
    if (detail->context_line[0]) {
        fprintf(stderr, "   %s%4d |%s %s\n", bold, detail->line, reset, detail->context_line);
        
        /* Print caret */
        if (detail->column > 0) {
            fprintf(stderr, "        | ");
            for (int i = 1; i < detail->column; i++) fprintf(stderr, " ");
            fprintf(stderr, "%s^", red);
            size_t line_len = strlen(detail->context_line);
            for (int i = detail->column; i < (int)line_len && i < detail->column + 10; i++) {
                if (detail->context_line[i] && detail->context_line[i] != ' ') {
                    fprintf(stderr, "~");
                }
            }
            fprintf(stderr, "%s\n", reset);
        }
    }
    
    for (int i = 0; i < 3; i++) {
        if (detail->context_after[i][0]) {
            fprintf(stderr, "   %s%4d |%s %s\n", gray, detail->line + i + 1, reset, detail->context_after[i]);
        }
    }
    
    fprintf(stderr, "\n");
    
    if (detail->suggestion[0]) {
        fprintf(stderr, "   %s💡 Suggestion:%s %s\n\n", cyan, reset, detail->suggestion);
    }
}

void cm_error_detail_print_runtime(const cm_error_detail_t* detail) {
    if (!detail) return;
    
    int colors = cm_use_colors();
    const char* yellow = colors ? "\x1b[33m" : "";
    const char* cyan = colors ? "\x1b[36m" : "";
    const char* reset = colors ? "\x1b[0m" : "";
    const char* bold = colors ? "\x1b[1m" : "";
    
    fprintf(stderr, "\n%s⚠️  CM Runtime Error:%s\n", bold, reset);
    fprintf(stderr, "   %sLocation:%s %s -> Line %d\n", cyan, reset, detail->file, detail->line);
    
    if (detail->object_name[0]) {
        fprintf(stderr, "   %sObject:%s %s %s%s%s\n",
                cyan, reset, detail->object_type,
                yellow, detail->object_name, reset);
    }
    
    fprintf(stderr, "   %sStatus:%s %s\n", cyan, reset, detail->message);
    
    if (detail->suggestion[0]) {
        fprintf(stderr, "   %sFix:%s %s\n", cyan, reset, detail->suggestion);
    }
    
    fprintf(stderr, "\n");
}

cm_string_t* cm_error_detail_to_json(const cm_error_detail_t* detail) {
    if (!detail) return cm_string_new("{}");
    
    cm_string_t* json = cm_string_new("{\"error\":{\"code\":");
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", detail->code);
    cm_string_append(json, buf);
    
    cm_string_append(json, ",\"severity\":");
    snprintf(buf, sizeof(buf), "%d", detail->severity);
    cm_string_append(json, buf);
    
    if (detail->message[0]) {
        cm_string_append(json, ",\"message\":\"");
        cm_string_append(json, detail->message);
        cm_string_append(json, "\"");
    }
    
    if (detail->file[0]) {
        cm_string_append(json, ",\"file\":\"");
        cm_string_append(json, detail->file);
        cm_string_append(json, "\"");
    }
    
    if (detail->line > 0) {
        cm_string_append(json, ",\"line\":");
        snprintf(buf, sizeof(buf), "%d", detail->line);
        cm_string_append(json, buf);
    }
    
    if (detail->column > 0) {
        cm_string_append(json, ",\"column\":");
        snprintf(buf, sizeof(buf), "%d", detail->column);
        cm_string_append(json, buf);
    }
    
    if (detail->object_name[0]) {
        cm_string_append(json, ",\"object\":{\"type\":\"");
        cm_string_append(json, detail->object_type);
        cm_string_append(json, "\",\"name\":\"");
        cm_string_append(json, detail->object_name);
        cm_string_append(json, "\"}");
    }
    
    if (detail->suggestion[0]) {
        cm_string_append(json, ",\"suggestion\":\"");
        cm_string_append(json, detail->suggestion);
        cm_string_append(json, "\"");
    }
    
    cm_string_append(json, "}}");
    return json;
}

cm_error_detail_t* cm_error_detail_current(void) {
    return &cm_current_error_detail;
}

void cm_error_detail_clear(void) {
    cm_error_detail_init(&cm_current_error_detail);
}
