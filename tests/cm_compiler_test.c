#include "cm/core.h"
#include "cm/error.h"
#include "cm/memory.h"
#include "cm/cm_lang.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    cm_gc_init();
    cm_init_error_detector();

    /* Front-end/codegen smoke test: emit C from a fixture .cm file. */
    int rc = cm_emit_c_file("tests/fixtures/hello.cm", "tests/fixtures/hello_out.c");
    if (rc != 0) {
        fprintf(stderr, "cm_emit_c_file failed: %s\n", cm_error_get_message());
        cm_gc_shutdown();
        return 1;
    }

    /* If we got here, parsing + desugaring + codegen succeeded. */
    assert(rc == 0);

    cm_gc_shutdown();
    return 0;
}

