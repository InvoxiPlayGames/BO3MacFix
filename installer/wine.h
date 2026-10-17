#include <stdbool.h>
#include <stddef.h>

bool wine_bundled_path(const char *fix_directory, char *out_path, size_t out_path_size);
bool wine_system_install_present(void);
bool wine_install_bundled(const char *fix_directory);
