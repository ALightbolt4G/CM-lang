#include "cm/compiler/ast_v2.h"

// Function declarations for testing
cm_ast_v2_list_t cm_parse_v2(const char* src);
cm_string_t* cm_codegen_v2_to_c(const cm_ast_v2_list_t* ast);
#include <stdio.h>
#include <stdlib.h>

/* Simple test for CM v2 compiler */

int main() {
    printf("Testing CM v2 compiler...\n");
    
    // Test simple CM v2 code
    const char* test_code = 
        "let name = \"Adham\";\n"
        "let age := 21;\n"
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b;\n"
        "}\n"
        "let sum = add(10, 5);\n";
    
    printf("Parsing test code:\n%s\n", test_code);
    
    // Parse the code
    cm_ast_v2_list_t ast = {0};
    ast = cm_parse_v2(test_code);
    printf("Parse successful!\n");
    
    // Generate C code
    cm_string_t* c_code = cm_codegen_v2_to_c(&ast);
    if (c_code) {
        printf("Generated C code:\n%s\n", c_code->data);
        
        // Write to file for testing
        FILE* f = fopen("test_output.c", "w");
        if (f) {
            fprintf(f, "%s", c_code->data);
            fclose(f);
            printf("C code written to test_output.c\n");
        }
        
        cm_string_free(c_code);
    } else {
        printf("Code generation failed!\n");
    }
    
    // Cleanup
    cm_ast_v2_free_list(&ast);
    
    printf("Test completed!\n");
    return 0;
}
