#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/syslimits.h>
#include <unistd.h>
#include <copyfile.h>

#include "ansi.h"
#include "utilities.h"

char *baseDirPath = NULL;

typedef enum _nextaction_t {
    A_Exit,
    A_ShowMainMenuUninstalled,
    A_ShowMainMenuInstalled,
    A_ShowGameDirectoryError,
    A_ShowFixDirectoryError,
    A_StartInstall,
    A_StartVerify,
    A_StartUninstall,
    A_ClearNavmesh,
    A_LaunchGame,
} nextaction_t;

int launch_game_native(const char *path);

void StartTerminalWindow() {
    printf(TmTitle "BO3MacFix Installer" TmEnd);
    printf(TmClear TmStartPos);

    printf(TmBold TmUnderline "BO3MacFix Installer v1.2\n\n" TmReset);
}

nextaction_t Error_NotInRightDirectory() {
    StartTerminalWindow();
    printf("The BO3MacFix installer wasn't able to find the Black Ops 3 game.\n");
    printf("Make sure you copied the BO3MacFix folder next to the Black Ops 3 .app file.\n\n");
    return A_Exit;
}

nextaction_t Error_CantFindFixDirectory() {
    StartTerminalWindow();
    printf("The BO3MacFix installer wasn't able to find the BO3MacFix files.\n");
    printf("Make sure you copied the entire BO3MacFix folder next to the Black Ops 3 .app file.\n\n");
    return A_Exit;
}

nextaction_t MainMenuUninstalled() {
    StartTerminalWindow();

    printf("1) Install BO3MacFix\n\n");
    printf("9) Launch game with fix\n\n");
    printf("0) Exit\n\n");

    printf("Select an option: ");

    char c = (char)getchar();
    if (c == '1')
        return A_StartInstall;
    if (c == '9')
        return A_LaunchGame;
    if (c == '0')
        return A_Exit;
    return A_ShowMainMenuUninstalled;
}

nextaction_t MainMenuInstalled() {
    StartTerminalWindow();

    //printf("1) Verify BO3MacFix Install\n");
    printf("1) Update/reinstall BO3MacFix\n");
    printf("2) Uninstall BO3MacFix\n\n");
    printf("9) Launch game with fix\n\n");
    printf("0) Exit\n\n");

    printf("Select an option: ");

    char c = (char)getchar();
    if (c == '1')
        return A_StartVerify;
    if (c == '2')
        return A_StartUninstall;
    if (c == '9')
        return A_LaunchGame;
    if (c == '0')
        return A_Exit;
    return A_ShowMainMenuInstalled;
}

nextaction_t StartInstall() {
    char sourcePath[PATH_MAX] = {0};
    char targetPath[PATH_MAX] = {0};

    StartTerminalWindow();

    printf("Installing BO3MacFix...\n\n");

    printf("Backing up original launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    memset(targetPath, 0, PATH_MAX);
    strncat(targetPath, baseDirPath, PATH_MAX);
    strncat(targetPath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe_backup", PATH_MAX);
    if (rename(sourcePath, targetPath) != 0) {
        printf("failed (%i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Copying new launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "AppBundleExe", PATH_MAX);
    memset(targetPath, 0, PATH_MAX);
    strncat(targetPath, baseDirPath, PATH_MAX);
    strncat(targetPath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    int r = copyfile(sourcePath, targetPath, 0, COPYFILE_ALL);
    if (r != 0) {
        printf("failed (%i, %i).\n\n", r, errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Fixing permissions on launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    if (chmod(sourcePath, 0755) != 0) { // rwxr-xr-x
        printf("failed (chmod: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    if (removexattr(sourcePath, "com.apple.quarantine", 0) != 0 && errno != ENOATTR) {
        printf("failed (xattr: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Fixing permissions on BO3MacFix...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "BO3MacFix.dylib", PATH_MAX);
    if (chmod(sourcePath, 0644) != 0) { // rwxr-xr-x
        printf("failed (chmod: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    if (removexattr(sourcePath, "com.apple.quarantine", 0) != 0 && errno != ENOATTR) {
        printf("failed (xattr: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Installation complete!\n\n");

    printf("Press ENTER to continue.\n");
    getchar();
    return A_ShowMainMenuInstalled;
}

nextaction_t StartVerify() {
    char sourcePath[PATH_MAX] = {0};
    char targetPath[PATH_MAX] = {0};

    StartTerminalWindow();

    printf("Reinstalling BO3MacFix...\n\n");

    printf("Copying new launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "AppBundleExe", PATH_MAX);
    memset(targetPath, 0, PATH_MAX);
    strncat(targetPath, baseDirPath, PATH_MAX);
    strncat(targetPath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    int r = copyfile(sourcePath, targetPath, 0, COPYFILE_ALL);
    if (r != 0) {
        printf("failed (%i, %i).\n\n", r, errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Fixing permissions on launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    if (chmod(sourcePath, 0755) != 0) { // rwxr-xr-x
        printf("failed (chmod: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    if (removexattr(sourcePath, "com.apple.quarantine", 0) != 0 && errno != ENOATTR) {
        printf("failed (xattr: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Fixing permissions on BO3MacFix...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "BO3MacFix.dylib", PATH_MAX);
    if (chmod(sourcePath, 0644) != 0) { // rwxr-xr-x
        printf("failed (chmod: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    if (removexattr(sourcePath, "com.apple.quarantine", 0) != 0 && errno != ENOATTR) {
        printf("failed (xattr: %i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    printf("Installation complete!\n\n");

    printf("Press ENTER to continue.\n");
    getchar();
    return A_ShowMainMenuInstalled;
}

nextaction_t LaunchGame() {
    StartTerminalWindow();

    printf("Launching game with fix...\n\n");

    launch_game_native(baseDirPath);

    return A_Exit;
}

nextaction_t StartUninstall() {
    char sourcePath[PATH_MAX] = {0};
    char targetPath[PATH_MAX] = {0};

    StartTerminalWindow();

    printf("Uninstalling BO3MacFix...\n\n");

    printf("Deleting modified launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    remove(sourcePath); // we don't check error code on here because the rename will delete it anyway if it failed
    printf("done!\n");

    printf("Restoring original launcher...");
    memset(sourcePath, 0, PATH_MAX);
    strncat(sourcePath, baseDirPath, PATH_MAX);
    strncat(sourcePath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe_backup", PATH_MAX);
    memset(targetPath, 0, PATH_MAX);
    strncat(targetPath, baseDirPath, PATH_MAX);
    strncat(targetPath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    if (rename(sourcePath, targetPath) != 0) {
        printf("failed (%i).\n\n", errno);
        printf("Press ENTER to continue.\n");
        printf("You might have to repair game files in Steam...\n");
        getchar();
        return A_ShowMainMenuUninstalled;
    }
    remove(sourcePath);
    printf("done!\n");

    printf("Uninstallation complete!\n");
    printf("If you have any issues, verify game file integrity from within Steam.\n\n");

    printf("Press ENTER to continue.\n");
    getchar();
    return A_ShowMainMenuUninstalled;
}


int main(int argc, char **argv) {
    StartTerminalWindow();
    printf("Preparing...\n");
    nextaction_t act = A_ShowMainMenuUninstalled;

    // Get the path of the folder we're executing from
    const char *end_filename = filename_from_path(argv[0]);
    baseDirPath = strndup(argv[0], (size_t)(end_filename - argv[0]));

    // Get the path of the AppBundleExe
    char appPath[PATH_MAX] = {0};
    strncat(appPath, baseDirPath, PATH_MAX);
    strncat(appPath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe", PATH_MAX);
    // make sure the appbundleexe exists
    struct stat appSt;
    if (stat(appPath, &appSt) != 0) {
        act = A_ShowGameDirectoryError;
    } else { // make sure our dylib exists as well
        char dylibPath[PATH_MAX] = {0};
        strncat(dylibPath, baseDirPath, PATH_MAX);
        strncat(dylibPath, "BO3MacFix.dylib", PATH_MAX);
        if (stat(dylibPath, &appSt) != 0)
            act = A_ShowFixDirectoryError;
    }

    // check if the backup exists, assume the mod's already installed here
    memset(appPath, 0, PATH_MAX);
    strncat(appPath, baseDirPath, PATH_MAX);
    strncat(appPath, "../CoDBlkOps3.app/Contents/MacOS/AppBundleExe_backup", PATH_MAX);
    if (stat(appPath, &appSt) == 0) {
        act = A_ShowMainMenuInstalled;
    }

    while (act != A_Exit) {
        switch (act) {
            case A_ShowMainMenuUninstalled:
                act = MainMenuUninstalled();
                break;
            case A_ShowMainMenuInstalled:
                act = MainMenuInstalled();
                break;
            case A_ShowGameDirectoryError:
                act = Error_NotInRightDirectory();
                break;
            case A_ShowFixDirectoryError:
                act = Error_CantFindFixDirectory();
                break;
            case A_StartInstall:
                act = StartInstall();
                break;
            case A_StartVerify:
                act = StartVerify();
                break;
            case A_StartUninstall:
                act = StartUninstall();
                break;
            case A_ClearNavmesh:
                printf("A_ClearNavmesh\n");
                act = A_Exit;
                break;
            case A_LaunchGame:
                act = LaunchGame();
                break;
            default:
                act = A_Exit;
                break;
        }
        getchar();
    }

    printf("Close the window to exit.\n");

    free(baseDirPath);

    return 0;
}