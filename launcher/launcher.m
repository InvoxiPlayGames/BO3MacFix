#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <Foundation/Foundation.h>

int main(int argc, char **argv) {
    NSTask *gameProc = [[NSTask alloc] init];
    // copy our current working directory and pass it onto the child process
    NSProcessInfo *currentProcess = [NSProcessInfo processInfo];
    NSString *pwd = [currentProcess arguments][0];
    gameProc.currentDirectoryURL = [[NSURL fileURLWithPath:pwd] URLByDeletingLastPathComponent];
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
    // parse our arguments and figure out which ones we send to the new process vs which ones we 
    NSMutableArray *newArguments = [[NSMutableArray alloc] init];
    for (int i = 0; i < argc; i++) {
        if (strcasecmp(argv[i], "--password") == 0 && i + 1 < argc)
            newEnvironment[@"BO3MACFIX_NETWORKPASSWORD"] = [NSString stringWithUTF8String:argv[++i]];
        else if (strcasecmp(argv[i], "--playername") == 0 && i + 1 < argc)
            newEnvironment[@"BO3MACFIX_PLAYERNAME"] = [NSString stringWithUTF8String:argv[++i]];
        else if (strcasecmp(argv[i], "--winepath") == 0 && i + 1 < argc)
            newEnvironment[@"BO3MACFIX_WINEPATH"] = [NSString stringWithUTF8String:argv[++i]];
        else
            [newArguments addObject:[NSString stringWithUTF8String:argv[i]]];
    }
    gameProc.environment = newEnvironment;
    gameProc.arguments = newArguments;
    
    //NSLog(@"Launching game...\n");
    //NSLog(@"  Executable: %@\n", gameProc.executableURL);
    //NSLog(@"  Working Directory: %@\n", gameProc.currentDirectoryURL);
    //NSLog(@"  DYLD_INSERT_LIBRARIES: %@\n", newEnvironment[@"DYLD_INSERT_LIBRARIES"]);
    //NSLog(@"  WINEPREFIX: %@\n", newEnvironment[@"WINEPREFIX"]);
    // launch the game executable
    [gameProc launch];
    [gameProc waitUntilExit];
    return 0;
}
