#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/mman.h>

#include "bo3macnative.h"
#include "codhavoktypes.h"
#include "dobby.h"
#include "utilities.h"
#include "offsets.h"

#define Version_Prefix "[BO3MacFix v1.0]"
#define Error_Prefix "^7[^6BO3MacFix^7] "

// cod2map64 path relative to Assets folder in the .app bundle
#define C2M_Path "../../../BO3MacFix/cod2map/cod2map64.exe"

#define Wine_Path_ASi "/opt/homebrew/bin/wine"
#define Wine_Path_Intel "/usr/local/bin/wine"

typedef struct _SESSIONMODE_STATE {
    int32_t mode : 4;
    int32_t campaignMode : 2;
    int32_t network : 4;
    int32_t matchmaking : 4;
    int32_t game : 4;
    int32_t viewport : 4;
} SESSIONMODE_STATE;

// The base address of the CoDBlkOps3_Exe executable
uint64_t game_base_address = 0x0;
// The hashed network password used for network encryption.
uint64_t network_password = 0;

// Returns the current gamemode string as used in paths
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

// game functions we call upon
static void(*build_usermods_path)(const char *filename, const char *extension, size_t out_size, char *out_path, int type_maybe, char *workshop_id);
static void *(*DB_FindXAssetHeader)(int type, char *name, bool errorIfMissing, int waitTime);
static void (*Dvar_SetFromStringByName)(const char *dvarName, const char *string, bool createIfMissing);
static void (*Com_Error)(char *file, int line, int code, char *fmt, ...);
static uint16_t (*Sys_Checksum)(uint8_t *payload, int paylen);
static uint8_t (*MSG_ReadByte)(void *msg);
static void (*MSG_WriteByte)(void *msg, uint8_t c);

// Makes sure incoming checksums have our password
install_hook_name(Sys_VerifyPacketChecksum, int, uint8_t *payload, int paylen) {
    if (paylen >= 2) {
        int newlen = paylen - 2;
        uint16_t payload_checksum = *(uint16_t *)(payload + newlen);
        uint16_t expected_checksum = (uint16_t)network_password ^ Sys_Checksum(payload, newlen);
        if (payload_checksum == expected_checksum)
            return newlen;
        printf("Mismatched packet checksum! (%04x != %04x)\n", payload_checksum, expected_checksum);
    }
    return -1;
}

// Adds our password onto checksums
install_hook_name(Sys_CheckSumPacketCopy, uint16_t, uint8_t *dest, uint8_t *payload, int paylen) {
    uint16_t ret = orig_Sys_CheckSumPacketCopy(dest, payload, paylen);
    ret ^= (uint16_t)network_password;
    return ret;
}

// Adds password into lobby messages
install_hook_name(LobbyMsgRW_PrepReadMsg, bool, void *msg) {
    bool orig = orig_LobbyMsgRW_PrepReadMsg(msg);
    if (orig && network_password != 0) {
        if (MSG_ReadByte(msg) == (uint8_t)(network_password & 0xFF0000 >> 16) &&
            MSG_ReadByte(msg) == (uint8_t)(network_password & 0xFF000000 >> 24))
            return true;
        return false;
    }
    return orig;
}
install_hook_name(LobbyMsgRW_PrepWriteMsg, bool, void *lobbyMsg, uint8_t *data, int length, int msgType) {
    bool orig = orig_LobbyMsgRW_PrepWriteMsg(lobbyMsg, data, length, msgType);
    if (orig && network_password != 0) {
        MSG_WriteByte(lobbyMsg, (uint8_t)(network_password & 0xFF0000 >> 16));
        MSG_WriteByte(lobbyMsg, (uint8_t)(network_password & 0xFF000000 >> 24));
    }
    return orig;
}

// Aspyr's png to texture conversion code is buggy and always crashes.
// Return no texture so we don't crash upon load.
install_hook_name(load_workshop_texture, void *, const char *path) {
    return NULL;
}

// Prints out logs to stdout that would normally go in the logging window
install_hook_name(someLoggingFunction, void, int bla, int bla2, char *str, int bla3) {
    printf("%s", str);
    orig_someLoggingFunction(bla, bla2, str, bla3);
    return;
}

// Hooks some early window creation function to set the window title to ours
install_hook_name(someWindowCreationFunction, bool, void *r3, int r4) {
    bool r = orig_someWindowCreationFunction(r3, r4);
    set_window_title("Call of Duty: Black Ops III (with BO3MacFix)");
    return r;
}

// Adds the version of our mod to the version info in the corner
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

// patches all instance of the old network version with the new network version
static void network_version_patch() {
    uint16_t original_network_version = 18530;
    uint16_t patched_network_version = 18532;
    // all the places the original network version is hardcoded
    uint32_t addresses[] = {
        0x00E078E4, 0x00E181D3, 0x00E83E4E, 0x00EB8389, 0x00EB91A9,
        0x00F988A0, 0x0104E0CB, 0x0105973D, 0x01059844, 0x0105C368,
        0x0105C5DC, 0x011F7C3E, 0x011F7C43, 0x011F7C49, 0x0120B547,
        0x0120B56A, 0x0123244C, 0x0126F78C, 0x0126F897, 0x0126F8BC,
        0x0126F90D, 0x0126F96F, 0x012D35DA, 0x013B5C1D, 
    };
    int num_addrs = sizeof(addresses) / sizeof(addresses[0]);
    // enumerate through every one of these instances
    for (int i = 0; i < num_addrs; i++) {
        // find the offset of the old number in the mov instruction
        int o = 0;
        bool found = false;
        for (o = 0; o < 16; o++)  {
            if (memcmp((void *)(game_base_address + addresses[i] + o), &original_network_version, sizeof(uint16_t)) == 0) {
                found = true;
                break;
            }
        }
        if (found == true) {
            DobbyCodePatch((void *)(game_base_address + addresses[i] + o), (uint8_t *)&patched_network_version, sizeof(uint16_t));
        } else {
            printf("failed to find network patch for %08x\n", addresses[i]);
        }
    }
    // patch the version reported by Lua_CoD_LuaCall_GetProtocolVersion
    uint32_t new_lua_version = 0x4690C800;
    DobbyCodePatch((void *)(game_base_address + ADDR_Lua_CoD_LuaCall_GetProtocolVersion_Inst + 3), (uint8_t *)&new_lua_version, sizeof(uint32_t));
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
    install_hook_someLoggingFunction((void *)(game_base_address + ADDR_someLoggingFunction));
    install_hook_someWindowCreationFunction((void *)(game_base_address + ADDR_someWindowCreationFunction));
    install_hook_Sys_VerifyPacketChecksum((void *)(game_base_address + ADDR_Sys_VerifyPacketChecksum));
    install_hook_Sys_CheckSumPacketCopy((void *)(game_base_address + ADDR_Sys_CheckSumPacketCopy));
    install_hook_LobbyMsgRW_PrepReadMsg((void *)(game_base_address + ADDR_LobbyMsgRW_PrepReadMsg));
    install_hook_LobbyMsgRW_PrepWriteMsg((void *)(game_base_address + ADDR_LobbyMsgRW_PrepWriteMsg));

    // set up functions that we later call
    build_usermods_path = (void *)(game_base_address + ADDR_build_usermods_path);
    DB_FindXAssetHeader = (void *)(game_base_address + ADDR_DB_FindXAssetHeader);
    Com_Error = (void *)(game_base_address + ADDR_Com_Error);
    Dvar_SetFromStringByName = (void *)(game_base_address + ADDR_Dvar_SetFromStringByName);
    Sys_Checksum = (void *)(game_base_address + ADDR_Sys_Checksum);
    MSG_ReadByte = (void *)(game_base_address + ADDR_MSG_ReadByte);
    MSG_WriteByte = (void *)(game_base_address + ADDR_MSG_WriteByte);

    // disable setting a fatal error in Scr_ErrorInternal, Aspyr forces it to always be true
    uint8_t error_patch[1] = { 0x00 };
    DobbyCodePatch((void *)(game_base_address + ADDR_Scr_ErrorInternalPatch + 4), error_patch, 1);

    // fix a crash bug
    uint8_t ret[1] = { 0x3C };
    DobbyCodePatch((void *)(game_base_address + ADDR_Lua_CmdParseArgs), ret, sizeof(ret));

    // set the network password rather than having it be 0 - should probably have a gui to set it
    const char *password_text = getenv("BO3MACFIX_NETWORKPASSWORD");
    if (password_text != NULL) {
        network_password = gscu_canon_hash64(password_text);

        // nop out where dw sets the Content-Length header, fixes a bug with modern macOS failing to log in online
        uint8_t curl_nops[(ADDR_Curl_ContentLengthSetEnd - ADDR_Curl_ContentLengthSetStart)];
        memset(curl_nops, 0x90, sizeof(curl_nops));
        DobbyCodePatch((void *)(game_base_address + ADDR_Curl_ContentLengthSetStart), curl_nops, sizeof(curl_nops));
    }

    // patches the network version to match Windows, need to make the protocol identical
    //network_version_patch();
}
