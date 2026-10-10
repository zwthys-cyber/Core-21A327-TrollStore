#ifndef CORE_BOOT_POLICY_H
#define CORE_BOOT_POLICY_H
#include <stdbool.h>
#include <string.h>
#include <strings.h>

static inline bool CBCIdentityMatches(const char *product, const char *build, const char *board,
                                     const char *runningProduct, const char *runningBuild,
                                     const char *runningBoard) {
    return product && *product && build && board && *board && runningProduct && runningBuild &&
           runningBoard && !strcmp(build, "21A327") && !strcmp(build, runningBuild) &&
           !strcmp(product, runningProduct) && !strcasecmp(board, runningBoard);
}

static inline bool CBCPathMatchesBoot(const char *path, const char *hash) {
    if (!path || !hash) return false;
    size_t size = strlen(hash);
    if (size != 40 && size != 96) return false;
    for (size_t i = 0; i < size; ++i)
        if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'A' && hash[i] <= 'F'))) return false;
    const char *prefix = "/private/preboot/";
    size_t prefixSize = strlen(prefix);
    if (strlen(path) <= prefixSize + size || strncmp(path, prefix, prefixSize) ||
        strncmp(path + prefixSize, hash, size) || path[prefixSize + size] != '/') return false;
    // Caller resolves the path with realpath first; reject unresolved dot components as well.
    return !strstr(path, "/../") && !strstr(path, "/./");
}
#endif
