#include <stdio.h>
#include "cm/string.h"

int main() {
    /* 15 bytes in UTF-8, but only 5 characters (ñ, é, ó, 漢, 字) */
    cm_string_t* str = cm_string_new("ñéó漢字");
    printf("Raw byte length: %zu\n", cm_string_length(str));
    printf("UTF-8 character length: %zu\n", cm_string_length_utf8(str));
    
    if (cm_string_length_utf8(str) == 5 && cm_string_length(str) > 5) {
        printf("UTF-8 count SUCCESS\n");
    } else {
        printf("UTF-8 count FAILED\n");
    }
    
    cm_string_free(str);
    return 0;
}
