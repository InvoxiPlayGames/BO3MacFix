#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "download.h"
#include "wine.h"

#define Wine_Archive_Url "https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.0_1/wine-stable-11.0_1-osx64.tar.xz"
#define Wine_Archive_Sha256 "b50dc50ec7f41d58b115a6b685d4d1315ba3c797bd3aa0f49213f2703cb82388"
#define Wine_Binary_Suffix "wine/Wine Stable.app/Contents/Resources/wine/bin/wine"

static bool executable_exists(const char *path) {
    return access(path, X_OK) == 0;
}

bool wine_bundled_path(const char *fix_directory, char *out_path, size_t out_path_size) {
    int written = snprintf(out_path, out_path_size, "%s%s", fix_directory, Wine_Binary_Suffix);
    return written > 0 && (size_t)written < out_path_size && executable_exists(out_path);
}

bool wine_system_install_present(void) {
    static const char *const known_paths[] = {
        "/opt/homebrew/bin/wine64",
        "/opt/homebrew/bin/wine",
        "/usr/local/bin/wine64",
        "/usr/local/bin/wine",
        "/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine64",
        "/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine",
        "/Applications/Wine Staging.app/Contents/Resources/wine/bin/wine64",
        "/Applications/Wine Staging.app/Contents/Resources/wine/bin/wine",
        "/Applications/Wine Development.app/Contents/Resources/wine/bin/wine64",
        "/Applications/Wine Development.app/Contents/Resources/wine/bin/wine",
        NULL
    };
    for (int i = 0; known_paths[i] != NULL; i++) {
        if (executable_exists(known_paths[i]))
            return true;
    }
    return false;
}

bool wine_install_bundled(const char *fix_directory) {
    char wine_directory[4096];
    char archive_path[4096];
    char binary_path[4096];
    snprintf(wine_directory, sizeof(wine_directory), "%swine", fix_directory);
    snprintf(archive_path, sizeof(archive_path), "%swine/wine-stable.tar.xz", fix_directory);

    if (mkdir(wine_directory, 0755) != 0 && access(wine_directory, W_OK) != 0) {
        printf("Cannot create %s.\n", wine_directory);
        return false;
    }

    printf("Downloading Wine 11.0 (about 185 MB) from github.com/Gcenx/macOS_Wine_builds...\n");
    if (!download_verified(Wine_Archive_Url, Wine_Archive_Sha256, archive_path))
        return false;

    printf("Unpacking Wine...");
    char *const tar_argv[] = { "/usr/bin/tar", "-xJf", archive_path, "-C", wine_directory, NULL };
    int tar_result = run_command(tar_argv);
    unlink(archive_path);
    if (tar_result != 0 || !wine_bundled_path(fix_directory, binary_path, sizeof(binary_path))) {
        printf("failed.\n");
        return false;
    }
    printf("done!\n");

    char *const version_argv[] = { binary_path, "--version", NULL };
    if (run_command_quietly(version_argv) != 0) {
        printf("Wine was unpacked but does not start. Install Rosetta 2 and try again.\n");
        return false;
    }
    return true;
}
