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
#define ADDR_Com_GetBuildIntField 0xf16d9a // func
#define ADDR_SDL_SetWindowTitle 0x1944d0 // func

//globals
#define ADDR_g_workshopMapId 0x3087dac // char[] - name is a guess
#define ADDR_g_emuPath 0x4ae9c00 // char[] - name is a guess
#define ADDR_sSessionModeState 0x6939a14 // int

//function patches
#define ADDR_Scr_ErrorInternalPatch 0x11a238e // mid-func
#define ADDR_Curl_ContentLengthSetStart 0x159345c // mid-func
#define ADDR_Curl_ContentLengthSetEnd 0x159346f // mid-func
#define ADDR_Lua_CoD_LuaCall_GetProtocolVersion_Inst 0xE1FCA8 // mid-func
#define ADDR_LPC_GetRemoteManifest_Category_Inst 0xEA8C04 // mid-func
#define ADDR_Live_SystemInfo_LarpInstr 0x129dbcc // mid-func
#define ADDR_LobbyHostMsg_SendJoinRequest_GetChangelistCall 0x1232458 // mid-func
#define ADDR_HKAI_InitMapData_DemonWithinCheck 0xe09f68 // mid-func
// -- END OFFSETS
