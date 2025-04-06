#include <Foundation/Foundation.h>
#include <AppKit/AppKit.h>
#include <dispatch/dispatch.h>
#include <string.h>
#include <unistd.h>
#include "versioninfo.h"

void display_nsalert(const char *title, const char *message); // fwd declare
void set_player_name(const char *name); // declared in bo3macfix.c
const char *get_player_name(); // declared in bo3macfix.c

@interface MacFixNativeDelegate : NSObject
@end
@implementation MacFixNativeDelegate
+(void)setUpMenuBar {
    NSMenuItem *macfixMenuItem = [NSMenuItem new];
    NSMenu *macfixMenu = [[NSMenu alloc] initWithTitle:@"BO3MacFix"];
    // the two dialogs. TODO(Emma): implement network password
    [[macfixMenu addItemWithTitle:@"Change Player Name" action:@selector(doPlayerNameChange) keyEquivalent:@"n"] setTarget:self];
    [[macfixMenu addItemWithTitle:@"Set Network Password" action:@selector(doPlayerNameChange) keyEquivalent:@"p"] setTarget:self];
    // friends only. TODO(Emma): implement this properly
    NSMenuItem *fo = [macfixMenu addItemWithTitle:@"Enable Friends Only" action:@selector(doPlayerNameChange) keyEquivalent:@"f"];
    [fo setState:NSControlStateValueOn];
    [fo setTarget:self];
    // spacer then version info
    [macfixMenu addItem:[NSMenuItem separatorItem]];
    [macfixMenu addItemWithTitle:@"BO3MacFix " @MACFIX_VERSION action:NULL keyEquivalent:@""];
    // add the menu to the main application's menu
    [macfixMenuItem setSubmenu:macfixMenu];
    [[[NSApplication sharedApplication] mainMenu] addItem:macfixMenuItem];
}
+(void)doPlayerNameChange {
    // create an NSAlert
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:[NSString stringWithUTF8String:"Change Player Name"]];
    [alert setInformativeText:[NSString stringWithUTF8String:"(leave blank to use your Steam name)"]];
    // make a text box with our existing custom player name as its contents
    NSTextField *txt = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 24)];
    [txt setStringValue:[NSString stringWithUTF8String:get_player_name()]];
    [alert setAccessoryView:txt];
    // launch it as a sheet modal so it doesn't freeze the whole game
    [alert beginSheetModalForWindow:[[NSApplication sharedApplication] mainWindow] completionHandler:^(NSModalResponse returnCode) {
        if (returnCode == NSModalResponseCancel || returnCode == NSModalResponseOK) { // pressing Enter can mean Cancel
            set_player_name([[txt stringValue] UTF8String]);
        }
    }];
}
@end

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

void add_menu_bar() {
    dispatch_async(dispatch_get_main_queue(), ^(void) {
        [MacFixNativeDelegate setUpMenuBar];
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
