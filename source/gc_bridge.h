#pragma once
#include <stdbool.h>
#include <stdint.h>

// DualSense and DualShock 4 controllers through macOS's GameController framework instead of the game's SDL 2.0.7:
// works over Bluetooth and USB, with rumble and the lightbar. Needs macOS 11.3 or newer.
// mode: 0 = off, 1 = on. lightbar_rgb: 0xRRGGBB, or -1 to leave the light alone.
// BO3MACFIX_CONTROLLERBRIDGE and BO3MACFIX_LIGHTBARCOLOR (RRGGBB) at env override both.
void gc_bridge_init(uint64_t base_address, int mode, int lightbar_rgb);

// is the bridge on? controllers.c then keeps the game's SDL away from Sony controllers, so they don't give double input
bool gc_bridge_enabled(void);

// is a PlayStation controller from the bridge on this XInput user index? for ButtonPrompts = 0
bool gc_bridge_is_playstation_user(int user_index);
