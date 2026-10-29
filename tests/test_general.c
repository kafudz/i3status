#include <assert.h>
#include <string.h>
#include <stdio.h>

#ifndef HAVE_STRNDUP
static char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}
#endif

int main(void) {
    char *a = strndup("hello", 3);
    assert(a && strcmp(a, "hel") == 0);
    free(a);
    char *b = strndup("hi", 100);
    assert(b && strcmp(b, "hi") == 0);
    free(b);
    puts("strndup fallback: OK");
    return 0;
}
