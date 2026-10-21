// macOS replacement for T7Overcharged (https://github.com/JariKCoding/T7Overcharged),
// a Windows DLL that workshop mods such as All-around Enhancement load through Lua.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "havok.h"
#include "offsets.h"
#include "t7overcharged.h"
#include "t7overcharged_lua.h"
#include "utilities.h"

// defined in bo3macfix.c
extern uint64_t game_base_address;
void lua_correct_path(const char *in_path, char *out_path, size_t out_path_len);
// defined in bo3macnative.m
char *copy_clipboard_text();
void set_clipboard_text(const char *text);

#define ASSET_TYPE_COUNT 0x6B
#define MAX_ASSET_LIMIT 100000

typedef struct _XAssetPool {
    void *pool;
    uint32_t itemSize;
    int32_t itemCount;
    bool isSingleton;
    int32_t itemAllocCount;
    void *freeHead;
} XAssetPool;
_Static_assert(sizeof(XAssetPool) == 0x20, "XAssetPool must match the game's layout");

// the folder the mod's T7Overcharged DLL is in
static char dll_dir[4096];

bool t7o_is_overcharged_dll(const char *path) {
    const char *name = filename_from_path(path);
    return strcasecmp(name, "T7Overcharged.ff") == 0 || strcasecmp(name, "T7Overcharged.dll") == 0;
}

// Raises an asset pool's limit by chaining a new block of entries onto its free list.
static void extend_asset_pool(int type, const char *name, int new_limit) {
    if (type < 0 || type >= ASSET_TYPE_COUNT || new_limit > MAX_ASSET_LIMIT)
        return;
    // make sure the mod means the same asset type as us
    const char *pool_name = ((const char **)(game_base_address + ADDR_g_assetNames))[type];
    if (strcmp(pool_name, name) != 0) {
        printf("T7Overcharged: asset type %i is '%s', not '%s'\n", type, pool_name, name);
        return;
    }
    XAssetPool *pool = (XAssetPool *)(game_base_address + ADDR_DB_XAssetPool) + type;
    if (pool->isSingleton || pool->itemSize < sizeof(void *) || new_limit <= pool->itemCount)
        return;
    int added = new_limit - pool->itemCount;
    uint8_t *block = calloc(added, pool->itemSize);
    if (block == NULL)
        return;
    for (int i = 0; i < added; i++) {
        void **entry = (void **)(block + (size_t)i * pool->itemSize);
        *entry = (i + 1 < added) ? block + (size_t)(i + 1) * pool->itemSize : pool->freeHead;
    }
    pool->freeHead = block;
    printf("T7Overcharged: raised %s limit from %i to %i\n", name, pool->itemCount, new_limit);
    pool->itemCount = new_limit;
}

// Applies the mod's T7Overcharged/assetlimits.txt, made of "<type> <name> <default limit> <new limit>" lines.
static void apply_asset_limits() {
    char path[4200];
    snprintf(path, sizeof(path), "%s/T7Overcharged/assetlimits.txt", dll_dir);
    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return;
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        int type, default_limit, new_limit;
        char name[64];
        if (sscanf(line, "%i %63s %i %i", &type, name, &default_limit, &new_limit) == 4)
            extend_asset_pool(type, name, new_limit);
    }
    fclose(fp);
}

static int lua_create_directory(lua_State *s) {
    char path[4096];
    lua_correct_path(hks_check_string(s, 1), path, sizeof(path));
    // like mkdir -p, create any missing parent folders too
    bool ok = true;
    for (char *p = path + 1; ok && *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            ok = mkdir(path, 0755) == 0 || errno == EEXIST;
            *p = '/';
        }
    }
    if (ok)
        ok = mkdir(path, 0755) == 0 || errno == EEXIST;
    hks_push_boolean(s, ok);
    return 1;
}

static int lua_directory_exists(lua_State *s) {
    char path[4096];
    lua_correct_path(hks_check_string(s, 1), path, sizeof(path));
    struct stat st;
    hks_push_boolean(s, stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    return 1;
}

static int lua_clipboard_get(lua_State *s) {
    char *text = copy_clipboard_text();
    hks_push_string(s, text != NULL ? text : "");
    free(text);
    return 1;
}

static int lua_clipboard_set(lua_State *s) {
    set_clipboard_text(hks_check_string(s, 1));
    return 0;
}

static int lua_print(lua_State *s) {
    printf("%s\n", hks_check_string(s, 1));
    return 0;
}

// Our version of T7Overcharged's init(): raises the mod's asset limits, then sets up the Lua libraries it provides.
static int t7o_init(lua_State *s) {
    // the stack can be reallocated while our Lua runs, so remember the top as an index
    ptrdiff_t top = HKS_TOP(s) - HKS_BASE(s);
    apply_asset_limits();
    if (hks_load_source(s, (const char *)t7overcharged_lua, t7overcharged_lua_len, "=BO3MacFix T7Overcharged")) {
        hks_push_cfunction(s, lua_create_directory, "CreateDirectory");
        hks_push_cfunction(s, lua_directory_exists, "DirectoryExists");
        hks_push_cfunction(s, lua_clipboard_get, "ClipboardGet");
        hks_push_cfunction(s, lua_clipboard_set, "ClipboardSet");
        hks_push_cfunction(s, lua_print, "Print");
        hks_pcall(s, 5, 0);
    }
    HKS_TOP(s) = HKS_BASE(s) + top;
    return 0;
}

void t7o_push_init(lua_State *s, const char *dll_path) {
    strncpy(dll_dir, dll_path, sizeof(dll_dir) - 1);
    char *last_slash = strrchr(dll_dir, '/');
    if (last_slash != NULL)
        *last_slash = '\0';
    hks_push_cfunction(s, t7o_init, "init");
}
