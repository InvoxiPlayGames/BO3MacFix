#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mman.h>

#include "dobby.h"

// Mac v1.0.0.0, in-game v98, executable 5e42371b6a16f2b11e637f48e38554625570aa9f
#define ADDR_HKAI_InitMapData 0xe09aa9 // func
#define ADDR_load_workshop_texture 0xe4a26c // func
#define ADDR_build_usermods_path 0xe49938 // func
#define ADDR_DB_FindXAssetHeader 0x1345b12 // func
#define ADDR_Com_Error 0xe71e51 // func
#define ADDR_Live_SystemInfo 0x129d650 // func
#define ADDR_Dvar_SetFromStringByName 0xfa3386 // func
#define ADDR_g_workshopMapId 0x3087dac // char[]
#define ADDR_g_emuPath 0x4ae9c00 // char[]
#define ADDR_sSessionModeState 0x6939a14 // int
#define ADDR_Scr_ErrorInternalPatch 0x11a238e // mid-func

#define Version_Prefix "[BO3MacFix v1.0]"
#define Error_Prefix "^7[^6BO3MacFix^7] "

// cod2map64 path relative to Assets folder in the .app bundle
#define C2M_Path "../../../BO3MacFix/cod2map/cod2map64.exe"

#define Wine_Path_ASi "/opt/homebrew/bin/wine"
#define Wine_Path_Intel "/usr/local/bin/wine"

#define ASSET_TYPE_NAVMESH 0x61
#define ASSET_TYPE_NAVVOLUME 0x62
typedef struct _NavMeshData {
    char *name;
    int navMeshSettingsBufferSize;
    uint8_t *navMeshSettingsBuffer;
    int userEdgePairsBufferSize;
    uint8_t *userEdgePairsBuffer;
    int clusterGraphBufferSize;
    uint8_t *clusterGraphBuffer;
    int mediatorBufferSize;
    uint8_t *mediatorBuffer;
    int metadataBufferSize;
    uint8_t *metadataBuffer;
    int clearanceCacheSeederBufferSize;
    uint8_t *clearanceCacheSeederBuffer;
} NavMeshData;
typedef struct _NavVolumeData {
    char *name;
    int smallNavVolumeSettingsBufferSize;
    uint8_t *smallNavVolumeSettingsBuffer;
    int smallNavVolumeMediatorBufferSize;
    uint8_t *smallNavVolumeMediatorBuffer;
    int bigNavVolumeSettingsBufferSize;
    uint8_t *bigNavVolumeSettingsBuffer;
    int bigNavVolumeMediatorBufferSize;
    uint8_t *bigNavVolumeMediatorBuffer;
} NavVolumeData;

typedef struct _SESSIONMODE_STATE {
    int32_t mode : 4;
    int32_t campaignMode : 2;
    int32_t network : 4;
    int32_t matchmaking : 4;
    int32_t game : 4;
    int32_t viewport : 4;
} SESSIONMODE_STATE;

typedef struct _clumpheader {
    int bool_hasNavVolume; // 0x0

    int navMeshSettingsBufferSize; // 0x4
    int userEdgePairsBufferSize; // 0x8
    int clusterGraphBufferSize; // 0xC
    int mediatorBufferSize; // 0x10
    int metadataBufferSize; // 0x14
    int clearanceCacheSeederBufferSize; // 0x18

    int smallNavVolumeSettingsBufferSize; // 0x1C
    int smallNavVolumeMediatorBufferSize; // 0x20
    int bigNavVolumeSettingsBufferSize; // 0x24
    int bigNavVolumeMediatorBufferSize; // 0x28

    int unused; // 0x2C
} clumpheader;

// The base address of the CoDBlkOps3_Exe executable
uint64_t game_base_address = 0x0;

static const char *GetGamemodeString() {
    SESSIONMODE_STATE modeState = *(SESSIONMODE_STATE *)(game_base_address + ADDR_sSessionModeState);
    switch (modeState.mode) {
        case 0:
            return "zm";
        case 1:
            return "mp";
        case 2:
            return "cp";
        default:
            return "core";
    }
}

// Gets the base address of the first loaded dyld image (the game binary)
static inline uint64_t get_base_address() {
    // getsegbyname is deprecated since 13.0, is there an alternative?
    const struct segment_command_64* segment = getsegbyname("__TEXT");
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    return segment->vmaddr + slide;
}

// Gets just the filename of a given path
static const char *filename_from_path(const char *path) {
    size_t len = strlen(path);
    for (size_t i = len - 1; i > 0; i--) {
        if (path[i] == '/')
            return path + i + 1;
    }
    return path;
}

// Detects if the process is running under Rosetta2 or not (find out if we're on ASi or Intel)
bool rosetta_translated_process() {
    int ret = 0;
    size_t size = sizeof(ret);
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1)
        return false;
    return ret;
}

static void(*build_usermods_path)(const char *filename, const char *extension, size_t out_size, char *out_path, int type_maybe, char *workshop_id);
static void *(*DB_FindXAssetHeader)(int type, char *name, bool errorIfMissing, int waitTime);
static void (*Dvar_SetFromStringByName)(const char *dvarName, const char *string, bool createIfMissing);
static void (*Com_Error)(char *file, int line, int code, char *fmt, ...);

// Aspyr's png to texture conversion code is buggy and always crashes.
// Return no texture so we don't crash upon load.
install_hook_name(load_workshop_texture, void *, const char *path) {
    return NULL;
}

install_hook_name(Live_SystemInfo, bool, int controllerIndex, int info, char *outputString, int outputLen) {
    bool r = orig_Live_SystemInfo(controllerIndex, info, outputString, outputLen);
    if (info == 0 && r == true) {
        char newStr[200];
        snprintf(newStr, 200, Version_Prefix " %s", outputString);
        strncpy(outputString, newStr, outputLen);
    }
    return r;
}

// Initialises the navmesh and navvolume for the map.
// If we haven't made conversions already, call our conversion tool's binary.
install_hook_name(HKAI_InitMapData, void, const char *mapname, char restart) {
    printf("HKAI_InitMapData for %s\n", mapname);

    char physicsPath[0x200];
    snprintf(physicsPath, sizeof(physicsPath), "Physics/%s_navmesh.hkt", mapname);
    if (access(physicsPath, F_OK) != 0) {
        char full_path[0x200];
        build_usermods_path(mapname, ".ff", 0x200, full_path, 2, (char *)(game_base_address + ADDR_g_workshopMapId));
        printf("map fastfile is '%s'\n", full_path);

        char xasset_path[0x200];
        snprintf(xasset_path, 0x200, "maps/%s/%s_navmesh", GetGamemodeString(), mapname);
        NavMeshData *old_navmesh = DB_FindXAssetHeader(0x61, xasset_path, 1, -1);
        if (old_navmesh == NULL)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "Failed to load '^3%s^7' for map '^2%s^7'.", xasset_path, mapname);
    
        snprintf(xasset_path, 0x200, "maps/%s/%s_navvolume", GetGamemodeString(), mapname);
        NavVolumeData *old_navvolume = DB_FindXAssetHeader(0x62, xasset_path, 0, -1);

        // set out a header for the conversion clump
        clumpheader header;
        memset(&header, 0, sizeof(clumpheader));
        header.navMeshSettingsBufferSize = old_navmesh->navMeshSettingsBufferSize;
        header.userEdgePairsBufferSize = old_navmesh->userEdgePairsBufferSize;
        header.clusterGraphBufferSize = old_navmesh->clusterGraphBufferSize;
        header.mediatorBufferSize = old_navmesh->mediatorBufferSize;
        header.metadataBufferSize = old_navmesh->metadataBufferSize;
        header.clearanceCacheSeederBufferSize = old_navmesh->clearanceCacheSeederBufferSize;
        if (old_navvolume != NULL) {
            header.bool_hasNavVolume = 1;
            header.smallNavVolumeSettingsBufferSize = old_navvolume->smallNavVolumeSettingsBufferSize;
            header.smallNavVolumeMediatorBufferSize = old_navvolume->smallNavVolumeMediatorBufferSize;
            header.bigNavVolumeSettingsBufferSize = old_navvolume->bigNavVolumeSettingsBufferSize;
            header.bigNavVolumeMediatorBufferSize = old_navvolume->bigNavVolumeMediatorBufferSize;
        }

        // write out the header and all the navmesh/navvolume data to the clump
        FILE *clumpfile = fopen("convert.clump", "wb");
        if (clumpfile == NULL)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "Failed to create map conversion clump file.");
        fwrite(&header, sizeof(clumpheader), 1, clumpfile);
        fwrite(old_navmesh->navMeshSettingsBuffer, old_navmesh->navMeshSettingsBufferSize, 1, clumpfile);
        fwrite(old_navmesh->userEdgePairsBuffer, old_navmesh->userEdgePairsBufferSize, 1, clumpfile);
        fwrite(old_navmesh->clusterGraphBuffer, old_navmesh->clusterGraphBufferSize, 1, clumpfile);
        fwrite(old_navmesh->mediatorBuffer, old_navmesh->mediatorBufferSize, 1, clumpfile);
        fwrite(old_navmesh->metadataBuffer, old_navmesh->metadataBufferSize, 1, clumpfile);
        fwrite(old_navmesh->clearanceCacheSeederBuffer, old_navmesh->clearanceCacheSeederBufferSize, 1, clumpfile);
        if (old_navvolume != NULL) {
            fwrite(old_navvolume->smallNavVolumeSettingsBuffer, old_navvolume->smallNavVolumeSettingsBufferSize, 1, clumpfile);
            fwrite(old_navvolume->smallNavVolumeMediatorBuffer, old_navvolume->smallNavVolumeMediatorBufferSize, 1, clumpfile);
            fwrite(old_navvolume->bigNavVolumeSettingsBuffer, old_navvolume->bigNavVolumeSettingsBufferSize, 1, clumpfile);
            fwrite(old_navvolume->bigNavVolumeMediatorBuffer, old_navvolume->bigNavVolumeMediatorBufferSize, 1, clumpfile);
        }
        fclose(clumpfile);

        // call Wine with our cod2map64 binary, passing in the clump
        char cwd[1000];
        char wine_path[3000];
        getcwd(cwd, sizeof(cwd));
        if (rosetta_translated_process())
            snprintf(wine_path, sizeof(wine_path), Wine_Path_ASi " " C2M_Path " -mac_navmesh \"Z:%s/convert.clump\" \"Z:%s/Physics/%s\"", cwd, cwd, mapname);
        else
            snprintf(wine_path, sizeof(wine_path), Wine_Path_Intel " " C2M_Path " -mac_navmesh \"Z:%s/convert.clump\" \"Z:%s/Physics/%s\"", cwd, cwd, mapname);
        printf("executing: %s\n", wine_path);
        unsetenv("DYLD_INSERT_LIBRARIES"); // stop steam from fucking with our wine
        FILE *convert = popen(wine_path, "r"); // can we log stdout easily from this?
        if (convert == NULL)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "Failed to start map conversion process for '^2%s^7'.", mapname);
        pclose(convert);
        printf("conversion complete!\n");

        // make sure we have a navmesh file generated
        if (access(physicsPath, F_OK) != 0)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "Failed to convert map '^2%s^7' to Mac navmesh format.", mapname);
    }

    orig_HKAI_InitMapData(mapname, restart);
}

// Entrypoint for the dylib
__attribute__((constructor)) static void dylib_main() {
    // make sure we're loading into the game process and nothing else
    // don't want to inject into defaults, Aspyr launcher or Steamwebhelper
    const char *image_path = _dyld_get_image_name(0);
    const char *image_name = filename_from_path(image_path);
    if (strcmp(image_name, "CoDBlkOps3_Exe") != 0)
        return;

    // get the base address of the binary
    game_base_address = get_base_address();
    printf("loaded into %s (0x%llx)!\n", image_name, game_base_address);

    // apply the hooks
    install_hook_HKAI_InitMapData((void *)(game_base_address + ADDR_HKAI_InitMapData));
    install_hook_load_workshop_texture((void *)(game_base_address + ADDR_load_workshop_texture));
    install_hook_Live_SystemInfo((void *)(game_base_address + ADDR_Live_SystemInfo));

    // set up functions that we later call
    build_usermods_path = (void *)(game_base_address + ADDR_build_usermods_path);
    DB_FindXAssetHeader = (void *)(game_base_address + ADDR_DB_FindXAssetHeader);
    Com_Error = (void *)(game_base_address + ADDR_Com_Error);
    Dvar_SetFromStringByName = (void *)(game_base_address + ADDR_Dvar_SetFromStringByName);

    // disable setting a fatal error in Scr_ErrorInternal, Aspyr forces it to always be true
    uint8_t patch[1] = { 0x00 };
    DobbyCodePatch((void *)(game_base_address + ADDR_Scr_ErrorInternalPatch + 4), patch, 1);
}
