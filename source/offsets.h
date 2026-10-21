// Modified by Mickael DE LA ROQUE on 2026-09-08 with offsets used by the
// unofficial custom-map stream-stall workaround.
// -- START OFFSETS [Mac v1.0.0.0, in-game v98, executable 5e42371b6a16f2b11e637f48e38554625570aa9f]
// functions
#define ADDR_HKAI_InitMapData 0xe09aa9 // func
#define ADDR_load_workshop_texture 0xe4a26c // func - name is a guess
#define ADDR_build_usermods_path 0xe49938 // func - name is a guess
#define ADDR_DB_FindXAssetHeader 0x1345b12 // func
#define ADDR_Com_Error 0xe71e51 // func
#define ADDR_Live_SystemInfo 0x129d650 // func
#define ADDR_Dvar_SetFromStringByName 0xfa3386 // func
#define ADDR_someLoggingFunction 0xe70f88 // func - name is a guess
#define ADDR_someWindowCreationFunction 0x1143ca // func - name is a guess
#define ADDR_Sys_Checksum 0x14025dd // func
#define ADDR_Sys_VerifyPacketChecksum 0x140258f // func
#define ADDR_Sys_CheckSumPacketCopy 0x1402648 // func
#define ADDR_Lua_CmdParseArgs 0x1408cf9 // func
#define ADDR_LobbyMsgRW_PrepWriteMsg 0xf415fa // func
#define ADDR_LobbyMsgRW_PrepReadMsg 0xf416fb // func
#define ADDR_MSG_ReadByte 0x1247989 // func
#define ADDR_MSG_WriteByte 0x12473d3 // func
#define ADDR_ASL_ToNativePath 0xe9f6d // func
#define ADDR_CSteamAPIContext_Init 0xd311e0 // func
#define ADDR_dwInstantMessageDispatch 0xe36915 // func
#define ADDR_hks_load_dll 0x148460d // func
#define ADDR_hksf_fopen_internal 0xfb503 // func - name is a guess, called by hksf_fopen
#define ADDR_CL_FirstSnapshot 0xe2f0be // func
#define ADDR_CL_SetCGameTime 0xe2fbe8 // func
#define ADDR_DB_GetStreamFraction 0x127c182 // func - name is a guess
#define ADDR_Com_GetBuildIntField 0xf16d9a // func
#define ADDR_Lua_GetBuildStringField 0xe66e2f // func - Engine.GetBuildStringField
#define ADDR_Lua_GetBuildIntField 0xe66e92 // func - Engine.GetBuildIntField
#define ADDR_hksi_lua_pushlstring 0x1409c50 // func
#define ADDR_hksi_luaL_checklstring 0x1473440 // func
#define ADDR_hksi_hksL_loadbuffer 0x1474566 // func
#define ADDR_hksi_lua_pcall 0x14465f9 // func
#define ADDR_hks_pushnamedcclosure 0x1476385 // func
#define ADDR_R_TrackTextureMemory 0x126dad2 // func - name is a guess
#define ADDR_ASL_SleepEx 0x111752 // func - Aspyr's Sleep/SleepEx(milliseconds, alertable)
#define ADDR_SDL_SetWindowTitle 0x1944d0 // func
#define ADDR_LiveSteam_RunEveryFrame 0x1434f3e // func
#define ADDR_SomeControllerTypeFunc 0x122f78d // func
#define ADDR_SDL_PrivateGameControllerAddMapping 0x13cff0 // func
#define ADDR_SDL_GameControllerInitMappings 0x13d450 // func
#define ADDR_Cbuf_AddText 0xf754aa // func

//globals
#define ADDR_g_workshopMapId 0x3087dac // char[] - name is a guess
#define ADDR_g_emuPath 0x4ae9c00 // char[] - name is a guess
#define ADDR_sSessionModeState 0x6939a14 // int
#define ADDR_DB_XAssetPool 0x1017cc40 // XAssetPool[]
#define ADDR_g_assetNames 0x20d84b0 // const char *[]
#define ADDR_g_d3d11Device 0x113c0b18 // ID3D11Device * - name is a guess
#define ADDR_DB_LOAD_TOTALS 0xb2b81c0 // int[4]
#define ADDR_DB_LOAD_STAGE_COUNT 0xb2b81d0 // int
#define ADDR_DB_LOAD_COMPLETED 0xb2b81e0 // int[4]
#define ADDR_CLIENT_CONNECTIONS 0x115324a0 // client connection array
#define ADDR_LOCAL_CLIENT_COUNT 0x11534590 // int
#define ADDR_LOCAL_CLIENTS 0x11534598 // pointer
#define ADDR_CLIENT_GLOBALS 0x115345a0 // pointer
#define ADDR_Prepatch_Header 0x1964080 // pre-patched executable: header and stubs, in __TEXT's padding after __eh_frame
#define ADDR_Prepatch_CodeEnd 0x1965000 // end of that padding
#define ADDR_Prepatch_Slots 0x1a268710 // pre-patched executable: slots, in __DATA's zero-filled tail after __common
#define ADDR_Prepatch_SlotsEnd 0x1a269000 // end of __DATA

// controllers
#define ADDR_GPad_GetGamepadType 0x122f78d // func - returns gamepadType_e, Aspyr's always says DURANGO for a connected pad
#define ADDR_g_gpads 0x5dedb70 // gamepad[2], 0x70 bytes each, mapped port (0-3) at +4 - name is a guess
#define ADDR_g_gpadPorts 0x5dedc50 // { int xinputUserIndex; int controllerIndex; }[4] - name is a guess
#define ADDR_ASL_XInputSlots 0x2108d50 // { SDL_GameController *; SDL_Haptic *; int effect; }[4] - name is a guess
#define ADDR_ASL_XInputGetState 0x1294a8 // func - DWORD XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState)
#define ADDR_ASL_XInputSetState 0x1295ac // func - DWORD XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
#define ADDR_ASL_XInputEnabled 0x1b97300 // bool - XInputEnable state, starts true and nothing changes it - name is a guess
#define ADDR_CFString_HIDVersionNumber 0x1b8b5e0 // CFStringRef - the "VersionNumber" key SDL 2.0.7 reads

//function patches
#define ADDR_Scr_ErrorInternalPatch 0x11a238e // mid-func
#define ADDR_Curl_ContentLengthSetStart 0x159345c // mid-func
#define ADDR_Curl_ContentLengthSetEnd 0x159346f // mid-func
#define ADDR_Lua_CoD_LuaCall_GetProtocolVersion_Inst 0xE1FCA8 // mid-func
#define ADDR_LPC_GetRemoteManifest_Category_Inst 0xEA8C04 // mid-func
#define ADDR_Live_SystemInfo_LarpInstr 0x129dbcc // mid-func
#define ADDR_LobbyHostMsg_SendJoinRequest_GetChangelistCall 0x1232458 // mid-func
#define ADDR_HKAI_InitMapData_DemonWithinCheck 0xe09f68 // mid-func
#define ADDR_ASL_BuildModeTable_WidthCap 0x6f275 // mid-func - jg over modes wider than the desktop
#define ADDR_ASL_BuildModeTable_HeightCap 0x6f27a // mid-func - jg over modes taller than the desktop
#define ADDR_ASL_DXGIOutput_GetModes_WidthCap 0xdee97 // mid-func - jg over modes wider than the desktop
#define ADDR_ASL_DXGIOutput_GetModes_HeightCap 0xdeea0 // mid-func - jg over modes taller than the desktop
#define ADDR_JobThreadsDvar_DefaultInstr 0x1330b91 // mid-func - mov edx, 2 (default job worker thread count, range 2-4)
#define ADDR_ASL_FramesInFlightSemaphoreInstr 0x2accc // mid-func - mov edi, 2 before dispatch_semaphore_create (Metal frames in flight)
#define ADDR_ASL_Present_PresentDrawableCall 0xe91e1 // mid-func - call [objc_msgSend] for [commandBuffer presentDrawable:]
#define ADDR_ASL_Present_CopyToDrawableCall 0xe8fde // mid-func - call [objc_msgSend] for [blitEncoder copyFromTexture:backbuffer ... toTexture:drawable.texture ...]
// where the network version (18530) and the "tu30" string are hardcoded, each within 16 bytes after the address
#define ADDRS_NetworkVersion \
    0x00E078E4, 0x00E181D3, 0x00E83E4E, 0x00EB8389, 0x00EB91A9, 0x00F988A0, 0x0104E0CB, 0x0105973D, \
    0x01059844, 0x0105C368, 0x0105C5DC, 0x011F7C3E, 0x011F7C43, 0x011F7C49, 0x0120B547, 0x0120B56A, \
    0x0123244C, 0x0126F78C, 0x0126F897, 0x0126F8BC, 0x0126F90D, 0x0126F96F, 0x012D35DA, 0x013B5C1D
#define ADDRS_TUVersion 0x016AA513, 0x017251FB, 0x01725209, 0x0172529C, 0x017252AB, 0x017252B7
#define ADDR_JQ_IdleWait_TimeoutInstr 0x11a7551 // mid-func - mov esi, 1 (idle job workers' WaitForSingleObject timeout in ms)
// -- END OFFSETS
