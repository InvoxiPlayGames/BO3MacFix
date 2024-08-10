#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Gets just the filename of a given path
const char *filename_from_path(const char *path) {
    size_t len = strlen(path);
    for (size_t i = len - 1; i > 0; i--) {
        if (path[i] == '/')
            return path + i + 1;
    }
    return path;
}
