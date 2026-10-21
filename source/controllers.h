#pragma once
#include <stdint.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDDevice.h>

// PlayStation controller support: SDL mappings for the DualSense and DualShock 4, and PlayStation button prompts.
// add_mappings: 0 = off, 1 = on. button_prompts: 0 = auto, 1 = always Xbox (stock), 2 = always PlayStation.
void controllers_init(uint64_t base_address, int add_mappings, int button_prompts);

// called from the IOHIDDeviceGetProperty interposer, returns the value BO3's SDL 2.0.7 should see
CFTypeRef controllers_hid_property(IOHIDDeviceRef device, CFStringRef key, CFTypeRef value);
