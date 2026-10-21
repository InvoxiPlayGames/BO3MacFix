// The pre-patched executable, see prepatch.h.
//
// In the copy BO3MacFix_BuildPrepatched writes:
// - BO3MacFix's byte patches are already applied
// - every hooked function starts with a jump to a stub in __TEXT's tail padding:
//       cmp qword [rip+slot], 0 ; je trampoline ; jmp [rip+slot]
//       trampoline: <the instructions the jump replaced> ; jmp <function + their length>
//   so hooking it only means storing a pointer in its slot, in __DATA's zero-filled tail
// - each "mov r32, imm32" a setting changes jumps to a stub that loads the value from a slot when it's set
// - each call through objc_msgSend's pointer a setting redirects calls a stub that jumps through a slot when it's set
// - a header at the start of the padding lists all of it, with the original bytes
// With every slot left at 0 the copy runs exactly like the original.
#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/loader.h>

#define PREPATCH_IMPL // the real DobbyHook/DobbyCodePatch, not the redirects in dobby.h
#include "dobby.h"
#include "offsets.h"
#include "prepatch.h"

#define EXPORT __attribute__((visibility("default")))

#define Prepatch_Magic "BO3MFPP1"
#define Prepatch_Version 2
#define Game_Base 0x100000000ULL

#pragma mark - what goes into the copy

// a hooked function, with the whole instructions the 5-byte jump at its entry replaces (none rip-relative)
typedef struct {
    uint32_t function;
    uint8_t length;
    uint8_t short_jcc_at; // 1 + the offset of a short conditional jump among them, made near in the trampoline
    uint8_t stolen[10];
} hook_def_t;

#define Push_R15 6, 0, { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57 } // push rbp; mov rbp, rsp; push r15
#define Push_R14 6, 0, { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56 } // push rbp; mov rbp, rsp; push r14

// every function BO3MacFix hooks
static const hook_def_t hook_defs[] = {
    { ADDR_ASL_SleepEx, Push_R15 },
    { ADDR_ASL_ToNativePath, Push_R15 },
    { ADDR_ASL_XInputGetState, Push_R14 },
    { ADDR_ASL_XInputSetState, Push_R14 },
    { ADDR_CL_FirstSnapshot, Push_R15 },
    { ADDR_CL_SetCGameTime, Push_R15 },
    { ADDR_Com_GetBuildIntField, 5, 4, { 0x83, 0xFF, 0x02, 0x74, 0x11 } }, // cmp edi, 2; je +0x11
    { ADDR_CSteamAPIContext_Init, Push_R15 },
    { ADDR_DB_GetStreamFraction, Push_R15 },
    { ADDR_dwInstantMessageDispatch, Push_R15 },
    { ADDR_GPad_GetGamepadType, 7, 0, { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x63, 0xC7 } }, // ...; movsxd rax, edi
    { ADDR_HKAI_InitMapData, Push_R15 },
    { ADDR_hks_load_dll, Push_R15 },
    { ADDR_hksf_fopen_internal, 6, 0, { 0x55, 0x48, 0x89, 0xE5, 0x31, 0xC0 } }, // ...; xor eax, eax
    { ADDR_Live_SystemInfo, Push_R15 },
    { ADDR_load_workshop_texture, Push_R15 },
    { ADDR_LobbyMsgRW_PrepReadMsg, Push_R14 },
    { ADDR_LobbyMsgRW_PrepWriteMsg, Push_R14 },
    { ADDR_Lua_GetBuildIntField, 5, 0, { 0x55, 0x48, 0x89, 0xE5, 0x53 } }, // ...; push rbx
    { ADDR_Lua_GetBuildStringField, Push_R14 },
    { ADDR_SDL_SetWindowTitle, Push_R14 },
    { ADDR_someLoggingFunction, Push_R15 },
    { ADDR_Sys_CheckSumPacketCopy, 10, 0, { 0x66, 0xB8, 0xFF, 0xFF, 0x81, 0xFA, 0xE8, 0x04, 0x00, 0x00 } }, // mov ax, 0xFFFF; cmp edx, 0x4E8
    { ADDR_Sys_VerifyPacketChecksum, Push_R15 },
};

// a "mov r32, imm32" that a setting changes, with the flags dead after it (its stub tests the slot)
typedef struct {
    uint32_t site;
    uint8_t original[5];
} value_def_t;

static const value_def_t value_defs[] = {
    { ADDR_JobThreadsDvar_DefaultInstr, { 0xBA, 0x02, 0x00, 0x00, 0x00 } },      // mov edx, 2
    { ADDR_JQ_IdleWait_TimeoutInstr, { 0xBE, 0x01, 0x00, 0x00, 0x00 } },         // mov esi, 1
    { ADDR_ASL_FramesInFlightSemaphoreInstr, { 0xBF, 0x02, 0x00, 0x00, 0x00 } }, // mov edi, 2
};

// a call qword [rip+x] through objc_msgSend's pointer that a setting redirects
typedef struct {
    uint32_t site;
    uint8_t original[6];
} call_def_t;

static const call_def_t call_defs[] = {
    { ADDR_ASL_Present_PresentDrawableCall, { 0xFF, 0x15, 0xD9, 0xDB, 0x87, 0x01 } },
    { ADDR_ASL_Present_CopyToDrawableCall, { 0xFF, 0x15, 0xDC, 0xDD, 0x87, 0x01 } },
};

// a byte patch BO3MacFix always makes (dylib_main and network_version_patch in bo3macfix.c, display_mode_patch)
typedef struct {
    uint32_t offset;
    uint32_t length;
    const char *original;
    const char *patched; // NULL for nops
} patch_def_t;

static const patch_def_t patch_defs[] = {
    { ADDR_Scr_ErrorInternalPatch + 4, 1, "\x01", "\x00" },
    { ADDR_Lua_CmdParseArgs, 1, "\x55", "\xC3" },
    { ADDR_Curl_ContentLengthSetStart, ADDR_Curl_ContentLengthSetEnd - ADDR_Curl_ContentLengthSetStart,
      "\xE8\x97\x3F\x09\x00\x48\x8B\x95\x38\xEF\xFF\xFF\x48\x89\x82\x88\x00\x00\x00", NULL },
    { ADDR_HKAI_InitMapData_DemonWithinCheck, 5, "\xE8\x2B\xDD\x81\x00", "\xB8\x00\x00\x00\x00" },
    { ADDR_Lua_CoD_LuaCall_GetProtocolVersion_Inst + 3, 4, "\x00\xC4\x90\x46", "\x00\xC8\x90\x46" },
    { ADDR_LPC_GetRemoteManifest_Category_Inst + 6, 4, "\x14\x05\x00\x00", "\x28\x05\x00\x00" },
    { ADDR_LobbyHostMsg_SendJoinRequest_GetChangelistCall, 5, "\xE8\xC0\x46\xCE\xFF", "\xB8\x12\xFC\xD3\x00" },
    { ADDR_ASL_BuildModeTable_WidthCap, 2, "\x7F\xDF", "\x66\x90" },
    { ADDR_ASL_BuildModeTable_HeightCap, 2, "\x7F\xDA", "\x66\x90" },
    { ADDR_ASL_DXGIOutput_GetModes_WidthCap, 6, "\x0F\x8F\x18\x01\x00\x00", "\x66\x0F\x1F\x44\x00\x00" },
    { ADDR_ASL_DXGIOutput_GetModes_HeightCap, 6, "\x0F\x8F\x0F\x01\x00\x00", "\x66\x0F\x1F\x44\x00\x00" },
};

// network_version_patch: the network version and the "tu30" string, each found within 16 bytes of these
static const uint32_t netver_addresses[] = { ADDRS_NetworkVersion };
static const uint32_t tuver_addresses[] = { ADDRS_TUVersion };
static const uint16_t netver_original = 18530, netver_patched = 18532;
static const char tuver_original[4] = "tu30", tuver_patched[4] = "tu32";

#pragma mark - the copy's header

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t table_id;     // table_id() of the BO3MacFix that made it
    uint32_t hook_count;
    uint32_t value_count;
    uint32_t call_count;
    uint32_t patch_count;
    uint32_t hooks_offset; // the records, as offsets from the game's base address
    uint32_t values_offset;
    uint32_t calls_offset;
    uint32_t patches_offset;
    uint8_t source_sha256[CC_SHA256_DIGEST_LENGTH]; // the original it was made from
} prepatch_header_t;

typedef struct {
    uint32_t function;
    uint32_t slot;
    uint32_t trampoline;
    uint8_t length;
    uint8_t pad[3];
    uint8_t original[16];
} prepatch_hook_t;

// a value or call site
typedef struct {
    uint32_t site;
    uint32_t slot;
    uint8_t length;
    uint8_t pad[3];
    uint8_t original[8];
} prepatch_site_t;

// byte patch records follow as { uint32_t offset; uint8_t length; uint8_t original[length]; }

static uint32_t fnv1a(uint32_t hash, const void *data, size_t size) {
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ ((const uint8_t *)data)[i]) * 16777619u;
    return hash;
}

// identifies everything above, so the launcher remakes the copy when a new BO3MacFix changes any of it
static uint32_t table_id(void) {
    uint32_t hash = fnv1a(2166136261u, &(uint32_t){ Prepatch_Version }, sizeof(uint32_t));
    hash = fnv1a(hash, hook_defs, sizeof(hook_defs));
    hash = fnv1a(hash, value_defs, sizeof(value_defs));
    hash = fnv1a(hash, call_defs, sizeof(call_defs));
    for (size_t i = 0; i < sizeof(patch_defs) / sizeof(patch_defs[0]); i++) {
        hash = fnv1a(hash, &patch_defs[i].offset, sizeof(uint32_t));
        hash = fnv1a(hash, patch_defs[i].original, patch_defs[i].length);
        hash = fnv1a(hash, patch_defs[i].patched ? patch_defs[i].patched : "\x90", patch_defs[i].patched ? patch_defs[i].length : 1);
    }
    hash = fnv1a(hash, netver_addresses, sizeof(netver_addresses));
    hash = fnv1a(hash, tuver_addresses, sizeof(tuver_addresses));
    hash = fnv1a(hash, &netver_patched, sizeof(netver_patched));
    return fnv1a(hash, tuver_patched, sizeof(tuver_patched));
}

#pragma mark - running from the copy

static uint64_t base = 0;
static uint64_t text_end = 0, image_end = 0;
static const prepatch_header_t *header = NULL;
static const prepatch_hook_t *hooks = NULL;
static const prepatch_site_t *values = NULL, *calls = NULL;
static int hooks_set = 0, values_set = 0, calls_set = 0, patches_in_place = 0, in_memory = 0, refused = 0;

bool prepatch_active(void) {
    return header != NULL;
}

// finds where the game's __TEXT segment and its whole image end in memory
static void find_image_bounds(uint64_t base_address) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)base_address;
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    uint64_t end = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (strcmp(seg->segname, "__TEXT") == 0)
                text_end = base_address + seg->vmsize;
            if (strcmp(seg->segname, "__PAGEZERO") != 0 && seg->vmaddr + seg->vmsize > end)
                end = seg->vmaddr + seg->vmsize;
        }
        lc = (const struct load_command *)((const uint8_t *)lc + lc->cmdsize);
    }
    image_end = base_address + (end - Game_Base);
}

bool prepatch_init(uint64_t base_address) {
    base = base_address;
    find_image_bounds(base_address);
    const prepatch_header_t *h = (const prepatch_header_t *)(base + ADDR_Prepatch_Header);
    if (memcmp(h->magic, Prepatch_Magic, sizeof(h->magic)) != 0)
        return false;
    if (h->version != Prepatch_Version) {
        printf("BO3MacFix: the pre-patched executable has format %u, this BO3MacFix needs %u\n", h->version, Prepatch_Version);
        return false;
    }
    header = h;
    hooks = (const prepatch_hook_t *)(base + h->hooks_offset);
    values = (const prepatch_site_t *)(base + h->values_offset);
    calls = (const prepatch_site_t *)(base + h->calls_offset);
    printf("BO3MacFix: running from the pre-patched executable (%u hooks, %u settings, %u calls, %u byte patches)\n",
           h->hook_count, h->value_count, h->call_count, h->patch_count);
    if (h->table_id != table_id())
        printf("BO3MacFix: the pre-patched executable was made by a different BO3MacFix, start the game through "
               "Steam to remake it\n");
    return true;
}

typedef struct {
    uint64_t start, end;
    uint8_t *buffer;
    bool changed;
} overlay_t;

static void overlay(uint64_t at, const uint8_t *original, size_t length, overlay_t *o) {
    for (size_t i = 0; i < length; i++) {
        uint64_t a = at + i;
        if (a >= o->start && a < o->end) {
            o->buffer[a - o->start] = original[i];
            o->changed = true;
        }
    }
}

const void *prepatch_original_code(const void *address, size_t size) {
    static uint8_t buffer[64];
    if (!prepatch_active() || size > sizeof(buffer))
        return address;
    overlay_t o = { (uint64_t)address, (uint64_t)address + size, buffer, false };
    memcpy(buffer, address, size);
    for (uint32_t i = 0; i < header->hook_count; i++)
        overlay(base + hooks[i].function, hooks[i].original, sizeof(hooks[i].original), &o);
    for (uint32_t i = 0; i < header->value_count; i++)
        overlay(base + values[i].site, values[i].original, values[i].length, &o);
    for (uint32_t i = 0; i < header->call_count; i++)
        overlay(base + calls[i].site, calls[i].original, calls[i].length, &o);
    const uint8_t *p = (const uint8_t *)(base + header->patches_offset);
    for (uint32_t i = 0; i < header->patch_count; i++) {
        uint32_t offset;
        memcpy(&offset, p, sizeof(offset));
        overlay(base + offset, p + 5, p[4], &o);
        p += 5 + p[4];
    }
    return o.changed ? buffer : address;
}

static bool in_game_code(const void *address) {
    return (uint64_t)address >= base && (uint64_t)address < text_end;
}

static bool in_game_image(const void *address) {
    return (uint64_t)address >= base && (uint64_t)address < image_end;
}

// replaces DobbyHook (see dobby.h): in the pre-patched copy, hooking a function only stores its slot
int BO3MacFix_Hook(void *address, void *fake_func, void **out_origin_func) {
    if (!prepatch_active() || !in_game_image(address))
        return DobbyHook(address, fake_func, out_origin_func);
    uint32_t offset = (uint32_t)((uint64_t)address - base);
    for (uint32_t i = 0; i < header->hook_count; i++) {
        if (hooks[i].function == offset) {
            *(void **)(base + hooks[i].slot) = fake_func;
            *out_origin_func = (void *)(base + hooks[i].trampoline);
            hooks_set++;
            return 0;
        }
    }
    // a hook the copy doesn't have: hooking it in memory still works, but brings back Rosetta's JIT
    if (prepatch_original_code(address, 16) != address) {
        refused++;
        printf("BO3MacFix: can't hook %08x, the pre-patched executable changed that code\n", offset);
        return -1;
    }
    in_memory++;
    printf("BO3MacFix: the pre-patched executable has no hook at %08x, hooking it in memory\n", offset);
    return DobbyHook(address, fake_func, out_origin_func);
}

// replaces DobbyCodePatch (see dobby.h): in the pre-patched copy, nothing writes the game's code
int BO3MacFix_CodePatch(void *address, uint8_t *buffer, uint32_t buffer_size) {
    if (!prepatch_active() || !in_game_image(address))
        return DobbyCodePatch(address, buffer, buffer_size);
    uint32_t offset = (uint32_t)((uint64_t)address - base);
    if (!in_game_code(address)) {
        // the game's own data, which is writable anyway
        memcpy(address, buffer, buffer_size);
        return 0;
    }
    if (memcmp(address, buffer, buffer_size) == 0) {
        patches_in_place++;
        return 0;
    }
    for (uint32_t i = 0; i < header->value_count; i++) {
        const prepatch_site_t *v = &values[i];
        if (v->site == offset && buffer_size == v->length && buffer[0] == v->original[0]) {
            uint32_t value;
            memcpy(&value, buffer + 1, sizeof(value));
            *(uint32_t *)(base + v->slot) = value;
            values_set++;
            return 0;
        }
    }
    // a patch the copy doesn't have: making it in memory still works, but brings back Rosetta's JIT
    if (prepatch_original_code(address, buffer_size) != address) {
        refused++;
        printf("BO3MacFix: can't patch %08x, the pre-patched executable changed that code\n", offset);
        return -1;
    }
    in_memory++;
    printf("BO3MacFix: the pre-patched executable doesn't have the patch at %08x, patching it in memory\n", offset);
    return DobbyCodePatch(address, buffer, buffer_size);
}

bool prepatch_redirect_call(uint32_t offset, void *function) {
    if (!prepatch_active())
        return false;
    for (uint32_t i = 0; i < header->call_count; i++) {
        if (calls[i].site == offset) {
            *(void **)(base + calls[i].slot) = function;
            calls_set++;
            return true;
        }
    }
    return false;
}

// how many of the game's code pages have been written to (copied on write) since it was loaded
static int written_code_pages(void) {
    int written = 0;
    mach_vm_address_t address = base;
    while (address < text_end) {
        mach_vm_size_t size = 0;
        vm_region_extended_info_data_t info;
        mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
        mach_port_t object = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info,
                           &count, &object) != KERN_SUCCESS || address >= text_end)
            break;
        written += info.pages_dirtied;
        address += size;
    }
    return written;
}

void prepatch_report(void) {
    if (!prepatch_active())
        return;
    printf("BO3MacFix: pre-patched executable: %i of %u hooks set, %i settings, %i calls redirected, %i patches "
           "already in place, %i done in memory, %i refused, game code pages written: %i\n",
           hooks_set, header->hook_count, values_set, calls_set, patches_in_place, in_memory, refused,
           written_code_pages());
}

#pragma mark - building the copy

typedef struct {
    uint8_t *data;              // the copy
    size_t size;
    uint32_t claimed[256][2];   // ranges already changed, to catch overlaps
    int claimed_count;
    uint8_t records[4096];      // the byte patch records
    size_t records_size;
    uint32_t patch_count;
    char *message;
    size_t message_size;
} build_t;

static int fail(char *message, size_t message_size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(message, message_size, format, args);
    va_end(args);
    return -1;
}

static int claim(build_t *b, uint32_t offset, uint32_t length, const char *what) {
    for (int i = 0; i < b->claimed_count; i++) {
        if (offset < b->claimed[i][1] && b->claimed[i][0] < offset + length)
            return fail(b->message, b->message_size, "%s at %08x overlaps another change", what, offset);
    }
    if (b->claimed_count == sizeof(b->claimed) / sizeof(b->claimed[0]) || offset + length > b->size)
        return fail(b->message, b->message_size, "%s at %08x doesn't fit", what, offset);
    b->claimed[b->claimed_count][0] = offset;
    b->claimed[b->claimed_count][1] = offset + length;
    b->claimed_count++;
    return 0;
}

// applies a byte patch and records the original bytes
static int patch(build_t *b, uint32_t offset, const void *original, const void *patched, uint32_t length) {
    if (offset + length > b->size || memcmp(b->data + offset, original, length) != 0)
        return fail(b->message, b->message_size, "unexpected code at %08x", offset);
    if (claim(b, offset, length, "a byte patch") != 0)
        return -1;
    if (b->records_size + 5 + length > sizeof(b->records) || length > 255)
        return fail(b->message, b->message_size, "too many byte patches");
    memcpy(b->records + b->records_size, &offset, sizeof(offset));
    b->records[b->records_size + 4] = (uint8_t)length;
    memcpy(b->records + b->records_size + 5, original, length);
    b->records_size += 5 + length;
    b->patch_count++;
    if (patched != NULL)
        memcpy(b->data + offset, patched, length);
    else
        memset(b->data + offset, 0x90, length);
    return 0;
}

// the patch network_version_patch makes for each of these: the first match within 16 bytes of the address
static int patch_near(build_t *b, const uint32_t *addresses, size_t count, const void *original, const void *patched,
                      uint32_t length) {
    for (size_t i = 0; i < count; i++) {
        int o = 0;
        while (o < 16 && memcmp(b->data + addresses[i] + o, original, length) != 0)
            o++;
        if (o == 16)
            return fail(b->message, b->message_size, "unexpected code near %08x", addresses[i]);
        if (patch(b, addresses[i] + o, original, patched, length) != 0)
            return -1;
    }
    return 0;
}

static void put_rel32(uint8_t *at, uint64_t target, uint64_t next) {
    int32_t rel = (int32_t)((int64_t)target - (int64_t)next);
    memcpy(at, &rel, sizeof(rel));
}

// checks that the padding and slots BO3MacFix uses are where it expects in this executable
static int check_layout(build_t *b) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)b->data;
    if (b->size < sizeof(*mh) || mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_X86_64 || mh->filetype != MH_EXECUTE ||
        sizeof(*mh) + mh->sizeofcmds > b->size)
        return fail(b->message, b->message_size, "it isn't the x86_64 game executable");
    uint64_t text_file_end = 0, text_sections_end = 0, data_start = 0, data_end = 0, data_sections_end = 0;
    const uint8_t *p = b->data + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmdsize < sizeof(*lc) || p + lc->cmdsize > b->data + sizeof(*mh) + mh->sizeofcmds)
            return fail(b->message, b->message_size, "its load commands are damaged");
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            const struct section_64 *sections = (const struct section_64 *)(seg + 1);
            uint64_t sections_end = 0;
            for (uint32_t s = 0; s < seg->nsects; s++) {
                if (sections[s].addr + sections[s].size > sections_end)
                    sections_end = sections[s].addr + sections[s].size;
            }
            if (strcmp(seg->segname, "__TEXT") == 0 && seg->vmaddr == Game_Base && seg->fileoff == 0) {
                text_file_end = seg->filesize;
                text_sections_end = sections_end - Game_Base;
            } else if (strcmp(seg->segname, "__DATA") == 0) {
                data_start = seg->vmaddr - Game_Base;
                data_end = data_start + seg->vmsize;
                data_sections_end = sections_end - Game_Base;
            }
        }
        p += lc->cmdsize;
    }
    if (text_sections_end == 0 || text_sections_end > ADDR_Prepatch_Header || ADDR_Prepatch_CodeEnd > text_file_end ||
        data_sections_end == 0 || data_sections_end > ADDR_Prepatch_Slots || ADDR_Prepatch_SlotsEnd > data_end ||
        ADDR_Prepatch_Slots < data_start)
        return fail(b->message, b->message_size, "it doesn't have room where this BO3MacFix expects it");
    for (uint32_t o = ADDR_Prepatch_Header; o < ADDR_Prepatch_CodeEnd; o++) {
        if (b->data[o] != 0)
            return fail(b->message, b->message_size, "the padding after its code isn't empty");
    }
    return 0;
}

static int sha256_file(const char *path, uint8_t digest[CC_SHA256_DIGEST_LENGTH], uint8_t **contents, size_t *size) {
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    uint8_t *data = malloc(st.st_size);
    size_t done = 0;
    while (data != NULL && done < (size_t)st.st_size) {
        ssize_t n = read(fd, data + done, st.st_size - done);
        if (n <= 0)
            break;
        done += n;
    }
    close(fd);
    if (data == NULL || done != (size_t)st.st_size) {
        free(data);
        return -1;
    }
    CC_SHA256(data, (CC_LONG)done, digest);
    if (contents != NULL) {
        *contents = data;
        *size = done;
    } else {
        free(data);
    }
    return 0;
}

EXPORT int BO3MacFix_PrepatchedIsCurrent(const char *original, const char *copy, char *message, size_t message_size) {
    prepatch_header_t h;
    FILE *fp = fopen(copy, "rb");
    if (fp == NULL)
        return fail(message, message_size, "there's no pre-patched executable yet");
    bool ok = fseek(fp, ADDR_Prepatch_Header, SEEK_SET) == 0 && fread(&h, sizeof(h), 1, fp) == 1;
    fclose(fp);
    if (!ok || memcmp(h.magic, Prepatch_Magic, sizeof(h.magic)) != 0 || h.version != Prepatch_Version)
        return fail(message, message_size, "the pre-patched executable is from an older BO3MacFix");
    if (h.table_id != table_id())
        return fail(message, message_size, "the pre-patched executable was made by a different BO3MacFix");
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    if (sha256_file(original, digest, NULL, NULL) != 0)
        return fail(message, message_size, "couldn't read %s", original);
    if (memcmp(digest, h.source_sha256, sizeof(digest)) != 0)
        return fail(message, message_size, "the game was updated since the pre-patched executable was made");
    snprintf(message, message_size, "the pre-patched executable is up to date");
    return 0;
}

EXPORT int BO3MacFix_BuildPrepatched(const char *original, const char *output, char *message, size_t message_size) {
    build_t *b = calloc(1, sizeof(build_t));
    if (b == NULL)
        return fail(message, message_size, "out of memory");
    b->message = message;
    b->message_size = message_size;
    prepatch_header_t h = { .version = Prepatch_Version, .table_id = table_id() };
    memcpy(h.magic, Prepatch_Magic, sizeof(h.magic));
    int result = -1;
    const size_t hook_count = sizeof(hook_defs) / sizeof(hook_defs[0]);
    const size_t value_count = sizeof(value_defs) / sizeof(value_defs[0]);
    const size_t call_count = sizeof(call_defs) / sizeof(call_defs[0]);
    prepatch_hook_t hook_records[sizeof(hook_defs) / sizeof(hook_defs[0])];
    prepatch_site_t value_records[sizeof(value_defs) / sizeof(value_defs[0])];
    prepatch_site_t call_records[sizeof(call_defs) / sizeof(call_defs[0])];
    memset(hook_records, 0, sizeof(hook_records));
    memset(value_records, 0, sizeof(value_records));
    memset(call_records, 0, sizeof(call_records));

    if (sha256_file(original, h.source_sha256, &b->data, &b->size) != 0) {
        fail(message, message_size, "couldn't read %s", original);
        goto done;
    }
    if (check_layout(b) != 0)
        goto done;

    // the byte patches
    for (size_t i = 0; i < sizeof(patch_defs) / sizeof(patch_defs[0]); i++) {
        if (patch(b, patch_defs[i].offset, patch_defs[i].original, patch_defs[i].patched, patch_defs[i].length) != 0)
            goto done;
    }
    if (patch_near(b, netver_addresses, sizeof(netver_addresses) / sizeof(netver_addresses[0]), &netver_original,
                   &netver_patched, sizeof(netver_original)) != 0 ||
        patch_near(b, tuver_addresses, sizeof(tuver_addresses) / sizeof(tuver_addresses[0]), tuver_original,
                   tuver_patched, sizeof(tuver_original)) != 0)
        goto done;

    // the header and its records come first in the padding, then the stubs
    h.hook_count = (uint32_t)hook_count;
    h.value_count = (uint32_t)value_count;
    h.call_count = (uint32_t)call_count;
    h.patch_count = b->patch_count;
    h.hooks_offset = ADDR_Prepatch_Header + sizeof(h);
    h.values_offset = h.hooks_offset + sizeof(hook_records);
    h.calls_offset = h.values_offset + sizeof(value_records);
    h.patches_offset = h.calls_offset + sizeof(call_records);
    uint32_t header_end = h.patches_offset + (uint32_t)b->records_size;
    uint32_t code = (header_end + 15) & ~15u, slot = ADDR_Prepatch_Slots;
    if (claim(b, ADDR_Prepatch_Header, header_end - ADDR_Prepatch_Header, "the header") != 0)
        goto done;

    for (size_t i = 0; i < hook_count; i++) {
        const hook_def_t *def = &hook_defs[i];
        uint32_t f = def->function, tramp = code + 16;
        if (memcmp(b->data + f, def->stolen, def->length) != 0 || def->length < 5) {
            fail(message, message_size, "unexpected code at %08x", f);
            goto done;
        }
        uint8_t stub[64];
        size_t n = 0;
        stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0x3D;               // cmp qword [rip+slot], 0
        put_rel32(stub + n, slot, code + 8); n += 4;
        stub[n++] = 0x00;
        stub[n++] = 0x74; stub[n++] = 0x06;                                  // je trampoline
        stub[n++] = 0xFF; stub[n++] = 0x25;                                  // jmp [rip+slot]
        put_rel32(stub + n, slot, code + 16); n += 4;
        if (def->short_jcc_at != 0) {
            // the instructions before it, then the same condition as a near jump to the same target
            size_t at = def->short_jcc_at - 1;
            uint64_t target = f + at + 2 + (int8_t)def->stolen[at + 1];
            memcpy(stub + n, def->stolen, at); n += at;
            stub[n++] = 0x0F; stub[n++] = 0x80 | (def->stolen[at] & 0x0F);
            put_rel32(stub + n, target, code + n + 4); n += 4;
            memcpy(stub + n, def->stolen + at + 2, def->length - at - 2); n += def->length - at - 2;
        } else {
            memcpy(stub + n, def->stolen, def->length); n += def->length;
        }
        stub[n++] = 0xE9;                                                    // jmp back
        put_rel32(stub + n, f + def->length, code + n + 4); n += 4;
        if (claim(b, code, (uint32_t)n, "a hook stub") != 0 || claim(b, f, def->length, "a hook") != 0)
            goto done;
        memcpy(b->data + code, stub, n);
        b->data[f] = 0xE9;
        put_rel32(b->data + f + 1, code, f + 5);
        memset(b->data + f + 5, 0x90, def->length - 5);
        hook_records[i] = (prepatch_hook_t){ .function = f, .slot = slot, .trampoline = tramp, .length = def->length };
        memcpy(hook_records[i].original, def->stolen, def->length);
        memcpy(hook_records[i].original + def->length, b->data + f + def->length, 16 - def->length);
        slot += 8;
        code = (code + (uint32_t)n + 15) & ~15u;
    }

    for (size_t i = 0; i < value_count; i++) {
        const value_def_t *def = &value_defs[i];
        uint32_t site = def->site;
        uint8_t reg = def->original[0] - 0xB8;
        if (memcmp(b->data + site, def->original, 5) != 0 || def->original[0] < 0xB8 || def->original[0] > 0xBF) {
            fail(message, message_size, "unexpected code at %08x", site);
            goto done;
        }
        uint8_t stub[32];
        size_t n = 0;
        stub[n++] = 0x8B; stub[n++] = 0x05 | (reg << 3);                     // mov r32, [rip+slot]
        put_rel32(stub + n, slot, code + 6); n += 4;
        stub[n++] = 0x85; stub[n++] = 0xC0 | (reg << 3) | reg;               // test r32, r32
        stub[n++] = 0x75; stub[n++] = 0x05;                                  // jnz past the original
        memcpy(stub + n, def->original, 5); n += 5;                          // mov r32, original value
        stub[n++] = 0xE9;                                                    // jmp back
        put_rel32(stub + n, site + 5, code + n + 4); n += 4;
        if (claim(b, code, (uint32_t)n, "a setting stub") != 0 || claim(b, site, 5, "a setting") != 0)
            goto done;
        memcpy(b->data + code, stub, n);
        b->data[site] = 0xE9;
        put_rel32(b->data + site + 1, code, site + 5);
        value_records[i] = (prepatch_site_t){ .site = site, .slot = slot, .length = 5 };
        memcpy(value_records[i].original, def->original, 5);
        slot += 8;
        code = (code + (uint32_t)n + 15) & ~15u;
    }

    for (size_t i = 0; i < call_count; i++) {
        const call_def_t *def = &call_defs[i];
        uint32_t site = def->site;
        if (memcmp(b->data + site, def->original, 6) != 0) {
            fail(message, message_size, "unexpected code at %08x", site);
            goto done;
        }
        int32_t disp;
        memcpy(&disp, def->original + 2, sizeof(disp));
        uint64_t pointer = site + 6 + (int64_t)disp; // where the call reads objc_msgSend's address from
        uint8_t stub[32];
        size_t n = 0;
        stub[n++] = 0x48; stub[n++] = 0x83; stub[n++] = 0x3D;               // cmp qword [rip+slot], 0
        put_rel32(stub + n, slot, code + 8); n += 4;
        stub[n++] = 0x00;
        stub[n++] = 0x74; stub[n++] = 0x06;                                  // je the original call
        stub[n++] = 0xFF; stub[n++] = 0x25;                                  // jmp [rip+slot]
        put_rel32(stub + n, slot, code + 16); n += 4;
        stub[n++] = 0xFF; stub[n++] = 0x25;                                  // jmp [rip+objc_msgSend pointer]
        put_rel32(stub + n, pointer, code + 22); n += 4;
        if (claim(b, code, (uint32_t)n, "a call stub") != 0 || claim(b, site, 6, "a call") != 0)
            goto done;
        memcpy(b->data + code, stub, n);
        b->data[site] = 0xE8;                                                // call stub; nop
        put_rel32(b->data + site + 1, code, site + 5);
        b->data[site + 5] = 0x90;
        call_records[i] = (prepatch_site_t){ .site = site, .slot = slot, .length = 6 };
        memcpy(call_records[i].original, def->original, 6);
        slot += 8;
        code = (code + (uint32_t)n + 15) & ~15u;
    }

    if (code > ADDR_Prepatch_CodeEnd || slot > ADDR_Prepatch_SlotsEnd) {
        fail(message, message_size, "out of room for the stubs");
        goto done;
    }
    memcpy(b->data + ADDR_Prepatch_Header, &h, sizeof(h));
    memcpy(b->data + h.hooks_offset, hook_records, sizeof(hook_records));
    memcpy(b->data + h.values_offset, value_records, sizeof(value_records));
    memcpy(b->data + h.calls_offset, call_records, sizeof(call_records));
    memcpy(b->data + h.patches_offset, b->records, b->records_size);

    int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        fail(message, message_size, "couldn't create %s", output);
        goto done;
    }
    size_t written = 0;
    while (written < b->size) {
        ssize_t n = write(fd, b->data + written, b->size - written);
        if (n <= 0)
            break;
        written += n;
    }
    fchmod(fd, 0755);
    if (close(fd) != 0 || written != b->size) {
        unlink(output);
        fail(message, message_size, "couldn't write %s", output);
        goto done;
    }
    snprintf(message, message_size, "made the pre-patched executable: %zu hooks, %zu settings, %zu calls, %u byte "
             "patches, %u of %u bytes of padding used", hook_count, value_count, call_count, b->patch_count,
             code - ADDR_Prepatch_Header, ADDR_Prepatch_CodeEnd - ADDR_Prepatch_Header);
    result = 0;
done:
    free(b->data);
    free(b);
    return result;
}
