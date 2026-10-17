#include <stdbool.h>
#include <stddef.h>

void steam_launch_options_value(const char *wrapper_path, char *out_value, size_t out_value_size);
bool steam_is_running(void);
bool steam_quit(void);
void steam_open(void);
int steam_set_launch_options(const char *launch_options, int *failures);
int steam_count_configs_with_launch_options(const char *launch_options);
void copy_to_clipboard(const char *text);
