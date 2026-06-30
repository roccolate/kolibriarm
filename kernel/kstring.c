#include "kernel/kstring.h"

int kstreq(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == *b;
}
