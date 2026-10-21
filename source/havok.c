#include <stdio.h>
#include <string.h>
#include "havok.h"
#include "offsets.h"

static const char *(*hksi_luaL_checklstring)(lua_State *s, int index, size_t *len);
static void (*hksi_lua_pushlstring)(lua_State *s, const char *str, size_t len);
static void (*hks_pushnamedcclosure)(lua_State *s, lua_CFunction fn, int nupvalues, const char *name, int treat_closure_as_func, int unknown);
static int (*hksi_hksL_loadbuffer)(lua_State *s, void *compiler_settings, const char *buff, size_t sz, const char *name);
static int (*hksi_lua_pcall)(lua_State *s, int nargs, int nresults, int errfunc);

void havok_init(uint64_t base_address) {
    hksi_luaL_checklstring = (void *)(base_address + ADDR_hksi_luaL_checklstring);
    hksi_lua_pushlstring = (void *)(base_address + ADDR_hksi_lua_pushlstring);
    hks_pushnamedcclosure = (void *)(base_address + ADDR_hks_pushnamedcclosure);
    hksi_hksL_loadbuffer = (void *)(base_address + ADDR_hksi_hksL_loadbuffer);
    hksi_lua_pcall = (void *)(base_address + ADDR_hksi_lua_pcall);
}

const char *hks_check_string(lua_State *s, int index) {
    return hksi_luaL_checklstring(s, index, NULL);
}

bool hks_number_arg(lua_State *s, int index, float *out) {
    HksObject *arg = HKS_BASE(s) + (index - 1);
    if (arg >= HKS_TOP(s) || (arg->t & 0xF) != HKS_TNUMBER)
        return false;
    *out = arg->v.number;
    return true;
}

void hks_push_nil(lua_State *s) {
    HKS_TOP(s)->t = HKS_TNIL;
    HKS_TOP(s)++;
}

void hks_push_boolean(lua_State *s, bool value) {
    HKS_TOP(s)->t = HKS_TBOOLEAN;
    HKS_TOP(s)->v.boolean = value;
    HKS_TOP(s)++;
}

void hks_push_number(lua_State *s, float value) {
    HKS_TOP(s)->t = HKS_TNUMBER;
    HKS_TOP(s)->v.number = value;
    HKS_TOP(s)++;
}

void hks_push_string(lua_State *s, const char *value) {
    hksi_lua_pushlstring(s, value, strlen(value));
}

void hks_push_cfunction(lua_State *s, lua_CFunction fn, const char *name) {
    hks_pushnamedcclosure(s, fn, 0, name, 0, 0);
}

// prints and pops the error message left on the stack by a failed load or call
static void hks_report_error(lua_State *s, const char *what) {
    printf("Lua %s error: %s\n", what, hks_check_string(s, -1));
    HKS_TOP(s)--;
}

bool hks_load_source(lua_State *s, const char *source, size_t source_len, const char *chunk_name) {
    // the game only loads precompiled Lua by default, so allow source code just while we compile ours
    uint8_t *global = HKS_GLOBAL(s);
    int32_t sharing_mode = HKS_BYTECODE_SHARING_MODE(global);
    HKS_BYTECODE_SHARING_MODE(global) = HKS_BYTECODE_SHARING_ON;
    int r = hksi_hksL_loadbuffer(s, HKS_COMPILER_SETTINGS(global), source, source_len, chunk_name);
    HKS_BYTECODE_SHARING_MODE(global) = sharing_mode;
    if (r != 0) {
        hks_report_error(s, "compile");
        return false;
    }
    return true;
}

bool hks_pcall(lua_State *s, int nargs, int nresults) {
    if (hksi_lua_pcall(s, nargs, nresults, 0) != 0) {
        hks_report_error(s, "runtime");
        return false;
    }
    return true;
}
