#pragma once

#define ENABLE_PRINTS 1
#define TEST_MAPBUILD_THEORY 0

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>
#include <iostream>
#include <stdio.h>
#include <Winternl.h>

// #define REBASE(x) (*(unsigned __int64*)((unsigned __int64)(NtCurrentTeb()->ProcessEnvironmentBlock) + 0x10) + (unsigned __int64)(x))
#define EXPORT extern "C" __declspec(dllexport)

#include "msdetours.h"
#define MDT_Activate(fn_name) DetourAttach(&(PVOID&)__ ## fn_name, fn_name)
#define MDT_Deactivate(fn_name) DetourDetach(&(PVOID&)__ ## fn_name, fn_name)
#define MDT_ORIGINAL(fn_name, fn_params) __ ## fn_name fn_params
#define MDT_ORIGINAL_PTR(fn_name) __ ## fn_name
#define MDT_Define_FASTCALL(off, fn_name, fn_return, fn_params) typedef fn_return (__fastcall* __ ## fn_name ## _t)fn_params; \
__ ## fn_name ## _t __ ## fn_name = (__ ## fn_name ## _t) off;\
fn_return __fastcall fn_name ## fn_params

#define MDT_Define_FASTCALL_STATIC(off, fn_name, fn_return, fn_params) typedef fn_return (__fastcall* __ ## fn_name ## _t)fn_params; \
static __ ## fn_name ## _t __ ## fn_name = (__ ## fn_name ## _t) off; fn_return __fastcall fn_name ## fn_params

#define MDT_Implement_STATIC(ns, fn_name, fn_return, fn_params) fn_return __fastcall ns::fn_name ## fn_params

#if ENABLE_PRINTS
#define DEBUG_PRINT(...) printf(__VA_ARGS__); printf("\n")
#else
#define DEBUG_PRINT(...)
#endif

template <typename T> void chgmem(__int64 addy, T copy)
{
    DWORD oldprotect;
    VirtualProtect((void*)addy, sizeof(T), PAGE_EXECUTE_READWRITE, &oldprotect);
    *(T*)addy = copy;
    VirtualProtect((void*)addy, sizeof(T), oldprotect, &oldprotect);
}

void chgmem(__int64 addy, __int32 size, void* copy);

struct hkClassMember;
struct hkClass;