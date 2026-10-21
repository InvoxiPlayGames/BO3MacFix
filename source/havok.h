#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal interface to the game's HavokScript (Lua) VM.

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *s);

#define HKS_TNIL 0
#define HKS_TBOOLEAN 1
#define HKS_TNUMBER 3
#define HKS_TSTRING 4

typedef struct _HksObject {
    uint32_t t;
    union {
        float number;
        int32_t boolean;
        void *ptr;
    } v;
} HksObject;

// lua_State fields
#define HKS_GLOBAL(s) (*(uint8_t **)((uint8_t *)(s) + 0x10))
#define HKS_TOP(s) (*(HksObject **)((uint8_t *)(s) + 0x48))
#define HKS_BASE(s) (*(HksObject **)((uint8_t *)(s) + 0x50))

// HksGlobal fields
#define HKS_BYTECODE_SHARING_MODE(g) (*(int32_t *)((g) + 0x1d8))
#define HKS_COMPILER_SETTINGS(g) ((void *)((g) + 0x568))
#define HKS_BYTECODE_SHARING_ON 1

void havok_init(uint64_t base_address);

// Reads argument index as a string, raising a Lua error if it isn't one.
const char *hks_check_string(lua_State *s, int index);
// Reads argument index as a number, returns false if it isn't one.
bool hks_number_arg(lua_State *s, int index, float *out);

void hks_push_nil(lua_State *s);
void hks_push_boolean(lua_State *s, bool value);
void hks_push_number(lua_State *s, float value);
void hks_push_string(lua_State *s, const char *value);
void hks_push_cfunction(lua_State *s, lua_CFunction fn, const char *name);

// Compiles Lua source code and pushes the resulting function, returns false (and prints why) on failure.
bool hks_load_source(lua_State *s, const char *source, size_t source_len, const char *chunk_name);
// Calls the function below nargs arguments, returns false (and prints why) on failure.
bool hks_pcall(lua_State *s, int nargs, int nresults);
