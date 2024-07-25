#include <stdint.h>

uint64_t get_base_address();
const char *filename_from_path(const char *path);
bool rosetta_translated_process();
uint64_t gscu_canon_hash64(const char *input);
