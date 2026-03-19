#include "cm_codegen.h"

#include "cm/error.h"
#include "cm/memory.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>


static void cm_emit_prelude(cm_string_t* out) {
    cm_string_append(out, "#include \"cm/core.h\"\n");
    cm_string_append(out, "#include \"cm/error.h\"\n");
    cm_string_append(out, "#include \"cm/memory.h\"\n");
    cm_string_append(out, "#include \"cm/string.h\"\n");
    cm_string_append(out, "#include \"cm/array.h\"\n");
    cm_string_append(out, "#include \"cm/map.h\"\n");
    cm_string_append(out, "#include \"cm/json.h\"\n");
    cm_string_append(out, "#include \"cm/http.h\"\n");
    cm_string_append(out, "#include \"cm/file.h\"\n");
    cm_string_append(out, "#include \"cm/thread.h\"\n\n");

    cm_string_append(out,
        "static void cm_builtin_print(const char* s) { if (s) printf(\"%s\", s); }\n"
        "static void cm_builtin_print_str(cm_string_t* s) { if (s && s->data) printf(\"%s\", s->data); }\n\n");

    cm_string_append(out,
        "static void cm_serve_static(CMHttpRequest* req, CMHttpResponse* res, const char* path, const char* mime) {\n"
        "    (void)req; cm_res_send_file(res, path, mime);\n"
        "}\n"
        "static void cm_serve_index(CMHttpRequest* req, CMHttpResponse* res) {\n"
        "    cm_serve_static(req, res, \"public_html/index.html\", \"text/html\");\n"
        "}\n"
        "static void cm_serve_js(CMHttpRequest* req, CMHttpResponse* res) {\n"
        "    cm_serve_static(req, res, \"public_html/script.js\", \"application/javascript\");\n"
        "}\n"
        "static void cm_serve_css(CMHttpRequest* req, CMHttpResponse* res) {\n"
        "    cm_serve_static(req, res, \"public_html/style.css\", \"text/css\");\n"
        "}\n\n");
}

static void cm_emit_postlude(cm_string_t* out) {
    cm_string_append(out, "    cm_gc_shutdown();\n");
    cm_string_append(out, "    return 0;\n");
    cm_string_append(out, "}\n");
}

static void cm_emit_var_decl(cm_string_t* out, const cm_ast_node_t* n) {
    if (!out || !n) return;
    const char* type = n->as.var_decl.type_name->data;
    /* Trim any leading whitespace from type name */
    while (*type == ' ' || *type == '\t') type++;
    
    if (strcmp(type, "string") == 0 || strcmp(type, "str") == 0) {
        cm_string_append(out, "    cm_string_t* ");
        cm_string_append(out, n->as.var_decl.var_name->data);
        cm_string_append(out, " = ");
        if (strstr(n->as.var_decl.init_expr->data, "input") != NULL) {
            cm_string_append(out, "cm_input(NULL);\n");
        } else if (strstr(n->as.var_decl.init_expr->data, "malloc") != NULL) {
            cm_string_append(out, "cm_alloc(");
            /* Extract size from malloc(size) */
            const char* open = strchr(n->as.var_decl.init_expr->data, '(');
            const char* close = strrchr(n->as.var_decl.init_expr->data, ')');
            if (open && close) {
                cm_string_append(out, open + 1);
            }
            cm_string_append(out, ");\n");
        } else {
            cm_string_append(out, "cm_string_new(");
            cm_string_append(out, n->as.var_decl.init_expr->data);
            cm_string_append(out, ");\n");
        }
    } else if (strcmp(n->as.var_decl.type_name->data, "ptr") == 0) {
        /* ptr type - maps to void* */
        cm_string_append(out, "    void* ");
        cm_string_append(out, n->as.var_decl.var_name->data);
        cm_string_append(out, " = ");
        if (strstr(n->as.var_decl.init_expr->data, "malloc") != NULL) {
            cm_string_append(out, "cm_alloc(");
            /* Extract size from malloc(size) */
            const char* open = strchr(n->as.var_decl.init_expr->data, '(');
            const char* close = strrchr(n->as.var_decl.init_expr->data, ')');
            if (open && close && close > open) {
                /* Extract just the size argument between parens */
                size_t arg_len = (size_t)(close - (open + 1));
                char* arg = (char*)malloc(arg_len + 1);
                if (arg) {
                    memcpy(arg, open + 1, arg_len);
                    arg[arg_len] = '\0';
                    cm_string_append(out, arg);
                    free(arg);
                }
                cm_string_append(out, ", \"ptr\"");
            }
            cm_string_append(out, ");\n");
        } else {
            cm_string_append(out, n->as.var_decl.init_expr->data);
            cm_string_append(out, ";\n");
        }
    } else if (strcmp(n->as.var_decl.type_name->data, "list") == 0) {
        cm_string_append(out, "    cm_list_t* ");
        cm_string_append(out, n->as.var_decl.var_name->data);
        cm_string_append(out, " = cm_list_new();\n");
    } else if (strcmp(n->as.var_decl.type_name->data, "array") == 0) {
        cm_string_append(out, "    cm_dynarray_t* ");
        cm_string_append(out, n->as.var_decl.var_name->data);
        cm_string_append(out, " = cm_dynarray_new(0);\n");
    } else if (strcmp(n->as.var_decl.type_name->data, "map") == 0) {
        cm_string_append(out, "    cm_map_t* ");
        cm_string_append(out, n->as.var_decl.var_name->data);
        cm_string_append(out, " = cm_map_new();\n");
    } else {
        cm_string_append(out, "    ");
        cm_string_append(out, n->as.var_decl.type_name->data);
        cm_string_append(out, " ");
        cm_string_append(out, n->as.var_decl.var_name->data);
        cm_string_append(out, " = ");
        cm_string_append(out, n->as.var_decl.init_expr->data);
        cm_string_append(out, ";\n");
    }
}

static void cm_emit_expr_stmt(cm_string_t* out, const cm_ast_node_t* n) {
    if (!out || !n) return;
    const char* expr = n->as.expr_stmt.expr_text->data ? n->as.expr_stmt.expr_text->data : "";

    const char* trimmed = expr;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

    if (strncmp(trimmed, "gc.", 3) == 0) {
        if (strcmp(trimmed + 3, "stats()") == 0) { cm_string_append(out, "    cm_gc_stats();\n"); return; }
        if (strcmp(trimmed + 3, "collect()") == 0) { cm_string_append(out, "    cm_gc_collect();\n"); return; }
        if (strcmp(trimmed + 3, "leaks()") == 0) { cm_string_append(out, "    cm_gc_print_leaks();\n"); return; }
    }

    /* Native API: gc_collect() */
    if (strcmp(trimmed, "gc_collect()") == 0) {
        cm_string_append(out, "    cm_gc_collect();\n");
        return;
    }

    /* Native API: malloc(x) -> cm_alloc(x, \"ptr\") */
    if (strncmp(trimmed, "malloc(", 7) == 0) {
        const char* open = strchr(trimmed, '(');
        const char* close = strrchr(trimmed, ')');
        if (open && close && close > open) {
            cm_string_append(out, "    cm_alloc(");
            cm_string_append(out, open + 1);
            cm_string_append(out, ");\n");
            return;
        }
    }

    /* Native API: free(x) -> cm_free(x) */
    if (strncmp(trimmed, "free(", 5) == 0) {
        const char* open = strchr(trimmed, '(');
        const char* close = strrchr(trimmed, ')');
        if (open && close && close > open) {
            cm_string_append(out, "    cm_free(");
            /* Extract just the argument between parens */
            size_t arg_len = (size_t)(close - (open + 1));
            char* arg = (char*)malloc(arg_len + 1);
            if (arg) {
                memcpy(arg, open + 1, arg_len);
                arg[arg_len] = '\0';
                cm_string_append(out, arg);
                free(arg);
            }
            cm_string_append(out, ");\n");
            return;
        }
    }

    if (strncmp(trimmed, "print", 5) == 0) {
        const char* open = strchr(trimmed, '(');
        const char* close = open ? strrchr(trimmed, ')') : NULL;
        if (open && close && close > open + 1) {
            size_t arg_len = (size_t)(close - (open + 1));
            cm_string_t* arg = cm_string_new("");
            if (arg_len > 0) {
                char* tmp = (char*)malloc(arg_len + 1);
                if (tmp) {
                    memcpy(tmp, open + 1, arg_len);
                    tmp[arg_len] = '\0';
                    cm_string_set(arg, tmp);
                    free(tmp);
                }
            }

            const char* a = arg->data;
            while (*a == ' ' || *a == '\t') a++;
            if (*a == '"') {
                cm_string_append(out, "    cm_builtin_print(");
                cm_string_append(out, a);
                cm_string_append(out, ");\n");
            } else {
                /* For string literals passed from CM (which lost their quotes during lexing),
                 * we need to wrap them in quotes for C */
                
                /* Check if this is a simple identifier (single word, alphanumeric + underscore) */
                const char* p = a;
                int has_space = 0;
                int has_special = 0;
                size_t token_len = 0;
                
                while (*p && *p != ')' && *p != '\n' && *p != '\r') {
                    if (*p == ' ' || *p == '\t') has_space = 1;
                    if (!isalnum((unsigned char)*p) && *p != '_') has_special = 1;
                    token_len++;
                    p++;
                }
                
                int is_simple_ident = !has_space && !has_special && token_len > 0 &&
                                      (isalpha((unsigned char)a[0]) || a[0] == '_');

                if (is_simple_ident) {
                    cm_string_append(out, "    cm_builtin_print_str(");
                    cm_string_append(out, a);
                    cm_string_append(out, ");\n");
                } else {
                    /* String literal content without quotes - wrap in quotes */
                    cm_string_append(out, "    cm_builtin_print(\"");
                    cm_string_append(out, a);
                    cm_string_append(out, "\");\n");
                }
            }
            cm_string_free(arg);
            return;
        }
    }

    /* Basic string method lowering: name.upper(), name.lower(), name.append(x) */
    const char* dot = strchr(trimmed, '.');
    const char* open = dot ? strchr(dot, '(') : NULL;
    const char* close = open ? strrchr(open, ')') : NULL;
    if (dot && open && close && close >= open) {
        size_t recv_len = (size_t)(dot - trimmed);
        while (recv_len > 0 && isspace((unsigned char)trimmed[recv_len - 1])) recv_len--;

        size_t method_len = (size_t)(open - (dot + 1));
        while (method_len > 0 && isspace((unsigned char)dot[1])) { dot++; method_len--; }
        while (method_len > 0 && isspace((unsigned char)(dot[1 + method_len - 1]))) method_len--;

        char recv[256] = {0};
        char method[64] = {0};
        if (recv_len < sizeof(recv) && method_len < sizeof(method)) {
            memcpy(recv, trimmed, recv_len); recv[recv_len] = '\0';
            memcpy(method, dot + 1, method_len); method[method_len] = '\0';
            if (strcmp(method, "upper") == 0) {
                cm_string_append(out, "    cm_string_upper("); cm_string_append(out, recv); cm_string_append(out, ");\n"); return;
            }
            if (strcmp(method, "lower") == 0) {
                cm_string_append(out, "    cm_string_lower("); cm_string_append(out, recv); cm_string_append(out, ");\n"); return;
            }
            if (strcmp(method, "append") == 0) {
                size_t arg_len = (size_t)(close - open - 1);
                char argbuf[512] = {0};
                if (arg_len < sizeof(argbuf)) {
                    memcpy(argbuf, open + 1, arg_len); argbuf[arg_len] = '\0';
                    cm_string_append(out, "    cm_string_append(");
                    cm_string_append(out, recv);
                    cm_string_append(out, ", ");
                    cm_string_append(out, argbuf);
                    cm_string_append(out, ");\n");
                    return;
                }
            }
        }
    }

    cm_string_append(out, "    ");
    cm_string_append(out, expr);
    cm_string_append(out, ";\n");
}

static void cm_emit_poly_block(cm_string_t* out, const cm_ast_node_t* n) {
    if (!out || !n) return;
    cm_string_append(out, "\n");
    cm_string_append(out, n->as.poly_block.code->data);
    cm_string_append(out, "\n");
}

static void cm_string_replace(cm_string_t* str, const char* from, const char* to) {
    if (!str || !str->data || !from || !to) return;
    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    char* pos = strstr(str->data, from);
    while (pos) {
        size_t offset = (size_t)(pos - str->data);
        size_t tail_len = str->length - offset - from_len;
        size_t new_len = str->length - from_len + to_len;
        char* new_data = (char*)cm_alloc(new_len + 1, "cm_string_data");
        memcpy(new_data, str->data, offset);
        memcpy(new_data + offset, to, to_len);
        memcpy(new_data + offset + to_len, pos + from_len, tail_len);
        new_data[new_len] = '\0';
        cm_free(str->data);
        str->data = new_data;
        str->length = new_len;
        str->capacity = new_len + 1;
        pos = strstr(str->data + offset + to_len, from);
    }
}

static int cm_is_global_expr(const char* expr) {
    while (*expr == ' ' || *expr == '\n' || *expr == '\r' || *expr == '\t') expr++;
    if (strncmp(expr, "void ", 5) == 0) return 1;
    if (strncmp(expr, "int ", 4) == 0) return 1;
    if (strncmp(expr, "class ", 6) == 0) return 1;
    if (strncmp(expr, "struct ", 7) == 0) return 1;
    return 0;
}

cm_string_t* cm_codegen_to_c(const cm_ast_list_t* ast) {
    cm_string_t* out = cm_string_new("");
    cm_emit_prelude(out);

    /* Transpiler syntactic sugar replacements at AST level */
    int has_main = 0;
    for (const cm_ast_node_t* n = ast ? ast->head : NULL; n; n = n->next) {
        if (n->kind == CM_AST_EXPR_STMT && n->as.expr_stmt.expr_text) {
            /* Allow v2-ish entrypoint syntax in the legacy pipeline. */
            cm_string_replace(n->as.expr_stmt.expr_text, "fn main()", "void main()");
            /* Map println("...") to the existing builtin print. */
            cm_string_replace(n->as.expr_stmt.expr_text, "println(\"", "cm_builtin_print(\"");

            cm_string_replace(n->as.expr_stmt.expr_text, "void main()", "void __cm_main()");
            cm_string_replace(n->as.expr_stmt.expr_text, "app.get", "cm_app_get");
            cm_string_replace(n->as.expr_stmt.expr_text, "app.post", "cm_app_post");
            cm_string_replace(n->as.expr_stmt.expr_text, "app.listen", "cm_app_listen");
            cm_string_replace(n->as.expr_stmt.expr_text, "(req,res)=>{", "({ void __fn(CMHttpRequest* req, CMHttpResponse* res) {");
            cm_string_replace(n->as.expr_stmt.expr_text, "});", "} __fn; });");
            cm_string_replace(n->as.expr_stmt.expr_text, "html.serve(\"index.html\")", "cm_res_send_file(res, \"public_html/index.html\", \"text/html\")");
            cm_string_replace(n->as.expr_stmt.expr_text, "html.serve(", "cm_res_send_file(res, ");

            if (strstr(n->as.expr_stmt.expr_text->data, "void __cm_main()")) has_main = 1;
        }
    }

    /* Pass 1: Global functions and classes */
    for (const cm_ast_node_t* n = ast ? ast->head : NULL; n; n = n->next) {
        if (n->kind == CM_AST_EXPR_STMT && cm_is_global_expr(n->as.expr_stmt.expr_text->data)) {
            cm_emit_expr_stmt(out, n);
            cm_string_append(out, "\n");
        }
        else if (n->kind == CM_AST_POLYGLOT) {
            cm_emit_poly_block(out, n);
        }
    }

    /* Pass 2: Main entry point */
    cm_string_append(out, "int main(void) {\n");
    cm_string_append(out, "    cm_gc_init();\n");
    cm_string_append(out, "    cm_init_error_detector();\n\n");

    if (has_main) {
        cm_string_append(out, "    __cm_main();\n");
    }

    for (const cm_ast_node_t* n = ast ? ast->head : NULL; n; n = n->next) {
        if (n->kind == CM_AST_EXPR_STMT && cm_is_global_expr(n->as.expr_stmt.expr_text->data)) continue;
        if (n->kind == CM_AST_POLYGLOT) continue;

        switch (n->kind) {
            case CM_AST_VAR_DECL: cm_emit_var_decl(out, n); break;
            case CM_AST_EXPR_STMT: cm_emit_expr_stmt(out, n); break;
            case CM_AST_REQUIRE: break;
            default: break;
        }
    }

    cm_emit_postlude(out);
    return out;
}

