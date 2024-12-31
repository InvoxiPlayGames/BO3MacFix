#include <stdint.h>
#include <stdio.h>

uint64_t get_base_address();
const char *filename_from_path(const char *path);
bool rosetta_translated_process();
uint64_t gscu_canon_hash64(const char *input);
const char *get_wine_path();
void replace_vtable_entry(void *entry, void *replacement);
bool is_macos_dylib(const char *path);
bool get_file_sha1(const char *path, uint8_t *output_hash);
bool bytes_to_hex(const uint8_t *bytes, size_t bytes_len, char *out_string, size_t out_string_len);
