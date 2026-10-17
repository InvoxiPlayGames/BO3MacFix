#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "download.h"
#include "steam_launch.h"
#include "vdf.h"

#define Game_App_Id "311210"
#define Steam_Userdata_Suffix "/Library/Application Support/Steam/userdata"

static char *read_text_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    char *text = malloc((size_t)size + 1);
    if (text == NULL || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

static bool write_text_file_atomically(const char *path, const char *text) {
    char temporary_path[PATH_MAX];
    snprintf(temporary_path, sizeof(temporary_path), "%s.bo3macfix-new", path);
    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL)
        return false;
    size_t length = strlen(text);
    bool written = fwrite(text, 1, length, file) == length;
    written = fclose(file) == 0 && written;
    if (!written || rename(temporary_path, path) != 0) {
        unlink(temporary_path);
        return false;
    }
    return true;
}

static void keep_first_backup(const char *path, const char *text) {
    char backup_path[PATH_MAX];
    snprintf(backup_path, sizeof(backup_path), "%s.bo3macfix-backup", path);
    if (access(backup_path, F_OK) != 0)
        write_text_file_atomically(backup_path, text);
}

static int for_each_steam_config(bool (*visit)(const char *config_path, void *context), void *context) {
    const char *home = getenv("HOME");
    if (home == NULL)
        return -1;
    char userdata_path[PATH_MAX];
    snprintf(userdata_path, sizeof(userdata_path), "%s" Steam_Userdata_Suffix, home);
    DIR *userdata = opendir(userdata_path);
    if (userdata == NULL)
        return -1;
    int visited = 0;
    struct dirent *entry;
    while ((entry = readdir(userdata)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        char config_path[PATH_MAX];
        snprintf(config_path, sizeof(config_path), "%s/%s/config/localconfig.vdf", userdata_path, entry->d_name);
        if (access(config_path, R_OK) != 0)
            continue;
        if (visit(config_path, context))
            visited++;
    }
    closedir(userdata);
    return visited;
}

typedef struct {
    const char *launch_options;
    int failures;
} UpdateContext;

static bool update_config(const char *config_path, void *context) {
    UpdateContext *update = context;
    char *text = read_text_file(config_path);
    if (text == NULL) {
        update->failures++;
        return false;
    }
    VdfResult result;
    char *updated = vdf_set_launch_options(text, Game_App_Id, update->launch_options, &result);
    bool success = result == Vdf_Result_Unchanged;
    if (result == Vdf_Result_Changed) {
        keep_first_backup(config_path, text);
        success = write_text_file_atomically(config_path, updated);
    }
    if (!success)
        update->failures++;
    free(updated);
    free(text);
    return success;
}

static bool config_has_launch_options(const char *config_path, void *context) {
    const char *expected = context;
    char *text = read_text_file(config_path);
    if (text == NULL)
        return false;
    char value[PATH_MAX * 2];
    bool matches = vdf_get_launch_options(text, Game_App_Id, value, sizeof(value)) && strcmp(value, expected) == 0;
    free(text);
    return matches;
}

void steam_launch_options_value(const char *wrapper_path, char *out_value, size_t out_value_size) {
    snprintf(out_value, out_value_size, "\"%s\" %%command%%", wrapper_path);
}

bool steam_is_running(void) {
    char *const pgrep_argv[] = { "/usr/bin/pgrep", "-x", "steam_osx", NULL };
    return run_command_quietly(pgrep_argv) == 0;
}

bool steam_quit(void) {
    char *const quit_argv[] = { "/usr/bin/osascript", "-e", "tell application \"Steam\" to quit", NULL };
    run_command_quietly(quit_argv);
    for (int second = 0; second < 60; second++) {
        if (!steam_is_running())
            return true;
        sleep(1);
    }
    return false;
}

void steam_open(void) {
    char *const open_argv[] = { "/usr/bin/open", "-a", "Steam", NULL };
    run_command_quietly(open_argv);
}

int steam_set_launch_options(const char *launch_options, int *failures) {
    UpdateContext context = { launch_options, 0 };
    int updated = for_each_steam_config(update_config, &context);
    *failures = context.failures;
    return updated;
}

int steam_count_configs_with_launch_options(const char *launch_options) {
    return for_each_steam_config(config_has_launch_options, (void *)launch_options);
}

void copy_to_clipboard(const char *text) {
    FILE *pbcopy = popen("/usr/bin/pbcopy", "w");
    if (pbcopy == NULL)
        return;
    fputs(text, pbcopy);
    pclose(pbcopy);
}
