// The pre-patched executable: a copy of CoDBlkOps3_Exe with BO3MacFix's changes to the game's code already made in
// the file.
//
// Writing to the game's code at runtime makes Rosetta give up its ahead-of-time translation of the whole game and
// run it through its slower JIT. The launcher builds the copy (BO3MacFix_BuildPrepatched) next to the original and
// starts it instead; running from the copy, hooks only store a pointer and nothing writes the game's code.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// the copy's file name, next to CoDBlkOps3_Exe
#define Prepatched_Exe_Name "CoDBlkOps3_Exe.bo3macfix"

// call once with the game's base address, returns true when running from the pre-patched copy
bool prepatch_init(uint64_t base_address);

// whether we're running from the pre-patched copy
bool prepatch_active(void);

// the game's code as shipped at address (up to 64 bytes), for checks before patching or hooking: the pre-patched
// copy has changed some of it already. Returns address itself when nothing there was changed.
const void *prepatch_original_code(const void *address, size_t size);

// points the call through objc_msgSend's pointer at offset at function, which the pre-patched copy does through a
// slot. Returns false when not running from the copy, or it has no such call.
bool prepatch_redirect_call(uint32_t offset, void *function);

// prints what was set up from the pre-patched copy, and whether any game code page was written
void prepatch_report(void);

// for the launcher, which loads BO3MacFix.dylib to call these. Both return 0 on success, otherwise -1 with the
// reason in message.
// whether the copy at copy is up to date for the original at original and this BO3MacFix
int BO3MacFix_PrepatchedIsCurrent(const char *original, const char *copy, char *message, size_t message_size);
// writes a pre-patched copy of the original at original to output (it still has to be signed)
int BO3MacFix_BuildPrepatched(const char *original, const char *output, char *message, size_t message_size);
