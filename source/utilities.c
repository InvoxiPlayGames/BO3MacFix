#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>

// Gets the base address of the first loaded dyld image (the game binary)
uint64_t get_base_address() {
    // getsegbyname is deprecated since 13.0, is there an alternative?
    const struct segment_command_64* segment = getsegbyname("__TEXT");
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    return segment->vmaddr + slide;
}

// Gets just the filename of a given path
const char *filename_from_path(const char *path) {
    size_t len = strlen(path);
    for (size_t i = len - 1; i > 0; i--) {
        if (path[i] == '/')
            return path + i + 1;
    }
    return path;
}

// Detects if the process is running under Rosetta2 or not (find out if we're on ASi or Intel)
bool rosetta_translated_process() {
    int ret = 0;
    size_t size = sizeof(ret);
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1)
        return false;
    return ret;
}

// Hash function used to generate network passwords on t7patch
uint64_t gscu_canon_hash64(const char *input) {
    static const uint64_t offset = 14695981039346656037ULL;
	static const uint64_t prime = 1099511628211ULL;
	uint64_t hash = offset;
	const char* _input = input;
	while (*_input) {
		hash = (hash ^ tolower(*_input)) * prime;
		_input++;
	}
	return 0x7FFFFFFFFFFFFFFF & hash;
}
