#include "user/basename.h"

char* basename(char* path) {
    char* base = path;
    while (*path) {
        if (*path == '/') {
            base = path;
        }
    }
    return base;
}
