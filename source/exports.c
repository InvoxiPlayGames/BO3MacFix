#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "dobby.h"
#include "utilities.h"

#define EXPORT __attribute__((visibility("default")))

extern uint64_t game_base_address;

EXPORT uint64_t BO3MacFix_GetBaseAddress() {
    return game_base_address;
}

EXPORT int BO3MacFix_DobbyCodePatch(void *address, uint8_t *buffer, uint32_t buffer_size) {
    return DobbyCodePatch(address, buffer, buffer_size);
}

EXPORT int BO3MacFix_DobbyHook(void *address, void *fake_func, void **out_origin_func) {
    return DobbyHook(address, fake_func, out_origin_func);
}
