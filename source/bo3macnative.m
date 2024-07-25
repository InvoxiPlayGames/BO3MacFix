#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>

void set_window_title(const char *new_title) {
    // doesn't work
    [[[NSApplication sharedApplication] mainWindow] setTitle:[NSString stringWithUTF8String:new_title]];
}
