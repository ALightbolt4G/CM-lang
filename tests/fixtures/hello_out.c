#include "cm/core.h"
#include "cm/error.h"
#include "cm/memory.h"
#include "cm/string.h"
#include "cm/array.h"
#include "cm/map.h"
#include "cm/json.h"
#include "cm/http.h"
#include "cm/file.h"
#include "cm/thread.h"

static void cm_builtin_print(const char* s) { if (s) printf("%s", s); }
static void cm_builtin_print_str(cm_string_t* s) { if (s && s->data) printf("%s", s->data); }

static void cm_serve_static(CMHttpRequest* req, CMHttpResponse* res, const char* path, const char* mime) {
    (void)req; cm_res_send_file(res, path, mime);
}
static void cm_serve_index(CMHttpRequest* req, CMHttpResponse* res) {
    cm_serve_static(req, res, "public_html/index.html", "text/html");
}
static void cm_serve_js(CMHttpRequest* req, CMHttpResponse* res) {
    cm_serve_static(req, res, "public_html/script.js", "application/javascript");
}
static void cm_serve_css(CMHttpRequest* req, CMHttpResponse* res) {
    cm_serve_static(req, res, "public_html/style.css", "text/css");
}

int main(void) {
    cm_gc_init();
    cm_init_error_detector();

    cm_string_t* name = cm_input(NULL);
    cm_string_upper(name);
    cm_builtin_print("Hello, ");
    cm_builtin_print_str(name);
    cm_builtin_print("!\n");
    cm_gc_stats();
    cm_gc_shutdown();
    return 0;
}
