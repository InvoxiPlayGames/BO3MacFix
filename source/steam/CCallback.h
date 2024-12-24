#ifndef EMSTEAM_CCALLBACK_H_
#define EMSTEAM_CCALLBACK_H_
#include "types.h"

typedef struct _CCallback {
    void *vtable;
    uint8_t m_nCallbackFlags;
    int m_iCallback;
    void *m_pObj;
    void *m_Func;
    void *unknown;
} CCallback;

typedef struct _CCallResult {
    void *vtable;
    uint8_t m_nCallbackFlags;
    int m_iCallback;
    SteamAPICall_t m_hAPICall;
    void *m_pObj;
    void *m_Func;
    void *unknown; // is this even here
} CCallResult;

// we can use these functions despite not actually linking against the api, because the exec does
// thank you dyld :3

void SteamAPI_RegisterCallback(CCallback *pCallback, int iCallback);
void SteamAPI_UnegisterCallback(CCallback *pCallback);

void SteamAPI_RegisterCallResult(CCallResult *pCallback, SteamAPICall_t hAPICall);
void SteamAPI_UnegisterCallResult(CCallResult *pCallback, SteamAPICall_t hAPICall);

#endif // EMSTEAM_CCALLBACK_H_
