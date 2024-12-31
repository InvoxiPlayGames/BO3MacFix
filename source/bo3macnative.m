#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <string.h>
#include <unistd.h>

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

void bounce_dock_icon() {
    dispatch_async(dispatch_get_main_queue(), ^(void) {
        [[NSApplication sharedApplication] requestUserAttention:NSCriticalRequest];
    });
}

bool alert_displaying = false;

void display_nsalert(const char *title, const char *message) {
    // don't ping another message if an alert is displaying
    while (alert_displaying) {
        usleep(1000);
    }
    // really shit way of halting the thread until the alert returns
    alert_displaying = true;
    dispatch_async(dispatch_get_main_queue(), ^(void) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:title]];
        [alert setInformativeText:[NSString stringWithUTF8String:message]];
        [alert runModal];
        alert_displaying = false;
    });
    // halt our calling thread while the alert hasn't returned
    while (alert_displaying) {
        usleep(1000);
    }
}
