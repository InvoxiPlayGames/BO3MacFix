#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <CommonCrypto/CommonDigest.h>

#include "download.h"

extern char **environ;

static int spawn_and_wait(char *const argv[], const posix_spawn_file_actions_t *file_actions) {
    pid_t pid;
    if (posix_spawn(&pid, argv[0], file_actions, NULL, argv, environ) != 0)
        return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int run_command(char *const argv[]) {
    return spawn_and_wait(argv, NULL);
}

int run_command_quietly(char *const argv[]) {
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_addopen(&file_actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&file_actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    int result = spawn_and_wait(argv, &file_actions);
    posix_spawn_file_actions_destroy(&file_actions);
    return result;
}

static bool sha256_file_matches(const char *path, const char *expected_hex) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    static uint8_t buffer[1 << 16];
    size_t read_size;
    while ((read_size = fread(buffer, 1, sizeof(buffer), file)) > 0)
        CC_SHA256_Update(&context, buffer, (CC_LONG)read_size);
    fclose(file);
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    char digest_hex[CC_SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
        snprintf(digest_hex + i * 2, 3, "%02x", digest[i]);
    return strcmp(digest_hex, expected_hex) == 0;
}

bool download_verified(const char *url, const char *expected_sha256, const char *destination) {
    char *const curl_argv[] = { "/usr/bin/curl", "-L", "--fail", "--progress-bar", "-o", (char *)destination, (char *)url, NULL };
    if (run_command(curl_argv) != 0) {
        printf("The download failed. Check the internet connection and try again.\n");
        unlink(destination);
        return false;
    }
    printf("Verifying the download...");
    if (!sha256_file_matches(destination, expected_sha256)) {
        printf("failed. The file does not match the expected checksum.\n");
        unlink(destination);
        return false;
    }
    printf("done!\n");
    return true;
}
