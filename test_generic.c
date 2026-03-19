#include <stdio.h>

void print_int(int x) { printf("INT: %d\n", x); }
void print_str(const char* s) { printf("STR: %s\n", s); }

#define print(X) _Generic((X), \
    int: print_int, \
    char*: print_str, \
    const char*: print_str \
)(X)

int main() {
    int z = 42;
    print(z);
    print("hello");
    return 0;
}
