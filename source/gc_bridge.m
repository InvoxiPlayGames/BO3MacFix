// PlayStation controllers through macOS's GameController framework, fed into Aspyr's XInput emulation.
//
// The game reads controllers with a built-in SDL 2.0.7 that only knows IOKit HID: it can't read a DualSense over
// Bluetooth, and its rumble goes through ForceFeedback, which has no driver for Sony controllers. With
// ControllerBridge = 1, controllers.c hides Sony controllers from that SDL, and this file answers Aspyr's
// XInputGetState/XInputSetState for them instead, over USB or Bluetooth, with rumble through CoreHaptics and the
// lightbar colour from settings.ini. The game's own code only ever calls those two XInput functions.
//
// Aspyr keeps an SDL controller per XInput user index (player) and gives a new one the first free slot. A bridged
// controller takes the first index SDL isn't using, and moves if SDL later puts a controller of its own there.
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dobby.h"
#include "offsets.h"
#include "gc_bridge.h"
#include "prepatch.h"

#define XUSER_MAX_COUNT 4
#define ERROR_SUCCESS 0
#define SONY_VENDOR_ID 0x054C

typedef struct {
    uint16_t wButtons;
    uint8_t bLeftTrigger;
    uint8_t bRightTrigger;
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} XINPUT_GAMEPAD;

typedef struct {
    uint32_t dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct {
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
} XINPUT_VIBRATION;

// one controller from the GameController framework. Objective-C objects are kept as id so this builds for
// macOS 10.13, only code behind @available(macOS 11.3) touches them.
typedef struct {
    // under pads_lock:
    id controller;              // GCController, retained, nil = free entry
    const char *kind;           // "DualSense" or "DualShock 4"
    int user_index;             // XInput user index the game reads it on, -1 = waiting for a free one
    XINPUT_STATE state;
    uint16_t wanted_motors[2];  // last speeds from XInputSetState
    // only on input_queue:
    id engines[2];              // CHHapticEngine for the left (large) and right (small) motor, or one for both
    id players[2];              // CHHapticAdvancedPatternPlayer
    int engine_count;
    uint16_t motors[2];         // speeds the players are running at
    double haptics_retry_at;    // CFAbsoluteTime after which setting up rumble may be tried again
} bridged_pad;

static uint64_t base_address;
static bool enabled;
static int lightbar_color = -1;
static atomic_bool app_active = true;
static os_unfair_lock pads_lock = OS_UNFAIR_LOCK_INIT;
static bridged_pad pads[XUSER_MAX_COUNT];
static dispatch_queue_t input_queue;

// Aspyr's XInput slots, { SDL_GameController *; SDL_Haptic *; int effect; }[4]
static bool sdl_slot_in_use(int user_index) {
    return *(void *volatile *)(base_address + ADDR_ASL_XInputSlots + user_index * 24) != NULL;
}

// these all need pads_lock held
static bool user_index_free(int user_index) {
    if (sdl_slot_in_use(user_index))
        return false;
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
        if (pads[i].controller != nil && pads[i].user_index == user_index)
            return false;
    return true;
}

static int first_free_user_index(void) {
    for (int u = 0; u < XUSER_MAX_COUNT; u++)
        if (user_index_free(u))
            return u;
    return -1;
}

static bridged_pad *pad_for_user(int user_index) {
    if (sdl_slot_in_use(user_index))
        return NULL;
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
        if (pads[i].controller != nil && pads[i].user_index == user_index)
            return &pads[i];
    return NULL;
}

static void player_changed(bridged_pad *pad, int old_index) {
    if (pad->user_index < 0)
        printf("GameController bridge: player %i was taken by another controller, %s waits for a free slot\n",
               old_index + 1, pad->kind);
    else if (old_index < 0)
        printf("GameController bridge: %s is now player %i\n", pad->kind, pad->user_index + 1);
    else
        printf("GameController bridge: player %i was taken by another controller, %s moved to player %i\n",
               old_index + 1, pad->kind, pad->user_index + 1);
    if (@available(macOS 11.3, *)) {
        GCController *controller = [pad->controller retain];
        GCControllerPlayerIndex index = pad->user_index >= 0 ? (GCControllerPlayerIndex)pad->user_index : GCControllerPlayerIndexUnset;
        dispatch_async(dispatch_get_main_queue(), ^{
            controller.playerIndex = index;
            [controller release];
        });
    }
}

// SDL takes the first free slot when a controller it handles connects (an Xbox controller, say), which can be the
// one a bridged controller is on. Move bridged controllers off slots SDL has taken, and give waiting ones a free one.
static void rehome_pads(void) {
    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        bridged_pad *pad = &pads[i];
        if (pad->controller == nil || (pad->user_index >= 0 && !sdl_slot_in_use(pad->user_index)))
            continue;
        int old_index = pad->user_index;
        pad->user_index = -1;
        pad->user_index = first_free_user_index();
        if (pad->user_index != old_index)
            player_changed(pad, old_index);
    }
}

#pragma mark - input

static int16_t stick_value(float value) {
    float scaled = value * 32767.0f;
    if (scaled > 32767.0f)
        return 32767;
    if (scaled < -32768.0f)
        return -32768;
    return (int16_t)scaled;
}

// runs on input_queue whenever anything on the controller changes
API_AVAILABLE(macos(11.3))
static void update_state(bridged_pad *pad, GCExtendedGamepad *gamepad) {
    uint16_t buttons = 0;
    if (gamepad.dpad.up.pressed) buttons |= 0x0001;
    if (gamepad.dpad.down.pressed) buttons |= 0x0002;
    if (gamepad.dpad.left.pressed) buttons |= 0x0004;
    if (gamepad.dpad.right.pressed) buttons |= 0x0008;
    if (gamepad.buttonMenu.pressed) buttons |= 0x0010;              // Options -> Start
    if (gamepad.buttonOptions.pressed) buttons |= 0x0020;           // Create/Share -> Back
    if (gamepad.leftThumbstickButton.pressed) buttons |= 0x0040;
    if (gamepad.rightThumbstickButton.pressed) buttons |= 0x0080;
    if (gamepad.leftShoulder.pressed) buttons |= 0x0100;
    if (gamepad.rightShoulder.pressed) buttons |= 0x0200;
    if (gamepad.buttonA.pressed) buttons |= 0x1000;                 // Cross
    if (gamepad.buttonB.pressed) buttons |= 0x2000;                 // Circle
    if (gamepad.buttonX.pressed) buttons |= 0x4000;                 // Square
    if (gamepad.buttonY.pressed) buttons |= 0x8000;                 // Triangle
    // the touchpad click is the scoreboard on PlayStation, which is Back for the game
    if ([gamepad isKindOfClass:[GCDualSenseGamepad class]] && ((GCDualSenseGamepad *)gamepad).touchpadButton.pressed)
        buttons |= 0x0020;
    if ([gamepad isKindOfClass:[GCDualShockGamepad class]] && ((GCDualShockGamepad *)gamepad).touchpadButton.pressed)
        buttons |= 0x0020;

    XINPUT_GAMEPAD state = {
        .wButtons = buttons,
        .bLeftTrigger = (uint8_t)(gamepad.leftTrigger.value * 255.0f + 0.5f),
        .bRightTrigger = (uint8_t)(gamepad.rightTrigger.value * 255.0f + 0.5f),
        .sThumbLX = stick_value(gamepad.leftThumbstick.xAxis.value),
        .sThumbLY = stick_value(gamepad.leftThumbstick.yAxis.value),
        .sThumbRX = stick_value(gamepad.rightThumbstick.xAxis.value),
        .sThumbRY = stick_value(gamepad.rightThumbstick.yAxis.value),
    };
    os_unfair_lock_lock(&pads_lock);
    // a late update from a controller that has since disconnected must not land on whatever took its entry
    if (pad->controller == gamepad.controller) {
        pad->state.dwPacketNumber++;
        pad->state.Gamepad = state;
    }
    os_unfair_lock_unlock(&pads_lock);
}

// DWORD XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
install_hook_name(ASL_XInputGetState, uint32_t, uint32_t user_index, XINPUT_STATE *state) {
    if (enabled && user_index < XUSER_MAX_COUNT) {
        os_unfair_lock_lock(&pads_lock);
        rehome_pads();
        bridged_pad *pad = pad_for_user(user_index);
        if (pad != NULL) {
            // like Aspyr's, a neutral state while XInput is disabled, and also while the game isn't focused
            bool active = atomic_load(&app_active) && (*(volatile uint8_t *)(base_address + ADDR_ASL_XInputEnabled) & 1);
            if (active)
                *state = pad->state;
            else
                memset(state, 0, sizeof(*state));
            os_unfair_lock_unlock(&pads_lock);
            return ERROR_SUCCESS;
        }
        os_unfair_lock_unlock(&pads_lock);
    }
    return orig_ASL_XInputGetState(user_index, state);
}

#pragma mark - rumble

API_AVAILABLE(macos(11.3))
static id make_player(CHHapticEngine *engine) {
    // a continuous event that never ends, or if CoreHaptics won't take that, a 30 second one that loops
    const float durations[2] = { GCHapticDurationInfinite, 30.0f };
    for (int i = 0; i < 2; i++) {
        NSError *error = nil;
        CHHapticEventParameter *intensity =
            [[[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1.0f] autorelease];
        CHHapticEvent *event = [[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                              parameters:@[ intensity ]
                                                            relativeTime:0
                                                                duration:durations[i]] autorelease];
        CHHapticPattern *pattern = [[[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error] autorelease];
        id<CHHapticAdvancedPatternPlayer> player = pattern != nil ? [engine createAdvancedPlayerWithPattern:pattern error:&error] : nil;
        if (player != nil) {
            if (i == 1) {
                player.loopEnabled = YES;
                player.loopEnd = durations[i];
            }
            return [player retain];
        }
        printf("GameController bridge: couldn't make the %s rumble pattern: %s\n", i == 0 ? "endless" : "looping",
               error.localizedDescription.UTF8String ?: "unknown error");
    }
    return nil;
}

// the motors run as continuous events at full strength, scaled by a dynamic intensity, so speed changes don't
// restart anything
API_AVAILABLE(macos(11.3))
static void set_motors(bridged_pad *pad, uint16_t left, uint16_t right) {
    uint16_t speeds[2] = { left, right };
    if (pad->engine_count == 1)
        speeds[0] = left > right ? left : right;    // one engine for both plays the stronger one
    for (int i = 0; i < pad->engine_count; i++) {
        id<CHHapticAdvancedPatternPlayer> player = pad->players[i];
        if (player == nil || pad->motors[i] == speeds[i])
            continue;
        NSError *error = nil;
        if (speeds[i] == 0) {
            [player stopAtTime:CHHapticTimeImmediate error:&error];
        } else {
            // macOS can stop an engine on its own (the game lost focus, say), start it again if so
            if (pad->motors[i] == 0 && ![player startAtTime:CHHapticTimeImmediate error:&error] &&
                [pad->engines[i] startAndReturnError:nil])
                [player startAtTime:CHHapticTimeImmediate error:&error];
            CHHapticDynamicParameter *intensity =
                [[[CHHapticDynamicParameter alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                                                                 value:speeds[i] / 65535.0f
                                                          relativeTime:0] autorelease];
            [player sendParameters:@[ intensity ] atTime:CHHapticTimeImmediate error:&error];
        }
        pad->motors[i] = speeds[i];
    }
}

// on input_queue: play what the game last asked for, or nothing while it isn't focused
API_AVAILABLE(macos(11.3))
static void start_haptics(bridged_pad *pad, GCController *controller, int user_index);

API_AVAILABLE(macos(11.3))
static void apply_motors(bridged_pad *pad) {
    os_unfair_lock_lock(&pads_lock);
    uint16_t left = pad->wanted_motors[0], right = pad->wanted_motors[1];
    GCController *controller = [pad->controller retain];
    int user_index = pad->user_index;
    os_unfair_lock_unlock(&pads_lock);
    if (controller == nil || !atomic_load(&app_active))
        left = right = 0;
    // rumble couldn't be set up when the controller connected (the game may not have been frontmost yet),
    // so try again when the game first asks for it, at most every 3 seconds
    if (pad->engine_count == 0 && (left != 0 || right != 0) && CFAbsoluteTimeGetCurrent() >= pad->haptics_retry_at)
        start_haptics(pad, controller, user_index);
    [controller release];
    set_motors(pad, left, right);
}

API_AVAILABLE(macos(11.3))
static void restart_haptics(bridged_pad *pad) {
    // after a reset (the haptics server restarted, the Mac slept) each engine has to be started again and its
    // players made again
    for (int i = 0; i < pad->engine_count; i++) {
        [pad->players[i] release];
        pad->players[i] = nil;
        if ([pad->engines[i] startAndReturnError:nil])
            pad->players[i] = make_player(pad->engines[i]);
    }
    pad->motors[0] = pad->motors[1] = 0;
    apply_motors(pad);
}

// on input_queue. DualSense and DualShock 4 have a motor in each grip, so one engine per grip where macOS offers it
// makes, starts and keeps one rumble engine for this locality, returns 0 (saying why) if macOS won't
API_AVAILABLE(macos(11.3))
static int add_engine(bridged_pad *pad, GCDeviceHaptics *haptics, GCHapticsLocality locality, int user_index) {
    CHHapticEngine *engine = [haptics createEngineWithLocality:locality];
    if (engine == nil) {
        printf("GameController bridge: macOS made no rumble engine for %s on player %i\n", locality.UTF8String, user_index + 1);
        return 0;
    }
    engine.playsHapticsOnly = YES;
    NSError *error = nil;
    if (![engine startAndReturnError:&error]) {
        printf("GameController bridge: couldn't start rumble (%s) on player %i: %s\n", locality.UTF8String, user_index + 1,
               error.localizedDescription.UTF8String ?: "unknown error");
        return 0;
    }
    id player = make_player(engine);
    if (player == nil) {
        [engine stopWithCompletionHandler:nil];
        return 0;
    }
    engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
        printf("GameController bridge: rumble engine on player %i stopped (reason %li)\n", user_index + 1, (long)reason);
    };
    engine.resetHandler = ^{
        dispatch_async(input_queue, ^{
            @autoreleasepool {
                restart_haptics(pad);
            }
        });
    };
    pad->engines[pad->engine_count] = [engine retain];
    pad->players[pad->engine_count] = player;
    pad->engine_count++;
    return 1;
}

API_AVAILABLE(macos(11.3))
static void start_haptics(bridged_pad *pad, GCController *controller, int user_index) {
    pad->haptics_retry_at = CFAbsoluteTimeGetCurrent() + 3.0;
    GCDeviceHaptics *haptics = controller.haptics;
    if (haptics == nil) {
        printf("GameController bridge: no rumble on player %i, macOS gives no haptics for this controller\n", user_index + 1);
        return;
    }
    NSSet *localities = haptics.supportedLocalities;
    bool grips = [localities containsObject:GCHapticsLocalityLeftHandle] && [localities containsObject:GCHapticsLocalityRightHandle];
    pad->engine_count = 0;
    // one engine per grip for the game's two motors, otherwise (or if that fails) one engine for both
    if (grips && add_engine(pad, haptics, GCHapticsLocalityLeftHandle, user_index))
        add_engine(pad, haptics, GCHapticsLocalityRightHandle, user_index);
    if (pad->engine_count == 0)
        add_engine(pad, haptics, GCHapticsLocalityDefault, user_index);
    if (pad->engine_count == 2)
        printf("GameController bridge: rumble ready on player %i (left and right grip)\n", user_index + 1);
    else if (pad->engine_count == 1)
        printf("GameController bridge: rumble ready on player %i (both motors on one engine)\n", user_index + 1);
    else
        printf("GameController bridge: no rumble on player %i\n", user_index + 1);
}

API_AVAILABLE(macos(11.3))
static void stop_haptics(bridged_pad *pad) {
    for (int i = 0; i < pad->engine_count; i++) {
        [pad->players[i] stopAtTime:CHHapticTimeImmediate error:nil];
        [pad->players[i] release];
        [pad->engines[i] stopWithCompletionHandler:nil];
        [pad->engines[i] release];
        pad->players[i] = nil;
        pad->engines[i] = nil;
    }
    pad->engine_count = 0;
    pad->motors[0] = pad->motors[1] = 0;
}

// DWORD XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
install_hook_name(ASL_XInputSetState, uint32_t, uint32_t user_index, XINPUT_VIBRATION *vibration) {
    if (enabled && user_index < XUSER_MAX_COUNT && vibration != NULL) {
        os_unfair_lock_lock(&pads_lock);
        bridged_pad *pad = pad_for_user(user_index);
        if (pad != NULL) {
            uint16_t left = vibration->wLeftMotorSpeed, right = vibration->wRightMotorSpeed;
            bool changed = pad->wanted_motors[0] != left || pad->wanted_motors[1] != right;
            pad->wanted_motors[0] = left;
            pad->wanted_motors[1] = right;
            os_unfair_lock_unlock(&pads_lock);
            // the game sends the same speeds every frame, only pass on changes
            if (changed) {
                if (@available(macOS 11.3, *)) {
                    dispatch_async(input_queue, ^{
                        @autoreleasepool {
                            apply_motors(pad);
                        }
                    });
                }
            }
            return ERROR_SUCCESS;
        }
        os_unfair_lock_unlock(&pads_lock);
    }
    return orig_ASL_XInputSetState(user_index, vibration);
}

#pragma mark - controllers coming and going

static bool is_playstation_product(int product) {
    return product == 0x05C4 || product == 0x09CC || product == 0x0BA0 || product == 0x0CE6 || product == 0x0DF2;
}

// the GameController framework doesn't say how a controller is connected, IOKit does
static const char *sony_transport(void) {
    bool usb = false, bluetooth = false;
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (manager == NULL)
        return "unknown connection";
    int vendor = SONY_VENDOR_ID;
    CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    const void *keys[] = { CFSTR(kIOHIDVendorIDKey) };
    const void *values[] = { vendor_ref };
    CFDictionaryRef matching = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(manager, matching);
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (devices != NULL) {
        CFIndex count = CFSetGetCount(devices);
        IOHIDDeviceRef *list = calloc(count, sizeof(IOHIDDeviceRef));
        CFSetGetValues(devices, (const void **)list);
        for (CFIndex i = 0; i < count; i++) {
            int product = 0;
            CFTypeRef product_ref = IOHIDDeviceGetProperty(list[i], CFSTR(kIOHIDProductIDKey));
            if (product_ref != NULL && CFGetTypeID(product_ref) == CFNumberGetTypeID())
                CFNumberGetValue(product_ref, kCFNumberIntType, &product);
            CFTypeRef transport = IOHIDDeviceGetProperty(list[i], CFSTR(kIOHIDTransportKey));
            if (!is_playstation_product(product) || transport == NULL || CFGetTypeID(transport) != CFStringGetTypeID())
                continue;
            if (CFStringCompare(transport, CFSTR("USB"), 0) == kCFCompareEqualTo)
                usb = true;
            else if (CFStringHasPrefix(transport, CFSTR("Bluetooth")))
                bluetooth = true;
        }
        free(list);
        CFRelease(devices);
    }
    CFRelease(matching);
    CFRelease(vendor_ref);
    CFRelease(manager);
    return usb && bluetooth ? "USB or Bluetooth" : usb ? "USB" : bluetooth ? "Bluetooth" : "unknown connection";
}

API_AVAILABLE(macos(11.3))
static void controller_connected(GCController *controller) {
    GCExtendedGamepad *gamepad = controller.extendedGamepad;
    const char *kind = NULL;
    if ([gamepad isKindOfClass:[GCDualSenseGamepad class]])
        kind = "DualSense";
    else if ([gamepad isKindOfClass:[GCDualShockGamepad class]])
        kind = "DualShock 4";
    if (kind == NULL)
        return; // Xbox and other controllers stay with SDL

    os_unfair_lock_lock(&pads_lock);
    bridged_pad *pad = NULL;
    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        if (pads[i].controller == controller) {
            os_unfair_lock_unlock(&pads_lock);
            return;
        }
        if (pad == NULL && pads[i].controller == nil)
            pad = &pads[i];
    }
    if (pad == NULL) {
        os_unfair_lock_unlock(&pads_lock);
        printf("GameController bridge: %s connected, but 4 controllers are already bridged\n", kind);
        return;
    }
    memset(&pad->state, 0, sizeof(pad->state));
    pad->wanted_motors[0] = pad->wanted_motors[1] = 0;
    pad->kind = kind;
    pad->user_index = first_free_user_index();
    pad->controller = [controller retain];
    int user_index = pad->user_index;
    os_unfair_lock_unlock(&pads_lock);

    // a DualSense over Bluetooth reports its level with an unknown charging state
    char battery[48] = "";
    GCDeviceBattery *device_battery = controller.battery;
    if (device_battery != nil && device_battery.batteryLevel > 0.0f)
        snprintf(battery, sizeof(battery), ", battery %i%%%s", (int)(device_battery.batteryLevel * 100.0f + 0.5f),
                 device_battery.batteryState == GCDeviceBatteryStateCharging ? " and charging" :
                 device_battery.batteryState == GCDeviceBatteryStateFull ? " (full)" : "");
    if (user_index >= 0)
        printf("GameController bridge: %s connected as player %i (%s)%s\n", kind, user_index + 1, sony_transport(), battery);
    else
        printf("GameController bridge: %s connected (%s)%s, waiting for a free player slot\n", kind, sony_transport(), battery);

    controller.playerIndex = user_index >= 0 ? (GCControllerPlayerIndex)user_index : GCControllerPlayerIndexUnset;
    if (lightbar_color < 0) {
        printf("GameController bridge: lightbar left as it is (no LightbarColor in settings.ini)\n");
    } else if (controller.light != nil) {
        controller.light.color = [[[GCColor alloc] initWithRed:((lightbar_color >> 16) & 0xFF) / 255.0f
                                                         green:((lightbar_color >> 8) & 0xFF) / 255.0f
                                                          blue:(lightbar_color & 0xFF) / 255.0f] autorelease];
        printf("GameController bridge: lightbar set to %06X\n", lightbar_color);
    } else {
        printf("GameController bridge: this %s has no lightbar macOS can set\n", kind);
    }

    controller.handlerQueue = input_queue;
    gamepad.valueChangedHandler = ^(GCExtendedGamepad *changed, GCControllerElement *element) {
        update_state(pad, changed);
    };
    dispatch_async(input_queue, ^{
        @autoreleasepool {
            update_state(pad, gamepad);
            start_haptics(pad, controller, user_index);
        }
    });
}

API_AVAILABLE(macos(11.3))
static void controller_disconnected(GCController *controller) {
    for (int i = 0; i < XUSER_MAX_COUNT; i++) {
        bridged_pad *pad = &pads[i];
        os_unfair_lock_lock(&pads_lock);
        bool match = pad->controller == controller;
        int user_index = pad->user_index;
        if (match)
            pad->controller = nil; // the game stops seeing it right away
        os_unfair_lock_unlock(&pads_lock);
        if (!match)
            continue;
        controller.extendedGamepad.valueChangedHandler = nil;
        if (user_index >= 0)
            printf("GameController bridge: %s on player %i disconnected\n", pad->kind, user_index + 1);
        else
            printf("GameController bridge: %s disconnected\n", pad->kind);
        // rumble is torn down on the input queue, after anything still queued for this controller
        dispatch_async(input_queue, ^{
            @autoreleasepool {
                stop_haptics(pad);
                [controller release];
            }
        });
    }
}

// in the background macOS stops sending controller input, so a held button would stay held: send the game a neutral
// state and stop rumble until it's focused again, then pick up the controllers' current state
API_AVAILABLE(macos(11.3))
static void app_focus_changed(bool active) {
    if (atomic_exchange(&app_active, active) == active)
        return;
    dispatch_async(input_queue, ^{
        @autoreleasepool {
            for (int i = 0; i < XUSER_MAX_COUNT; i++) {
                bridged_pad *pad = &pads[i];
                os_unfair_lock_lock(&pads_lock);
                GCController *controller = [pad->controller retain];
                os_unfair_lock_unlock(&pads_lock);
                if (controller == nil)
                    continue;
                if (active)
                    update_state(pad, controller.extendedGamepad);
                apply_motors(pad);
                [controller release];
            }
        }
    });
}

API_AVAILABLE(macos(11.3))
static void gc_setup(void) {
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    NSOperationQueue *main_queue = [NSOperationQueue mainQueue];
    [center addObserverForName:GCControllerDidConnectNotification object:nil queue:main_queue usingBlock:^(NSNotification *note) {
        controller_connected(note.object);
    }];
    [center addObserverForName:GCControllerDidDisconnectNotification object:nil queue:main_queue usingBlock:^(NSNotification *note) {
        controller_disconnected(note.object);
    }];
    // AppKit's NSApplicationDidResignActiveNotification and NSApplicationDidBecomeActiveNotification
    [center addObserverForName:@"NSApplicationDidResignActiveNotification" object:nil queue:main_queue usingBlock:^(NSNotification *note) {
        app_focus_changed(false);
    }];
    [center addObserverForName:@"NSApplicationDidBecomeActiveNotification" object:nil queue:main_queue usingBlock:^(NSNotification *note) {
        app_focus_changed(true);
    }];
    for (GCController *controller in GCController.controllers)
        controller_connected(controller);
}

#pragma mark - setup

bool gc_bridge_enabled(void) {
    return enabled;
}

bool gc_bridge_is_playstation_user(int user_index) {
    if (!enabled || user_index < 0 || user_index >= XUSER_MAX_COUNT)
        return false;
    os_unfair_lock_lock(&pads_lock);
    bool playstation = pad_for_user(user_index) != NULL; // only PlayStation controllers are bridged
    os_unfair_lock_unlock(&pads_lock);
    return playstation;
}

static int parse_rgb(const char *text) {
    text += strspn(text, " \t\"#");
    return (int)strtol(text, NULL, 16);
}

void gc_bridge_init(uint64_t base, int mode, int lightbar_rgb) {
    const char *env = getenv("BO3MACFIX_CONTROLLERBRIDGE");
    if (env != NULL)
        mode = atoi(env);
    env = getenv("BO3MACFIX_LIGHTBARCOLOR");
    if (env != NULL)
        lightbar_rgb = parse_rgb(env);
    if (mode == 0)
        return;

    if (@available(macOS 11.3, *)) {
        // push rbp; mov rbp, rsp; push r14; push rbx; sub rsp, 0x80 - both start the same way
        static const uint8_t expected[] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 };
        void *get_state = (void *)(base + ADDR_ASL_XInputGetState);
        void *set_state = (void *)(base + ADDR_ASL_XInputSetState);
        if (memcmp(prepatch_original_code(get_state, sizeof(expected)), expected, sizeof(expected)) != 0 ||
            memcmp(prepatch_original_code(set_state, sizeof(expected)), expected, sizeof(expected)) != 0) {
            printf("unexpected code at %08x or %08x, GameController bridge off\n", ADDR_ASL_XInputGetState, ADDR_ASL_XInputSetState);
            return;
        }
        base_address = base;
        lightbar_color = lightbar_rgb > 0xFFFFFF ? -1 : lightbar_rgb;
        input_queue = dispatch_queue_create("BO3MacFix controller input", DISPATCH_QUEUE_SERIAL);
        install_hook_ASL_XInputGetState(get_state);
        install_hook_ASL_XInputSetState(set_state);
        enabled = true;
        // the GameController framework needs the app's run loop, which runs once the game is up
        dispatch_async(dispatch_get_main_queue(), ^{
            @autoreleasepool {
                gc_setup();
            }
        });
        printf("GameController bridge: on, DualSense and DualShock 4 controllers go through macOS instead of SDL\n");
    } else {
        printf("GameController bridge: needs macOS 11.3 or newer, staying off\n");
    }
}
