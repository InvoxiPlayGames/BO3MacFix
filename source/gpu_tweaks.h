#ifndef _GPU_TWEAKS_H
#define _GPU_TWEAKS_H

#include <stdint.h>

// checked_code_patch from bo3macfix.c
typedef void (*code_patch_fn)(uint32_t address, const uint8_t *expected, uint8_t *patch, uint32_t size);

// frames_in_flight: 1 to 3 (2 leaves Aspyr's default), pacing_fps: 30 to 240 (0 leaves it off)
void gpu_tweaks_init(int frames_in_flight, int pacing_fps, code_patch_fn checked_code_patch);

// metalfx_upscale: 1 upscales to the screen's native size with MetalFX when the game runs at a lower resolution, 0 = off
void gpu_tweaks_metalfx_init(int metalfx_upscale, code_patch_fn checked_code_patch);

#endif
