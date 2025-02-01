#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <Foundation/Foundation.h>

int launch_game_native(const char *path) {
    NSTask *gameProc = [[NSTask alloc] init];
    // copy our current working directory and pass it onto the child process
    NSProcessInfo *currentProcess = [NSProcessInfo processInfo];
    NSString *pwd = [NSString stringWithUTF8String:path];
    NSLog(@"%@\n", pwd);
    NSURL *fixDirectoryUrl = [NSURL fileURLWithPath:pwd];
    NSLog(@"%@\n", fixDirectoryUrl);
    NSURL *gameDirectoryUrl = [fixDirectoryUrl URLByAppendingPathComponent:@"../CoDBlkOps3.app/Contents/MacOS/"];
    NSLog(@"%@\n", gameDirectoryUrl);
    gameProc.currentDirectoryURL = gameDirectoryUrl;
    // set the game executable path
    gameProc.executableURL = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"CoDBlkOps3_Exe"];
    
    // add the fix dylib to DYLD_INSERT_LIBRARIES of the new process
    NSURL *dylibUrl = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"../../../BO3MacFix/BO3MacFix.dylib"];
    NSMutableDictionary *newEnvironment = [[currentProcess environment] mutableCopy];
    // if we already have dyld_insert_libraries set (by Steam), append our dylib to that
    if ([[currentProcess environment] objectForKey:@"DYLD_INSERT_LIBRARIES"]) {
        newEnvironment[@"DYLD_INSERT_LIBRARIES"] = [NSString stringWithFormat:@"%@:%@",[dylibUrl path],[currentProcess environment][@"DYLD_INSERT_LIBRARIES"]];
    } else { // otherwise just set it by itself
        newEnvironment[@"DYLD_INSERT_LIBRARIES"] = [dylibUrl path];
    }
    newEnvironment[@"WINEPREFIX"] = [[gameProc.currentDirectoryURL URLByAppendingPathComponent:@"../../../BO3MacFix/pfx"] path];
    newEnvironment[@"SteamAppId"] = @"311210";
    gameProc.environment = newEnvironment;

    NSLog(@"Launching game...\n");
    NSLog(@"  Executable: %@\n", gameProc.executableURL);
    NSLog(@"  Working Directory: %@\n", gameProc.currentDirectoryURL);
    NSLog(@"  DYLD_INSERT_LIBRARIES: %@\n", newEnvironment[@"DYLD_INSERT_LIBRARIES"]);
    NSLog(@"  WINEPREFIX: %@\n", newEnvironment[@"WINEPREFIX"]);

    // launch the game executable
    [gameProc launch];
    [gameProc waitUntilExit];
    return 0;
}
