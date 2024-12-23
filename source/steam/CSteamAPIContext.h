#ifndef EMSTEAM_CSTEAMAPICONTEXT_H_
#define EMSTEAM_CSTEAMAPICONTEXT_H_
#include "interfaces.h"

typedef struct _CSteamAPIContext
{
    void *m_pSteamClient;
    ISteamUser **m_pSteamUser;
    ISteamFriends **m_pSteamFriends;
    void *m_pSteamUtils;
    void *m_pSteamMatchmaking;
    void *m_pSteamUserStats;
    ISteamApps **m_pSteamApps;
    void *m_pSteamMatchmakingServers;
    void *m_pSteamNetworking;
    void *m_pSteamRemoteStorage;
    void *m_pSteamScreenshots;
    void *m_pSteamHTTP;
    void *m_pSteamUnifiedMessages;
    void *m_pController;
    void *m_pSteamUGC;
    void *m_pSteamAppList;
    void *m_pSteamMusic;
    void *m_pSteamMusicRemote;
    void *m_pSteamHTMLSurface;
    void *m_pSteamInventory;
    void *m_pSteamVideo;
} CSteamAPIContext;
#endif // EMSTEAM_CSTEAMAPICONTEXT_H_
