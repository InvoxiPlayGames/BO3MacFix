// Modified by Mickael DE LA ROQUE on 2026-09-08 to add the unofficial
// custom-map stream-stall workaround.
#include <_stdio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <ctype.h>
#include <time.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include "dyld-interposing.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <sandbox.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDDevice.h>

#include "bo3macnative.h"
#include "codhavoktypes.h"
#include "cpu_tuning.h"
#include "dobby.h"
#include "gpu_tweaks.h"
#include "havok.h"
#include "utilities.h"
#include "offsets.h"
#include "prepatch.h"
#include "steam.h"
#include "stream_stall_recovery.h"
#include "t7overcharged.h"
#include "controllers.h"
#include "gc_bridge.h"

#include "versioninfo.h"

#ifdef MACFIX_DEBUG
#define Version_Prefix "[BO3MacFix DEBUG]"
#else
#define Version_Prefix "[BO3MacFix " MACFIX_VERSION "]"
#endif
#define Error_Prefix "^7[^6BO3MacFix^7] "

#define Basegame_Path "../../../"

#define Macfix_Path "../../../BO3MacFix"

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
// Whether to only allow friends to join the session.
bool friends_only = false;

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
static void (*MSG_WriteByte)(void *msg, int64_t c);

// Makes sure incoming checksums have our password
install_hook_name(Sys_VerifyPacketChecksum, int, uint8_t *payload, int paylen) {
    if (paylen >= 2) {
        int newlen = paylen - 2;
        uint16_t payload_checksum = *(uint16_t *)(payload + newlen);
        uint16_t expected_checksum = (uint16_t)network_password ^ Sys_Checksum(payload, newlen);
        if (payload_checksum == expected_checksum)
            return newlen;
#ifdef MACFIX_DEBUG
        printf("Mismatched packet checksum! (%04x != %04x)\n", payload_checksum, expected_checksum);
#endif
    }
    return -1;
}

// Adds our password onto checksums
install_hook_name(Sys_CheckSumPacketCopy, uint16_t, uint8_t *dest, uint8_t *payload, int paylen) {
    uint16_t ret = orig_Sys_CheckSumPacketCopy(dest, payload, paylen);
    ret ^= (uint16_t)network_password;
    return ret;
}

#define ZBR_PREFIX_BYTE (uint8_t)((network_password & 0xFF0000) >> 16)
#define ZBR_PREFIX_BYTE2 (uint8_t)((network_password & 0xFF000000) >> 24)

// Adds password into lobby messages
install_hook_name(LobbyMsgRW_PrepReadMsg, bool, void *msg) {
    bool orig = orig_LobbyMsgRW_PrepReadMsg(msg);
    if (orig && network_password != 0 && ZBR_PREFIX_BYTE != 0) {
        uint8_t prefbyte1 = MSG_ReadByte(msg);
        uint8_t prefbyte2 = MSG_ReadByte(msg);
        if (!ZBR_PREFIX_BYTE || (((prefbyte1 == ZBR_PREFIX_BYTE) && (prefbyte2 == ZBR_PREFIX_BYTE2))))
            return true;
#ifdef MACFIX_DEBUG
        printf("Failed prepreadmsg password (Got %02x %02x, expected %02x %02x)\n", prefbyte1, prefbyte2, ZBR_PREFIX_BYTE, ZBR_PREFIX_BYTE2);
#endif
        return false;
    }
    return orig;
}
install_hook_name(LobbyMsgRW_PrepWriteMsg, bool, void *lobbyMsg, uint8_t *data, int length, int msgType) {
    bool orig = orig_LobbyMsgRW_PrepWriteMsg(lobbyMsg, data, length, msgType);
    if (orig && network_password != 0) {
        MSG_WriteByte(lobbyMsg, ZBR_PREFIX_BYTE);
        MSG_WriteByte(lobbyMsg, ZBR_PREFIX_BYTE2);
    }
    return orig;
}

// the parts of Direct3D 11 that Aspyr's Metal backend implements, which we need to make textures
typedef struct _D3D11_TEXTURE2D_DESC {
    uint32_t Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CPUAccessFlags, MiscFlags;
} D3D11_TEXTURE2D_DESC;
typedef struct _D3D11_SUBRESOURCE_DATA {
    const void *pSysMem;
    uint32_t SysMemPitch, SysMemSlicePitch;
} D3D11_SUBRESOURCE_DATA;
typedef struct _D3D11_SHADER_RESOURCE_VIEW_DESC {
    uint32_t Format, ViewDimension, MostDetailedMip, MipLevels;
    uint64_t unused;
} D3D11_SHADER_RESOURCE_VIEW_DESC;
#define DXGI_FORMAT_R8G8B8A8_UNORM 28
#define D3D11_BIND_SHADER_RESOURCE 8
#define D3D11_SRV_DIMENSION_TEXTURE2D 4
#define COM_METHOD(obj, index, type) ((type)((*(void ***)(obj))[index]))
static void (*R_TrackTextureMemory)(void *view, D3D11_TEXTURE2D_DESC *desc, int category);

// Aspyr's png to texture conversion code is buggy and always crashes.
// Decode workshop images with ImageIO and make an RGBA texture from them ourselves instead.
install_hook_name(load_workshop_texture, void *, const char *path) {
    uint32_t width, height;
    uint8_t *pixels = decode_image_rgba(path, &width, &height);
    if (pixels == NULL)
        return NULL;
    void *device = *(void **)(game_base_address + ADDR_g_d3d11Device);
    D3D11_TEXTURE2D_DESC desc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 0, 0, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_SUBRESOURCE_DATA data = { pixels, width * 4, width * 4 * height };
    void *texture = NULL;
    void *view = NULL;
    if (device != NULL &&
        COM_METHOD(device, 5, int (*)(void *, D3D11_TEXTURE2D_DESC *, D3D11_SUBRESOURCE_DATA *, void **))(device, &desc, &data, &texture) >= 0 &&
        texture != NULL) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = { DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_SRV_DIMENSION_TEXTURE2D, 0, 0xFFFFFFFF, 0 };
        if (COM_METHOD(device, 7, int (*)(void *, void *, D3D11_SHADER_RESOURCE_VIEW_DESC *, void **))(device, texture, &view_desc, &view) < 0)
            view = NULL;
        if (view != NULL)
            R_TrackTextureMemory(view, &desc, 0);
        COM_METHOD(texture, 2, unsigned long (*)(void *))(texture);
    }
    free(pixels);
    printf("workshop image %s (%ux%u): %s\n", path, width, height, view != NULL ? "loaded" : "failed to create texture");
    return view;
}

// Prints out logs to stdout that would normally go in the logging window
install_hook_name(someLoggingFunction, void, int bla, int bla2, char *str, int bla3) {
#ifdef MACFIX_DEBUG
    printf("%s", str);
#endif
    orig_someLoggingFunction(bla, bla2, str, bla3);
    return;
}

/*
// Hooks some early window creation function to set the window title to ours
install_hook_name(someWindowCreationFunction, bool, void *r3, int r4) {
    bool r = orig_someWindowCreationFunction(r3, r4);
    set_window_title("Call of Duty: Black Ops III (with BO3MacFix)");
    return r;
}
*/

// Hooks the SDL window title setting function
bool added_menu_bar = false;
install_hook_name(SDL_SetWindowTitle, void, void *sdlWindow, const char *title)
{
    // verify the window title we're setting is the main game window
    if (strcmp("Call of Duty: Black Ops III", title) == 0)
    {
        title = "Call of Duty: Black Ops III (with BO3MacFix)";
        set_window_title(title);
        if (added_menu_bar == false) {
            //add_menu_bar();
            added_menu_bar = true;
        }
    }
    orig_SDL_SetWindowTitle(sdlWindow, title);
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

// Rejects instant messages from users who aren't friends if friends only is enabled.
install_hook_name(dwInstantDispatchMessage, bool, uint64_t userId, int controllerIndex, uint8_t *data, int datalen) {
    if (friends_only) {
        if (!steam_friends_with_user(userId) && // user isn't a friend
            userId != steam_id().id && // user isn't ourselves
            (userId >> 32) == 0x01100001) { // user is a steamid, not a guest or gameserver
#ifdef MACFIX_DEBUG
            printf("Rejecting instant message from %016llx due to friendsonly\n", userId);
#endif
            return true;
        }
    }
    if (datalen >= 2) {
        if (data[0] != 0x31) {
#ifdef MACFIX_DEBUG
            printf("Rejecting instant message from %016llx since it's invalid (1)\n", userId);
#endif
            return true;
        }
        if (data[1] == 0x65 || data[1] == 0x6D) {
#ifdef MACFIX_DEBUG
            printf("Rejecting instant message from %016llx since it's invalid (2)\n", userId);
#endif
            return true;
        }
    }
#ifdef MACFIX_DEBUG
    printf("Dispatching instant message from %016llx\n", userId);
#endif
    return orig_dwInstantDispatchMessage(userId, controllerIndex, data, datalen);
}

#define CLIENT_CONNECTION_STRIDE 0x1078
#define CLIENT_CONNECTION_STATE_OFFSET 0x8
#define LOCAL_CLIENT_STRIDE 0x197a40
#define CLIENT_GLOBAL_STRIDE 0x25780
#define SNAPSHOT_FLAGS_OFFSET 0x94
#define SNAPSHOT_TIME_OFFSET 0x98
#define NEW_SNAPSHOTS_OFFSET 0xb680
#define STREAM_READY_OFFSET 0x2454d
#define CONNECTION_STATE_PRIMED 10

static atomic_bool stream_recovery_eligible;
static StreamRecoveryLifecycle stream_recovery_lifecycle =
    STREAM_RECOVERY_LIFECYCLE_INITIALIZER;
static StreamStallMonitor stream_stall_monitor = {0};

static bool monotonic_milliseconds(uint64_t *milliseconds) {
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
        return false;

    *milliseconds = (uint64_t)timestamp.tv_sec * 1000ULL +
        (uint64_t)timestamp.tv_nsec / 1000000ULL;
    return true;
}

static bool db_load_stages_complete(void) {
    int stage_count = *(int *)(game_base_address + ADDR_DB_LOAD_STAGE_COUNT);
    if (stage_count < 1 || stage_count > 4)
        return false;

    int *totals = (int *)(game_base_address + ADDR_DB_LOAD_TOTALS);
    int *completed = (int *)(game_base_address + ADDR_DB_LOAD_COMPLETED);
    bool saw_work = false;

    for (int i = 0; i < stage_count; ++i) {
        if (totals[i] > 0)
            saw_work = true;
        if (completed[i] < totals[i])
            return false;
    }

    return saw_work;
}

static void begin_stream_recovery_load(bool eligible) {
    atomic_store(&stream_recovery_eligible, eligible);
    stream_recovery_begin_load(&stream_recovery_lifecycle);
}

install_hook_name(DB_GetStreamFraction, float, void) {
    float fraction = orig_DB_GetStreamFraction();
    if (stream_recovery_is_complete(&stream_recovery_lifecycle))
        return 1.0f;
    return fraction;
}

static void reset_stream_stall_observation(void) {
    if (stream_stall_monitor.observing)
        stream_stall_recovery_reset(&stream_stall_monitor);
}

static void update_stream_stall_recovery(int localClientNum) {
    static unsigned int observed_generation = 0;
    unsigned int generation = stream_recovery_current_generation(
        &stream_recovery_lifecycle);
    if (observed_generation != generation) {
        stream_stall_recovery_reset(&stream_stall_monitor);
        observed_generation = generation;
    }

    if (!atomic_load(&stream_recovery_eligible) ||
        stream_recovery_is_complete(&stream_recovery_lifecycle))
        return;

    // Stream completion is global. Only the primary client owns its timer.
    if (localClientNum != 0)
        return;

    int local_client_count = *(int *)(game_base_address + ADDR_LOCAL_CLIENT_COUNT);
    if (localClientNum < 0 || localClientNum >= local_client_count) {
        reset_stream_stall_observation();
        return;
    }

    uint8_t *connection = (uint8_t *)(game_base_address +
        ADDR_CLIENT_CONNECTIONS +
        ((size_t)localClientNum * CLIENT_CONNECTION_STRIDE));
    if (*(int *)(connection + CLIENT_CONNECTION_STATE_OFFSET) !=
        CONNECTION_STATE_PRIMED) {
        reset_stream_stall_observation();
        return;
    }

    uint8_t *local_clients = *(uint8_t **)(
        game_base_address + ADDR_LOCAL_CLIENTS);
    uint8_t *client_globals = *(uint8_t **)(
        game_base_address + ADDR_CLIENT_GLOBALS);
    if (local_clients == NULL || client_globals == NULL) {
        reset_stream_stall_observation();
        return;
    }

    uint8_t *local_client = local_clients +
        ((size_t)localClientNum * LOCAL_CLIENT_STRIDE);
    uint8_t *globals = client_globals +
        ((size_t)localClientNum * CLIENT_GLOBAL_STRIDE);
    uint8_t snapshot_flags = *(uint8_t *)(
        local_client + SNAPSHOT_FLAGS_OFFSET);
    bool valid_snapshot =
        *(int *)(local_client + NEW_SNAPSHOTS_OFFSET) != 0 &&
        *(int *)(local_client + SNAPSHOT_TIME_OFFSET) > 0 &&
        (snapshot_flags & 0x02) == 0;

    if (!valid_snapshot ||
        *(uint8_t *)(globals + STREAM_READY_OFFSET) != 0 ||
        !db_load_stages_complete()) {
        reset_stream_stall_observation();
        return;
    }

    uint64_t now_ms;
    if (!monotonic_milliseconds(&now_ms)) {
        reset_stream_stall_observation();
        return;
    }

    StreamStallInput input = {
        .eligible_custom_map = true,
        .connection_primed = true,
        .valid_snapshot = true,
        .db_load_complete = true,
        .engine_ready = false,
        .stream_fraction = orig_DB_GetStreamFraction(),
        .now_ms = now_ms,
    };

    if (stream_stall_recovery_update(&stream_stall_monitor, &input) &&
        stream_recovery_try_complete(
            &stream_recovery_lifecycle,
            generation)) {
        printf("BO3MacFix: stream loading for a custom map stalled "
               "at %.6f for %llu ms; treating it as complete.\n",
               input.stream_fraction,
               STREAM_STALL_TIMEOUT_MS);
    }
}

install_hook_name(CL_SetCGameTime, void, int localClientNum) {
    update_stream_stall_recovery(localClientNum);
    orig_CL_SetCGameTime(localClientNum);
}

// Initialises the navmesh and navvolume for the map.
// If we haven't made conversions already, call our conversion tool's binary.
#define C2M_Path Macfix_Path "/cod2map/cod2map64.exe"
install_hook_name(HKAI_InitMapData, void, const char *mapname, char restart) {
    printf("HKAI_InitMapData for %s\n", mapname);

    char physicsPath[0x200];
    char physicsAltPath[0x200];
    snprintf(physicsPath, sizeof(physicsPath), "Physics/%s_navmesh.hkt", mapname);
    snprintf(physicsAltPath, sizeof(physicsAltPath), Macfix_Path "/Physics/%s_navmesh.hkt", mapname);
    bool native_physics_missing = access(physicsPath, F_OK) != 0;

    // Missing native physics identifies maps whose Windows assets BO3MacFix
    // converts at runtime. Limit stream-stall recovery to those custom maps.
    begin_stream_recovery_load(native_physics_missing);

    // bees are broken on Demon Within - don't trust the original navmesh/navvolume
    bool shouldFixBees = strcmp(mapname, "cp_mi_cairo_infection2") == 0;
    if ((native_physics_missing || shouldFixBees)
        && access(physicsAltPath, F_OK) != 0) {
        if (get_wine_path() == NULL)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "You do not have Wine installed, so we can't convert the map '^2%s^7'.", mapname);
        if (!shouldFixBees) {
            char full_path[0x200];
            build_usermods_path(mapname, ".ff", 0x200, full_path, 2, (char *)(game_base_address + ADDR_g_workshopMapId));
            printf("map fastfile is '%s'\n", full_path);
        }

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

        // make sure the new physics folder exists
        if (access(Macfix_Path "/Physics/", F_OK) != 0)
            mkdir(Macfix_Path "/Physics/", 0644);

        // write out the header and all the navmesh/navvolume data to the clump
        FILE *clumpfile = fopen(Macfix_Path "/Physics/convert.clump", "wb");
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
        char cwd[1025];
        char wine_path[3075];
        getcwd(cwd, sizeof(cwd));
        snprintf(wine_path, sizeof(wine_path), "\"%s\" " C2M_Path " -mac_navmesh \"Z:%s/" Macfix_Path "/Physics/convert.clump\" \"Z:%s/" Macfix_Path "/Physics/%s\"", get_wine_path(), cwd, cwd, mapname);
        printf("executing: %s\n", wine_path);
        unsetenv("DYLD_INSERT_LIBRARIES"); // stop steam from fucking with our wine
        FILE *convert = popen(wine_path, "r");
        if (convert == NULL)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "Failed to start map conversion process for '^2%s^7'.", mapname);
        char stdoutresult[0x400] = {0};
        fread(stdoutresult, 1, 0x400, convert);
        pclose(convert);
        printf("%s\n", stdoutresult);
        printf("conversion complete!\n");

        // make sure we have a navmesh file generated
        if (access(physicsAltPath, F_OK) != 0)
            Com_Error(__FILE__, __LINE__, 2, Error_Prefix "Failed to convert map '^2%s^7' to Mac navmesh format.", mapname);
        
        unlink(Macfix_Path "/Physics/convert.clump");
    }

    orig_HKAI_InitMapData(mapname, restart);
}

// patches all instance of the old network version with the new network version
static void network_version_patch() {
    uint16_t original_network_version = 18530;
    uint16_t patched_network_version = 18532;
    // all the places the original network version is hardcoded
    uint32_t netver_addresses[] = { ADDRS_NetworkVersion };
    int num_netver_addrs = sizeof(netver_addresses) / sizeof(netver_addresses[0]);
    // enumerate through every one of these instances
    for (int i = 0; i < num_netver_addrs; i++) {
        // find the offset of the old number in the mov instruction
        int o = 0;
        bool found = false;
        for (o = 0; o < 16; o++)  {
            if (memcmp(prepatch_original_code((void *)(game_base_address + netver_addresses[i] + o), sizeof(uint16_t)), &original_network_version, sizeof(uint16_t)) == 0) {
                found = true;
                break;
            }
        }
        if (found == true) {
            DobbyCodePatch((void *)(game_base_address + netver_addresses[i] + o), (uint8_t *)&patched_network_version, sizeof(uint16_t));
        } else {
            printf("failed to find network patch for %08x\n", netver_addresses[i]);
        }
    }
    // patch the tu version used to get the ffotd
    uint8_t original_tuver[] = "tu30";
    uint8_t patched_tuver[] = "tu32";
    uint32_t tuver_addresses[] = { ADDRS_TUVersion };
    int num_tuver_addrs = sizeof(tuver_addresses) / sizeof(tuver_addresses[0]);
    // enumerate through every one of these instances
    for (int i = 0; i < num_tuver_addrs; i++) {
        int o = 0;
        bool found = false;
        for (o = 0; o < 16; o++)  {
            if (memcmp(prepatch_original_code((void *)(game_base_address + tuver_addresses[i] + o), sizeof(original_tuver) - 1), original_tuver, sizeof(original_tuver) - 1) == 0) {
                found = true;
                break;
            }
        }
        if (found == true) {
            DobbyCodePatch((void *)(game_base_address + tuver_addresses[i] + o), patched_tuver, sizeof(patched_tuver) - 1);
        } else {
            printf("failed to find string patch for %08x\n", tuver_addresses[i]);
        }
    }
    // patch the version reported by Lua_CoD_LuaCall_GetProtocolVersion
    uint32_t new_lua_version = 0x4690C800;
    DobbyCodePatch((void *)(game_base_address + ADDR_Lua_CoD_LuaCall_GetProtocolVersion_Inst + 3), (uint8_t *)&new_lua_version, sizeof(uint32_t));
    // set the LPC category version to match PC
    uint32_t newcategory = 0x0528;
    DobbyCodePatch((void *)(game_base_address + ADDR_LPC_GetRemoteManifest_Category_Inst + 6), (uint8_t *)&newcategory, sizeof(uint32_t));
    // set the CL version to match PC
    uint8_t mov_eax = 0xB8;
    int new_cl_version = 0xD3FC12;
    DobbyCodePatch((void *)(game_base_address + ADDR_LobbyHostMsg_SendJoinRequest_GetChangelistCall), &mov_eax, sizeof(uint8_t));
    DobbyCodePatch((void *)(game_base_address + ADDR_LobbyHostMsg_SendJoinRequest_GetChangelistCall + 1), (uint8_t *)&new_cl_version, sizeof(int));
}

// replaces the code at address with patch, only if it's still the code we expect
static void checked_code_patch(uint32_t address, const uint8_t *expected, uint8_t *patch, uint32_t size) {
    if (memcmp(prepatch_original_code((void *)(game_base_address + address), size), expected, size) == 0)
        DobbyCodePatch((void *)(game_base_address + address), patch, size);
    else
        printf("unexpected code at %08x, not patching\n", address);
}

// Aspyr's display layer only offers resolutions up to the desktop's size in points, which on a Retina
// screen is half its real resolution (1728x1117 on a 3456x2234 MacBook Pro), so allow the larger ones too
static void display_mode_patch() {
    static const uint8_t jg_width_short[] = { 0x7F, 0xDF };
    static const uint8_t jg_height_short[] = { 0x7F, 0xDA };
    static const uint8_t jg_width_near[] = { 0x0F, 0x8F, 0x18, 0x01, 0x00, 0x00 };
    static const uint8_t jg_height_near[] = { 0x0F, 0x8F, 0x0F, 0x01, 0x00, 0x00 };
    uint8_t nop2[] = { 0x66, 0x90 };
    uint8_t nop6[] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
    checked_code_patch(ADDR_ASL_BuildModeTable_WidthCap, jg_width_short, nop2, sizeof(nop2));
    checked_code_patch(ADDR_ASL_BuildModeTable_HeightCap, jg_height_short, nop2, sizeof(nop2));
    checked_code_patch(ADDR_ASL_DXGIOutput_GetModes_WidthCap, jg_width_near, nop6, sizeof(nop6));
    checked_code_patch(ADDR_ASL_DXGIOutput_GetModes_HeightCap, jg_height_near, nop6, sizeof(nop6));
}

// reads a number from a "Name = value" line in BO3MacFix/settings.ini, or returns fallback
#define Settings_Path Macfix_Path "/settings.ini"
static int read_setting(const char *name, int fallback, int base) {
    FILE *fp = fopen(Settings_Path, "r");
    if (fp == NULL)
        return fallback;
    int value = fallback;
    size_t name_len = strlen(name);
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line + strspn(line, " \t");
        if (strncmp(p, name, name_len) != 0)
            continue;
        p += name_len;
        p += strspn(p, " \t");
        if (*p != '=')
            continue;
        p++;
        p += strspn(p, " \t\"");
        if (base == 16 && *p == '#')
            p++;
        value = (int)strtol(p, NULL, base);
    }
    fclose(fp);
    return value;
}

static int read_int_setting(const char *name, int fallback) {
    return read_setting(name, fallback, 10);
}

// for colours like LightbarColor = 0040FF
static int read_hex_setting(const char *name, int fallback) {
    return read_setting(name, fallback, 16);
}

// the job system's worker thread count comes from a dvar that allows 2 to 4, which the Mac version leaves at 2
static void job_threads_patch(int threads) {
    static const uint8_t mov_edx_2[] = { 0xBA, 0x02, 0x00, 0x00, 0x00 };
    uint8_t mov_edx_threads[] = { 0xBA, (uint8_t)threads, 0x00, 0x00, 0x00 };
    checked_code_patch(ADDR_JobThreadsDvar_DefaultInstr, mov_edx_2, mov_edx_threads, sizeof(mov_edx_threads));
    printf("using %i job threads\n", threads);
}

void set_network_password(const char *password)
{
    if (password == NULL || strlen(password) < 1)
        network_password = 0;
    else
        network_password = gscu_canon_hash64(password);
}

int system_new(const char *command) {
    return -1;
}
FILE *popen_new(const char *command, const char *mode) {
    return NULL;
}
DYLD_INTERPOSE(system_new, system);
DYLD_INTERPOSE(popen_new, popen);

// this will get called in SDL2.0.7's FreeDevice, and not anywhere else, so it's safe
void IOHIDDeviceUnscheduleFromRunLoop_new(IOHIDDeviceRef device, CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
    IOHIDDeviceUnscheduleFromRunLoop(device, runLoop, runLoopMode);
    // release the device ref as per https://github.com/libsdl-org/SDL/commit/65fd63369411240dd54cad74916c6a6739e9363b
    //printf("releasing!\n");
    CFRelease(device);
}
DYLD_INTERPOSE(IOHIDDeviceUnscheduleFromRunLoop_new, IOHIDDeviceUnscheduleFromRunLoop);

// this will get called lots in SDL2.0.7's JoystickDeviceWasAddedCallback, so filter for PrimaryUsage
CFTypeRef IOHIDDeviceGetProperty_hook(IOHIDDeviceRef device, CFStringRef key)
{
    CFTypeRef orig = IOHIDDeviceGetProperty(device, key);
    // that address is the CFString for PrimaryUsage
    if (orig != NULL && key == (void *)(game_base_address + 0x1b8b420))
    { 
        //printf("device = %p\n", device);
        //printf("key = %p\n", key);
        // get the actual usage to see if SDL2.0.7 would be retaining it anyway
        uint32_t usage = 0;
        if (CFNumberGetValue(orig, kCFNumberSInt32Type, &usage))
        {
            // kHIDUsage_GD_Joystick, kHIDUsage_GD_GamePad, kHIDUsage_GD_MultiAxisController
            if ((usage < 9) && ((0x130U >> (usage & 0x1f) & 1) != 0)) {
                //printf("controller! retaining\n");
                // retain the device ref as per https://github.com/libsdl-org/SDL/commit/65fd63369411240dd54cad74916c6a6739e9363b
                CFRetain(device);
            }
        }
    }
    return controllers_hid_property(device, key, orig);
}
DYLD_INTERPOSE(IOHIDDeviceGetProperty_hook, IOHIDDeviceGetProperty);

int sandbox_init_with_parameters(const char *profile, uint64_t flags, const char *const parameters[], char **errorbuf);

#define EMUPATH_B "C:\\Emu\\AppAssets"
#define EMUPATH_F "C:/Emu/AppAssets"
install_hook_name(ASL_ToNativePath, void *, void *asl, char *in_str)
{
    char cwd[1025];
    getcwd(cwd, sizeof(cwd));
    size_t cwdlen = strlen(cwd);

    char newpath[4096] = { 0 };
    char *in_filename = NULL;

    // check if the file being resolves is either in the game's Assets folder (the cwd)
    // or is C:/Emu/AppAssets, which is the assets folder again
    if (strncmp(cwd, in_str, cwdlen) == 0) {
        in_filename = in_str + cwdlen;
    } else if (strncmp(EMUPATH_B, in_str, strlen(EMUPATH_B)) == 0 ||
        strncmp(EMUPATH_F, in_str, strlen(EMUPATH_F)) == 0) {
        in_filename = in_str + strlen(EMUPATH_F);
    }
    // check if it's an external mod or usermap, these need to go to / instead of /BO3MacFix/
    // not a perfect check but i'm not checking for every permutation and i want to check *before* path correction
    bool is_user_mod = 
        strstr(in_str, "/usermaps") != NULL || strstr(in_str, "/mods") != NULL ||
        strstr(in_str, "\\usermaps") != NULL || strstr(in_str, "\\mods") != NULL;
    // if it's in our cwd check if we have it in the BO3MacFix folder and redirect to read from there instead
    if (in_filename != NULL && strlen(in_filename) >= 2) {
        strncat(newpath, cwd, sizeof(newpath) - strlen(newpath) - 1);
        newpath[strlen(newpath)] = '/';
        if (is_user_mod)
            strncat(newpath, Basegame_Path, sizeof(newpath) - strlen(newpath) - 1);
        else
            strncat(newpath, Macfix_Path, sizeof(newpath) - strlen(newpath) - 1);
        newpath[strlen(newpath)] = '/';
        strncat(newpath, in_filename, sizeof(newpath) - strlen(newpath) - 1);
        // correct any backticks before checking if it exists
        for (int i = 0; i < strlen(newpath); i++)
            if (newpath[i] == '\\') newpath[i] = '/';
        // if the file exists redirect it, or if it's a mod
        if (is_user_mod || access(newpath, F_OK) == 0) {
#ifdef MACFIX_DEBUG
            // to avoid logspam only log files that exist
            if (access(newpath, F_OK) == 0)
                printf("redirecting %s to \"%s\"\n", in_filename, newpath);
#endif
            return orig_ASL_ToNativePath(asl, newpath);
        }
    }
    
    return orig_ASL_ToNativePath(asl, in_str);
}

bool ISteamApps_BIsSubscribedApp(void *interface, AppId_t app) {
    return steam_dlc_owns(app);
}

bool ISteamApps_BIsDlcInstalled(void *interface, AppId_t app) {
    return steam_dlc_installed(app);
}

char playerNameBuffer[17] = {0};
void set_player_name(const char *name) {
    strncpy(playerNameBuffer, name, sizeof(playerNameBuffer));
}
const char *get_player_name() {
    return playerNameBuffer;
}

const char *ISteamFriends_GetPersonaName(void *interface) {
    // check the player name set dynamically first
    if (playerNameBuffer[0] != '\0' && strlen(playerNameBuffer) >= 1 && strlen(playerNameBuffer) <= 16)
        return playerNameBuffer;
    // then check the player name
    const char *envname = getenv("BO3MACFIX_PLAYERNAME");
    if (envname != NULL && strlen(envname) >= 1 && strlen(envname) <= 16)
        return envname;
    return steam_name();
}

install_hook_name(CSteamAPIContext_Init, bool, CSteamAPIContext *ctx) {
    bool r = orig_CSteamAPIContext_Init(ctx);
    if (r == true) {
        init_steam_context(ctx);
        // populate our caches
        steam_friends_refresh_cache();
        steam_dlc_refresh_cache();
        // replace ISteamApps DLC to use cached versions
        replace_vtable_entry(&(*ctx->m_pSteamApps)->BIsSubscribedApp, ISteamApps_BIsSubscribedApp);
        replace_vtable_entry(&(*ctx->m_pSteamApps)->BIsDlcInstalled, ISteamApps_BIsDlcInstalled);
        // replace ISteamFriends to do our username spoof
        replace_vtable_entry(&(*ctx->m_pSteamFriends)->GetPersonaName, ISteamFriends_GetPersonaName);
    }
    return r;
}

void lua_correct_path(const char *in_path, char *out_path, size_t out_path_len) {
    out_path[0] = '\0';
    strncat(out_path, Basegame_Path, out_path_len - strlen(out_path) - 1);
    strncat(out_path, in_path, out_path_len - strlen(out_path) - 1);
    for (int i = 0; i < strlen(out_path); i++)
        if (out_path[i] == '\\') out_path[i] = '/';
}

install_hook_name(hksf_fopen_internal, void *, char *filename, char *opentype, int unknown) {
    printf("Lua opening file '%s'\n", filename);
    char newpath[4096];
    lua_correct_path(filename, newpath, sizeof(newpath));
    printf("-> '%s'\n", newpath);
    return fopen(newpath, opentype);
}

static bool shown_dll_warning = false;
install_hook_name(hks_load_dll, bool, void *s, const char *filename, const char *func_name) {
    printf("Lua loadlib with '%s' (func: '%s')\n", filename, func_name);
    char newpath[4096];
    lua_correct_path(filename, newpath, sizeof(newpath));
    printf("-> '%s'\n", newpath);

    // so we can bundle support for workshop mods retroactively where the creator might not be able to / is unwilling to add a dylib
    // we check /BO3MacFix/natives/[dll name]-[dll hash].dylib
    // NOTE(Emma): might want to change to [dll name]-[workshop id].dylib for workshop mods to avoid updates breaking it if changes are insignificant enough
    char test_native_path[1024];
    uint8_t file_sha1_hash[0x14];
    char file_sha1_hash_str[(0x14 * 2) + 1];
    if (get_file_sha1(newpath, file_sha1_hash)) {
        if (bytes_to_hex(file_sha1_hash, sizeof(file_sha1_hash), file_sha1_hash_str, sizeof(file_sha1_hash_str))) {
            snprintf(test_native_path, sizeof(test_native_path), Macfix_Path "/natives/%s-%s.dylib", filename_from_path(newpath), file_sha1_hash_str);
            if (access(test_native_path, F_OK) == 0) {
                printf("--> %s\n", test_native_path);
                return orig_hks_load_dll(s, test_native_path, func_name);
            }
        }
    }

    // T7Overcharged (used by mods such as All-around Enhancement) only comes as a Windows DLL, so use our own version of it
    if (t7o_is_overcharged_dll(newpath) && strcmp(func_name, "init") == 0) {
        printf("--> BO3MacFix T7Overcharged\n");
        t7o_push_init(s, newpath);
        return true;
    }

    // display an alert message (that makes sense) to the user informing them that the DLL load has failed
    if (!is_macos_dylib(newpath) && !shown_dll_warning) {
        printf("which isn't a macOS dylib, erroring...\n");
        char alert_msg[1024];
        snprintf(alert_msg, sizeof(alert_msg), "The mod you loaded tried to open a Windows DLL file. The game might work fine, but it might be unstable.\n\n%s: %s", filename_from_path(newpath), func_name);
        display_nsalert("BO3MacFix Mod Warning", alert_msg);
        // only show the warning once
        shown_dll_warning = true;
    }

    // TODO(Emma): we want to make sure the OS won't reject the dylib load because of Gatekeeper...

    return orig_hks_load_dll(s, newpath, func_name);
}

install_hook_name(CL_FirstSnapshot, void, void *arg) {
    bounce_dock_icon();
    return orig_CL_FirstSnapshot(arg);
}

install_hook_name(Com_GetBuildIntField, int, int field) {
    // if the field is this magic number ("Mac!"), return a version number
    if (field == 0x4D616321) {
        return 0x00000102; // 0x0102 for 1.2
    }
    return orig_Com_GetBuildIntField(field);
}

// Lua sees the build info of the current Windows version, since some workshop mods
// (e.g. All-around Enhancement) quit the game on any other build
#define BUILD_INTFIELD_CHANGELIST 1
#define BUILD_STRINGFIELD_BUILD_TIME 6
#define PC_BUILD_CHANGELIST 13892626
#define PC_BUILD_TIME "Wed Dec 10 17:10:46 2025"
install_hook_name(Lua_GetBuildIntField, int, lua_State *s) {
    float field;
    if (hks_number_arg(s, 1, &field) && (int)field == BUILD_INTFIELD_CHANGELIST) {
        hks_push_number(s, PC_BUILD_CHANGELIST);
        return 1;
    }
    return orig_Lua_GetBuildIntField(s);
}

install_hook_name(Lua_GetBuildStringField, int, lua_State *s) {
    float field;
    if (hks_number_arg(s, 1, &field) && (int)field == BUILD_STRINGFIELD_BUILD_TIME) {
        hks_push_string(s, PC_BUILD_TIME);
        return 1;
    }
    return orig_Lua_GetBuildStringField(s);
}

static int (*SDL_PrivateGameControllerAddMapping)(const char *mappingString, int priority);

// WIP(Emma): unfinished support for adding extra SDL controller mappings
static const char *extraGamepadMappings[] = {
    "030000004c050000a00b000000010000,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    "030000004c050000c405000000000000,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    "030000004c050000c405000000010000,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    "030000004c050000cc09000000010000,PS4 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    "050000004c050000e60c000000010000,PS5 Controller,a:b1,b:b2,back:b8,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,misc1:b13,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    "030000004c050000cc09000000000000,Sony DualShock 4 V2,a:b1,b:b2,back:b13,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    "030000004c050000a00b000000000000,Sony DualShock 4 Wireless Adaptor,a:b1,b:b2,back:b13,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b12,leftshoulder:b4,leftstick:b10,lefttrigger:a3,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b11,righttrigger:a4,rightx:a2,righty:a5,start:b9,x:b0,y:b3,",
    NULL
};
install_hook_name(SDL_GameControllerInitMappings, int, void)
{
    int r = orig_SDL_GameControllerInitMappings();
    int i = 0;
    const char *pMappingString = extraGamepadMappings[i];
    while (pMappingString) {
        SDL_PrivateGameControllerAddMapping(pMappingString, 0);
        i++;
        pMappingString = extraGamepadMappings[i];
    }
    return r;
}

// Entrypoint for the dylib
__attribute__((constructor)) static void dylib_main() {
    // make sure we're loading into the game process and nothing else
    // don't want to inject into defaults, Aspyr launcher or Steamwebhelper
    const char *image_path = _dyld_get_image_name(0);
    const char *image_name = filename_from_path(image_path);
    if (strcmp(image_name, "CoDBlkOps3_Exe") != 0 && strcmp(image_name, Prepatched_Exe_Name) != 0)
        return;

    // started from Steam our log goes nowhere, so keep it in BO3MacFix/log.txt (replaced every launch)
    struct stat out_stat;
    if (!isatty(STDOUT_FILENO) && (fstat(STDOUT_FILENO, &out_stat) != 0 || !S_ISREG(out_stat.st_mode))) {
        if (freopen(Macfix_Path "/log.txt", "w", stdout) != NULL) {
            setvbuf(stdout, NULL, _IOLBF, 0);
            dup2(fileno(stdout), STDERR_FILENO);
        }
    }

    // get the base address of the binary
    game_base_address = get_base_address();
    prepatch_init(game_base_address);

    // test switch: leave the game's code exactly as shipped (no hooks, no byte patches), to measure whether
    // patching code in memory makes Rosetta run the game through its JIT instead of its ahead-of-time translation.
    // The game then runs stock: no AAE, map loading fix, previews, native resolutions, controllers or CPU/GPU tweaks.
    // Only the data-only DYLD_INTERPOSE replacements stay active.
    if (read_int_setting("NoCodePatches", 0) != 0 || getenv("BO3MACFIX_NOCODEPATCHES") != NULL) {
        printf("BO3MacFix: NoCodePatches is on, not changing any game code\n");
        return;
    }

    // apply the hooks
    install_hook_HKAI_InitMapData((void *)(game_base_address + ADDR_HKAI_InitMapData));
    install_hook_load_workshop_texture((void *)(game_base_address + ADDR_load_workshop_texture));
    install_hook_Live_SystemInfo((void *)(game_base_address + ADDR_Live_SystemInfo));
    install_hook_someLoggingFunction((void *)(game_base_address + ADDR_someLoggingFunction));
    //install_hook_someWindowCreationFunction((void *)(game_base_address + ADDR_someWindowCreationFunction));
    install_hook_SDL_SetWindowTitle((void *)(game_base_address + ADDR_SDL_SetWindowTitle));
    install_hook_Sys_VerifyPacketChecksum((void *)(game_base_address + ADDR_Sys_VerifyPacketChecksum));
    install_hook_Sys_CheckSumPacketCopy((void *)(game_base_address + ADDR_Sys_CheckSumPacketCopy));
    install_hook_LobbyMsgRW_PrepReadMsg((void *)(game_base_address + ADDR_LobbyMsgRW_PrepReadMsg));
    install_hook_LobbyMsgRW_PrepWriteMsg((void *)(game_base_address + ADDR_LobbyMsgRW_PrepWriteMsg));
    install_hook_ASL_ToNativePath((void *)(game_base_address + ADDR_ASL_ToNativePath));
    install_hook_CSteamAPIContext_Init((void *)(game_base_address + ADDR_CSteamAPIContext_Init));
    install_hook_dwInstantDispatchMessage((void *)(game_base_address + ADDR_dwInstantMessageDispatch));
    install_hook_hks_load_dll((void *)(game_base_address + ADDR_hks_load_dll));
    install_hook_hksf_fopen_internal((void *)(game_base_address + ADDR_hksf_fopen_internal));
    install_hook_CL_FirstSnapshot((void *)(game_base_address + ADDR_CL_FirstSnapshot));
    install_hook_DB_GetStreamFraction((void *)(game_base_address + ADDR_DB_GetStreamFraction));
    install_hook_CL_SetCGameTime((void *)(game_base_address + ADDR_CL_SetCGameTime));
    install_hook_Com_GetBuildIntField((void *)(game_base_address + ADDR_Com_GetBuildIntField));
    install_hook_Lua_GetBuildIntField((void *)(game_base_address + ADDR_Lua_GetBuildIntField));
    install_hook_Lua_GetBuildStringField((void *)(game_base_address + ADDR_Lua_GetBuildStringField));
    //install_hook_SDL_GameControllerInitMappings((void *)(game_base_address + ADDR_SDL_GameControllerInitMappings));

    // set up functions that we later call
    havok_init(game_base_address);
    R_TrackTextureMemory = (void *)(game_base_address + ADDR_R_TrackTextureMemory);
    build_usermods_path = (void *)(game_base_address + ADDR_build_usermods_path);
    DB_FindXAssetHeader = (void *)(game_base_address + ADDR_DB_FindXAssetHeader);
    Com_Error = (void *)(game_base_address + ADDR_Com_Error);
    Dvar_SetFromStringByName = (void *)(game_base_address + ADDR_Dvar_SetFromStringByName);
    Sys_Checksum = (void *)(game_base_address + ADDR_Sys_Checksum);
    MSG_ReadByte = (void *)(game_base_address + ADDR_MSG_ReadByte);
    MSG_WriteByte = (void *)(game_base_address + ADDR_MSG_WriteByte);
    SDL_PrivateGameControllerAddMapping = (void *)(game_base_address + ADDR_SDL_PrivateGameControllerAddMapping);

    // disable setting a fatal error in Scr_ErrorInternal, Aspyr forces it to always be true
    uint8_t error_patch[1] = { 0x00 };
    DobbyCodePatch((void *)(game_base_address + ADDR_Scr_ErrorInternalPatch + 4), error_patch, 1);

    // fix a crash bug
    uint8_t ret[1] = { 0xC3 };
    DobbyCodePatch((void *)(game_base_address + ADDR_Lua_CmdParseArgs), ret, sizeof(ret));

    // larp as v100 in the version number just to feel nice about yourself :3
    // probably not doing this until we're closer to v100
    //uint8_t larp[1] = { 100 };
    //DobbyCodePatch((void *)(game_base_address + ADDR_Live_SystemInfo_LarpInstr), larp, sizeof(larp));

    // set the network password rather than having it be 0 - should probably have a gui to set it
    const char *password_text = getenv("BO3MACFIX_NETWORKPASSWORD");
    if (password_text != NULL && strlen(password_text) >= 1) {
        set_network_password(password_text);
    } else {
        // some sort of safety net
    }

    // enable friends only if it's specified at env
    const char *friends_only_env = getenv("BO3MACFIX_FRIENDSONLY");
    if (friends_only_env != NULL &&
        (strcasecmp(friends_only_env, "true") == 0 || strcasecmp(friends_only_env, "1") == 0)) {
        friends_only = true;
    }
        
    // nop out where dw sets the Content-Length header, fixes a bug with modern macOS failing to log in online
    uint8_t curl_nops[(ADDR_Curl_ContentLengthSetEnd - ADDR_Curl_ContentLengthSetStart)];
    memset(curl_nops, 0x90, sizeof(curl_nops));
    DobbyCodePatch((void *)(game_base_address + ADDR_Curl_ContentLengthSetStart), curl_nops, sizeof(curl_nops));

    // replace the strcmp where HKAI_InitMapData checks to see if the map name is cp_mi_cairo_infection2
    uint8_t return_0[] = { 0xB8, 0, 0, 0, 0};
    DobbyCodePatch((void *)(game_base_address + ADDR_HKAI_InitMapData_DemonWithinCheck), return_0, sizeof(return_0));

    // patches the network version to match Windows
    network_version_patch();

    // lets the game use native Retina resolutions
    display_mode_patch();

    // use more job worker threads if specified in settings.ini or at env (2 to 4)
    int job_threads = read_int_setting("JobThreads", 2);
    const char *job_threads_env = getenv("BO3MACFIX_JOBTHREADS");
    if (job_threads_env != NULL)
        job_threads = atoi(job_threads_env);
    if (job_threads >= 3 && job_threads <= 4)
        job_threads_patch(job_threads);

    // DualSense/DualShock 4 through macOS's GameController framework (Bluetooth, rumble, lightbar), see gc_bridge.m.
    // Before controllers_init, which then leaves PlayStation controllers to it.
    gc_bridge_init(game_base_address, read_int_setting("ControllerBridge", 0), read_hex_setting("LightbarColor", -1));

    // DualSense/DualShock 4 mappings for SDL, and PlayStation button prompts (0 = auto, 1 = Xbox, 2 = PlayStation)
    controllers_init(game_base_address, read_int_setting("ControllerMappings", 1), read_int_setting("ButtonPrompts", 0));

    // thread QoS and job worker waiting fixes (see cpu_tuning.c), each can be turned off in settings.ini
    cpu_tuning_init(game_base_address, read_int_setting("ThreadQoS", 1), read_int_setting("JobIdleFix", 1),
                    read_int_setting("YieldSleep0", 0));

    // Metal frame queue depth and present pacing from settings.ini or at env, see gpu_tweaks.c
    gpu_tweaks_init(read_int_setting("FramesInFlight", 2), read_int_setting("FramePacingFPS", 0), checked_code_patch);

    // MetalFX upscaling when the game runs below the screen's resolution (MetalFXUpscale, 0 = off), see metalfx.m
    gpu_tweaks_metalfx_init(read_int_setting("MetalFXUpscale", 0), checked_code_patch);

    // pre-fetch the wine path so we don't need to search earlier
    get_wine_path();

    // running from the pre-patched executable, say what was set up and whether any game code was written
    prepatch_report();

    // i'll figure all that stuff out later :')
    //int sbr = sandbox_init_with_parameters("(version 1)(allow default)(debug allow)", 0, NULL, NULL);
    //printf("seatbelt init %i\n", sbr);
}
