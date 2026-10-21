#include <dlfcn.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <Foundation/Foundation.h>

// BO3MacFix_PrepatchedIsCurrent and BO3MacFix_BuildPrepatched, see source/prepatch.h
typedef int (*prepatch_fn)(const char *original, const char *copy, char *message, size_t message_size);

// reads "Name = value" from BO3MacFix's settings.ini, like read_int_setting in bo3macfix.c
static int read_setting(NSString *path, const char *name, int fallback) {
    FILE *fp = fopen(path.fileSystemRepresentation, "r");
    if (fp == NULL)
        return fallback;
    int value = fallback;
    size_t name_len = strlen(name);
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line + strspn(line, " \t");
        if (strncmp(p, name, name_len) != 0)
            continue;
        p += name_len;
        p += strspn(p, " \t");
        if (*p != '=')
            continue;
        p++;
        p += strspn(p, " \t\"");
        value = atoi(p);
    }
    fclose(fp);
    return value;
}

// runs /usr/bin/codesign with these arguments, returns whether it succeeded
static BOOL codesign(NSArray<NSString *> *arguments) {
    NSTask *task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/codesign"];
    task.arguments = arguments;
    task.standardOutput = [NSFileHandle fileHandleWithNullDevice];
    task.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![task launchAndReturnError:nil])
        return NO;
    [task waitUntilExit];
    return task.terminationStatus == 0;
}

// The pre-patched executable is a copy of the game's with BO3MacFix's code changes made in the file, so they don't
// have to be made in memory, which makes Rosetta run the whole game through its slower JIT. Returns the copy,
// making it first when it's missing or out of date (after a game or BO3MacFix update), or nil to start the original.
static NSURL *prepatched_executable(NSURL *gameDir, NSURL *dylibUrl, FILE *log) {
    NSString *original = [gameDir URLByAppendingPathComponent:@"CoDBlkOps3_Exe"].path;
    NSString *copy = [gameDir URLByAppendingPathComponent:@"CoDBlkOps3_Exe.bo3macfix"].path;
    NSString *temp = [copy stringByAppendingString:@".tmp"];
    char message[512];

    // BO3MacFix makes the copy, it knows what to change
    void *macfix = dlopen(dylibUrl.path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    prepatch_fn is_current = macfix != NULL ? (prepatch_fn)dlsym(macfix, "BO3MacFix_PrepatchedIsCurrent") : NULL;
    prepatch_fn build = macfix != NULL ? (prepatch_fn)dlsym(macfix, "BO3MacFix_BuildPrepatched") : NULL;
    if (is_current == NULL || build == NULL) {
        fprintf(log, "couldn't load BO3MacFix to make the pre-patched executable: %s\n", macfix == NULL ? dlerror() : "it's too old");
        return nil;
    }
    if (is_current(original.fileSystemRepresentation, copy.fileSystemRepresentation, message, sizeof(message)) == 0) {
        fprintf(log, "%s\n", message);
        return [NSURL fileURLWithPath:copy];
    }
    fprintf(log, "%s, making it\n", message);
    unlink(temp.fileSystemRepresentation);
    if (build(original.fileSystemRepresentation, temp.fileSystemRepresentation, message, sizeof(message)) != 0) {
        fprintf(log, "couldn't make the pre-patched executable: %s\n", message);
        unlink(temp.fileSystemRepresentation);
        return nil;
    }
    fprintf(log, "%s\n", message);
    // changing the executable broke Aspyr's signature, so sign the copy ad hoc under the same identifier
    if (!codesign(@[ @"--force", @"--sign", @"-", @"--identifier", @"CoDBlkOps3_Exe", temp ]) ||
        !codesign(@[ @"--verify", temp ])) {
        fprintf(log, "couldn't sign the pre-patched executable\n");
        unlink(temp.fileSystemRepresentation);
        return nil;
    }
    if (rename(temp.fileSystemRepresentation, copy.fileSystemRepresentation) != 0) {
        fprintf(log, "couldn't put the pre-patched executable in place\n");
        unlink(temp.fileSystemRepresentation);
        return nil;
    }
    return [NSURL fileURLWithPath:copy];
}

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
        else if (strcasecmp(argv[i], "--friendsonly") == 0)
            newEnvironment[@"BO3MACFIX_FRIENDSONLY"] = @"true";
        else
            [newArguments addObject:[NSString stringWithUTF8String:argv[i]]];
    }
    gameProc.environment = newEnvironment;
    gameProc.arguments = newArguments;

    // start the pre-patched executable unless PrePatchedExe = 0 in settings.ini, or NoCodePatches is on (which
    // wants the game's code as shipped). What happened goes in BO3MacFix/launcher.log.
    NSURL *macfixUrl = [gameProc.currentDirectoryURL URLByAppendingPathComponent:@"../../../BO3MacFix"];
    NSString *settingsPath = [macfixUrl URLByAppendingPathComponent:@"settings.ini"].path;
    FILE *log = fopen([macfixUrl URLByAppendingPathComponent:@"launcher.log"].path.fileSystemRepresentation, "w");
    if (log == NULL)
        log = fopen("/dev/null", "w");
    const char *prepatched_env = getenv("BO3MACFIX_PREPATCHEDEXE");
    int prepatched = prepatched_env != NULL ? atoi(prepatched_env) : read_setting(settingsPath, "PrePatchedExe", 1);
    if (prepatched == 0) {
        fprintf(log, "PrePatchedExe is off\n");
    } else if (read_setting(settingsPath, "NoCodePatches", 0) != 0 || getenv("BO3MACFIX_NOCODEPATCHES") != NULL) {
        fprintf(log, "NoCodePatches is on, so the game's code stays as shipped\n");
    } else {
        NSURL *copyUrl = prepatched_executable(gameProc.currentDirectoryURL, dylibUrl, log);
        if (copyUrl != nil)
            gameProc.executableURL = copyUrl;
    }
    fprintf(log, "starting %s\n", gameProc.executableURL.lastPathComponent.UTF8String);
    fclose(log);

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
