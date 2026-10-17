#include <stdbool.h>

int run_command(char *const argv[]);
int run_command_quietly(char *const argv[]);
bool download_verified(const char *url, const char *expected_sha256, const char *destination);
