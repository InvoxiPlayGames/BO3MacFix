#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "assets.h"
#include "download.h"

#define Assets_Archive_Url "https://github.com/InvoxiPlayGames/BO3MacFix/releases/download/1.2-beta3/BO3MacFix-1.2-beta3.tar.gz"
#define Assets_Archive_Sha256 "aa954df5a912e8c0131a4178c0983e9d65313487fcc9092ea49a78bf6aa2b14c"

static const char *const required_assets[] = {
    "zone/core_patch.ff",
    "zone/mp_patch.ff",
    "zone/zm_patch.ff",
    "cod2map/cod2map64.exe",
    "cod2map/c2mpatch.dll",
    "Physics/cp_mi_cairo_infection2_navmesh.hkt",
    "Physics/cp_mi_cairo_infection2_navvolume.hkt",
    NULL
};

bool assets_present(const char *fix_directory) {
    char path[4096];
    for (int i = 0; required_assets[i] != NULL; i++) {
        snprintf(path, sizeof(path), "%s%s", fix_directory, required_assets[i]);
        if (access(path, R_OK) != 0)
            return false;
    }
    return true;
}

bool assets_install(const char *fix_directory) {
    char archive_path[4096];
    snprintf(archive_path, sizeof(archive_path), "%sBO3MacFix-1.2-beta3.tar.gz", fix_directory);

    printf("Downloading the BO3MacFix 1.2-beta3 assets (about 85 MB) from github.com/InvoxiPlayGames/BO3MacFix...\n");
    if (!download_verified(Assets_Archive_Url, Assets_Archive_Sha256, archive_path))
        return false;

    printf("Unpacking the assets...");
    char *const tar_argv[] = {
        "/usr/bin/tar", "-xzf", archive_path, "-C", (char *)fix_directory, "--strip-components", "1",
        "BO3MacFix/zone", "BO3MacFix/cod2map", "BO3MacFix/Physics", "BO3MacFix/natives", NULL
    };
    int tar_result = run_command(tar_argv);
    unlink(archive_path);
    if (tar_result != 0 || !assets_present(fix_directory)) {
        printf("failed.\n");
        return false;
    }
    printf("done!\n");
    return true;
}
