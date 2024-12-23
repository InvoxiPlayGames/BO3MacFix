#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <string.h>

static char new_title_buf[0x40];
void set_window_title(const char *new_title) {
    strncpy(new_title_buf, new_title, sizeof(new_title_buf));
    dispatch_async(dispatch_get_main_queue(), ^(void) {
        [[[NSApplication sharedApplication] mainWindow] setTitle:[NSString stringWithUTF8String:new_title_buf]];
        if (@available(macOS 10.14, *)) {
            [NSApplication sharedApplication].appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        }
    });
}
