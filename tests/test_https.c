#include <stdio.h>
#include "cm/http.h"
#include "cm/string.h"

int main() {
    printf("Testing HTTPS direct call...\n");
    CHttpResponse* res = cm_http_get("https://example.com");
    if (res) {
        printf("Secure HTTP GET returned status: %d\n", res->status_code);
        printf("Body length: %zu\n", cm_string_length(res->body));
        CHttpResponse_delete(res);
    } else {
        printf("HTTPS execution failed.\n");
    }
    return 0;
}
