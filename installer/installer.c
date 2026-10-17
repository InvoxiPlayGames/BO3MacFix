#include <limits.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "ansi.h"
#include "assets.h"
#include "download.h"
#include "steam_launch.h"
#include "wine.h"

#define Installer_Version "1.3-unofficial"

static char fixDirectory[PATH_MAX];
static char gameMacosDirectory[PATH_MAX];
static char wrapperPath[PATH_MAX];
static char launchOptions[PATH_MAX * 2];

typedef enum _nextaction_t {
    A_Exit,
    A_ShowMainMenuUninstalled,
    A_ShowMainMenuInstalled,
    A_ShowGameDirectoryError,
    A_ShowFixDirectoryError,
    A_StartInstall,
    A_StartUninstall,
    A_LaunchGame,
} nextaction_t;

static void StartTerminalWindow() {
    printf(TmTitle "BO3MacFix Installer" TmEnd);
    printf(TmClear TmStartPos);
    printf(TmBold TmUnderline "BO3MacFix Installer v" Installer_Version "\n\n" TmReset);
}

static void WaitForEnter() {
    printf("Press ENTER to continue.\n");
    getchar();
}

static bool AskYesNo(const char *question) {
    printf("%s [y/N]: ", question);
    char answer[16] = {0};
    if (fgets(answer, sizeof(answer), stdin) == NULL)
        return false;
    return answer[0] == 'y' || answer[0] == 'Y';
}

static bool ResolvePaths() {
    char executablePath[PATH_MAX];
    uint32_t executablePathSize = sizeof(executablePath);
    char resolvedPath[PATH_MAX];
    if (_NSGetExecutablePath(executablePath, &executablePathSize) != 0 || realpath(executablePath, resolvedPath) == NULL)
        return false;
    char *lastSlash = strrchr(resolvedPath, '/');
    if (lastSlash == NULL)
        return false;
    lastSlash[1] = '\0';
    strlcpy(fixDirectory, resolvedPath, sizeof(fixDirectory));
    snprintf(wrapperPath, sizeof(wrapperPath), "%slaunch-bo3macfix", fixDirectory);
    char gameFolder[PATH_MAX];
    snprintf(gameFolder, sizeof(gameFolder), "%s..", fixDirectory);
    char resolvedGameFolder[PATH_MAX];
    if (realpath(gameFolder, resolvedGameFolder) == NULL)
        return false;
    snprintf(gameMacosDirectory, sizeof(gameMacosDirectory), "%s/CoDBlkOps3.app/Contents/MacOS/", resolvedGameFolder);
    steam_launch_options_value(wrapperPath, launchOptions, sizeof(launchOptions));
    return true;
}

static bool FileExists(const char *directory, const char *name) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", directory, name);
    return access(path, F_OK) == 0;
}

static bool LegacyLauncherInstalled() {
    return FileExists(gameMacosDirectory, "AppBundleExe_backup");
}

static bool RestoreOriginalLauncher() {
    if (!LegacyLauncherInstalled())
        return true;
    printf("Restoring the original game launcher from an older BO3MacFix...");
    char backupPath[PATH_MAX];
    char launcherPath[PATH_MAX];
    snprintf(backupPath, sizeof(backupPath), "%sAppBundleExe_backup", gameMacosDirectory);
    snprintf(launcherPath, sizeof(launcherPath), "%sAppBundleExe", gameMacosDirectory);
    if (rename(backupPath, launcherPath) != 0) {
        printf("failed (%i).\nVerify the game files in Steam, then run this installer again.\n", errno);
        return false;
    }
    printf("done!\n");
    return true;
}

static bool PrepareFixFile(const char *name, mode_t mode) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", fixDirectory, name);
    if (chmod(path, mode) != 0) {
        printf("failed (chmod %s: %i).\n", name, errno);
        return false;
    }
    if (removexattr(path, "com.apple.quarantine", 0) != 0 && errno != ENOATTR) {
        printf("failed (xattr %s: %i).\n", name, errno);
        return false;
    }
    return true;
}

static bool SetUpAssets() {
    printf("Checking the Windows patch files and map tools...");
    if (assets_present(fixDirectory)) {
        printf("found.\n");
        return true;
    }
    printf("not found.\n");
    return assets_install(fixDirectory);
}

static void SetUpWine() {
    char bundledWinePath[PATH_MAX] = {0};
    printf("Checking Wine for custom maps...");
    if (wine_bundled_path(fixDirectory, bundledWinePath, sizeof(bundledWinePath)) || wine_system_install_present()) {
        printf("found.\n");
        return;
    }
    printf("not found.\n");
    if (wine_install_bundled(fixDirectory))
        printf("Wine is ready for custom map conversion.\n");
    else
        printf("Custom maps that need conversion will not load until Wine is installed. Run this installer again to retry.\n");
}

static void PrintManualLaunchOptions(bool removing) {
    printf("\nSet the Steam launch options by hand:\n");
    printf("  1. In Steam, right-click Call of Duty: Black Ops III and choose Properties.\n");
    if (removing) {
        printf("  2. Under General, clear the Launch Options field.\n\n");
        return;
    }
    copy_to_clipboard(launchOptions);
    printf("  2. Under General, paste this into Launch Options (it is on the clipboard):\n\n");
    printf("     %s\n\n", launchOptions);
}

static bool UpdateSteamLaunchOptions(const char *value) {
    bool removing = value == NULL;
    bool closedSteam = false;
    if (steam_is_running()) {
        printf("\nSteam must be closed to change the launch options of Black Ops III.\n");
        if (!AskYesNo("Close Steam now?")) {
            PrintManualLaunchOptions(removing);
            return false;
        }
        printf("Closing Steam...");
        if (!steam_quit()) {
            printf("failed. Steam is still running.\n");
            PrintManualLaunchOptions(removing);
            return false;
        }
        printf("done!\n");
        closedSteam = true;
    }

    printf(removing ? "Removing the Steam launch options..." : "Setting the Steam launch options...");
    int failures = 0;
    int accounts = steam_set_launch_options(value, &failures);
    bool success = accounts > 0 && failures == 0;
    if (success)
        printf("done for %i Steam account%s!\n", accounts, accounts == 1 ? "" : "s");
    else
        printf("failed (%i account%s changed, %i failed).\n", accounts < 0 ? 0 : accounts, accounts == 1 ? "" : "s", failures);

    if (closedSteam) {
        printf("Opening Steam again...\n");
        steam_open();
    }
    if (!success)
        PrintManualLaunchOptions(removing);
    return success;
}

static nextaction_t Error_NotInRightDirectory() {
    StartTerminalWindow();
    printf("The BO3MacFix installer wasn't able to find the Black Ops 3 game.\n");
    printf("Make sure you copied the BO3MacFix folder next to the Black Ops 3 .app file.\n\n");
    return A_Exit;
}

static nextaction_t Error_CantFindFixDirectory() {
    StartTerminalWindow();
    printf("The BO3MacFix installer wasn't able to find the BO3MacFix files.\n");
    printf("Make sure you copied the entire BO3MacFix folder next to the Black Ops 3 .app file.\n\n");
    return A_Exit;
}

static nextaction_t MainMenuUninstalled() {
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

static nextaction_t MainMenuInstalled() {
    StartTerminalWindow();
    printf("1) Update/reinstall BO3MacFix\n");
    printf("2) Uninstall BO3MacFix\n\n");
    printf("9) Launch game with fix\n\n");
    printf("0) Exit\n\n");
    printf("Select an option: ");
    char c = (char)getchar();
    if (c == '1')
        return A_StartInstall;
    if (c == '2')
        return A_StartUninstall;
    if (c == '9')
        return A_LaunchGame;
    if (c == '0')
        return A_Exit;
    return A_ShowMainMenuInstalled;
}

static nextaction_t StartInstall() {
    getchar();
    StartTerminalWindow();
    printf("Installing BO3MacFix...\n\n");

    if (!RestoreOriginalLauncher()) {
        WaitForEnter();
        return A_ShowMainMenuUninstalled;
    }

    printf("Fixing permissions on BO3MacFix...");
    if (!PrepareFixFile("BO3MacFix.dylib", 0644) || !PrepareFixFile("launch-bo3macfix", 0755)) {
        WaitForEnter();
        return A_ShowMainMenuUninstalled;
    }
    printf("done!\n");

    if (!SetUpAssets()) {
        printf("\nThe patch files could not be installed. Online play with Windows players needs them.\n");
        WaitForEnter();
        return A_ShowMainMenuUninstalled;
    }
    SetUpWine();

    bool launchOptionsSet = UpdateSteamLaunchOptions(launchOptions);

    printf("\nInstallation complete!\n");
    if (launchOptionsSet)
        printf("Start Black Ops III from Steam as usual. The game itself is not modified.\n\n");
    else
        printf("After you set the launch options, start Black Ops III from Steam as usual.\n\n");
    WaitForEnter();
    return A_ShowMainMenuInstalled;
}

static nextaction_t StartUninstall() {
    getchar();
    StartTerminalWindow();
    printf("Uninstalling BO3MacFix...\n\n");

    RestoreOriginalLauncher();
    UpdateSteamLaunchOptions(NULL);

    printf("\nUninstallation complete!\n");
    printf("You can delete the BO3MacFix folder. If you have any issues, verify game file integrity from within Steam.\n\n");
    WaitForEnter();
    return A_ShowMainMenuUninstalled;
}

static nextaction_t LaunchGame() {
    StartTerminalWindow();
    printf("Launching game with fix...\n\n");
    char *const wrapperArgv[] = { wrapperPath, NULL };
    if (run_command(wrapperArgv) != 0)
        printf("The game could not be started. Check %slaunch.log.\n\n", fixDirectory);
    return A_Exit;
}

int main(int argc, char **argv) {
    StartTerminalWindow();
    printf("Preparing...\n");

    nextaction_t act = A_ShowMainMenuUninstalled;
    if (!ResolvePaths() || !FileExists(gameMacosDirectory, "CoDBlkOps3_Exe"))
        act = A_ShowGameDirectoryError;
    else if (!FileExists(fixDirectory, "BO3MacFix.dylib") || !FileExists(fixDirectory, "launch-bo3macfix"))
        act = A_ShowFixDirectoryError;
    else if (LegacyLauncherInstalled() || steam_count_configs_with_launch_options(launchOptions) > 0)
        act = A_ShowMainMenuInstalled;

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
            case A_StartUninstall:
                act = StartUninstall();
                break;
            case A_LaunchGame:
                act = LaunchGame();
                break;
            default:
                act = A_Exit;
                break;
        }
    }

    printf("Close the window to exit.\n");
    return 0;
}
