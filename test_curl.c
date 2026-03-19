#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    FILE* fp = popen("curl -s -i https://example.com", "r");
    if (!fp) {
        printf("Failed to run curl\n");
        return 1;
    }
    char buffer[4096];
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[n] = '\0';
    printf("--- CURL OUTPUT ---\n%s\n", buffer);
    pclose(fp);
    return 0;
}
