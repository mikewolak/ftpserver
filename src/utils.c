// src/utils.c
#include "../include/utils.h"

char* get_absolute_path(const char *path) {
    static char abs_path[PATH_MAX];
    realpath(path, abs_path);
    return abs_path;
}
