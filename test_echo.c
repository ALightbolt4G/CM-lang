#include "cm/core.h"
#include "cm/memory.h"
#include "cm/string.h"
#include <stdio.h>

int main(void) {
    cm_gc_init();
    printf("GC initialized\n");
    
    cm_string_t* s = cm_string_new("Test String!");
    if (!s) {
        printf("cm_string_new returned NULL!\n");
    } else if (!s->data) {
        printf("cm_string_new returned string with NULL data!\n");
    } else {
        printf("String data: '%s', length: %zu\n", s->data, s->length);
        cm_println(s);
    }
    
    cm_gc_shutdown();
    return 0;
}
