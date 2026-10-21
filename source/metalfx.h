#pragma once

// MetalFX spatial upscaling of the whole frame when Aspyr's Metal layer presents it, see metalfx.m.

// checks that MetalFX and Aspyr's Metal view are there and starts keeping the drawable at the screen's native size,
// returns 1 if upscaling is on, otherwise logs why not and returns 0
int metalfx_init(void);

// replacement for Aspyr's [blitEncoder copyFromTexture:backbuffer ... toTexture:drawable.texture ...] at 0x1000e8fde
void *metalfx_copy_function(void);

// encodes the upscale of this frame, call right before [commandBuffer presentDrawable:]
void metalfx_before_present(void *command_buffer);
