#ifndef EMSTEAM_ISTEAMUSER_H_
#define EMSTEAM_ISTEAMUSER_H_
#include "types.h"

typedef struct _ISteamUser {
    HSteamUser (*GetHSteamUser)( void * );
    bool (*BLoggedOn)( void * );
    CSteamID (*GetSteamID)( void * );
    int (*InitiateGameConnection)( void *, void *pAuthBlob, int cbMaxAuthBlob, CSteamID steamIDGameServer, uint32_t unIPServer, uint16_t usPortServer, bool bSecure );
    void (*TerminateGameConnection)( void *, uint32_t unIPServer, uint16_t usPortServer );
    void (*TrackAppUsageEvent)( void *, CGameID *gameID, int eAppUsageEvent, const char *pchExtraInfo );
    bool (*GetUserDataFolder)( void *, char *pchBuffer, int cubBuffer );
    void (*StartVoiceRecording)( void * );
    void (*StopVoiceRecording)( void * );
    int (*GetAvailableVoice)( void *, uint32_t *pcbCompressed, uint32_t *pcbUncompressed, uint32_t nUncompressedVoiceDesiredSampleRate );
    int (*GetVoice)( void *, bool bWantCompressed, void *pDestBuffer, uint32_t cbDestBufferSize, uint32_t *nBytesWritten, bool bWantUncompressed, void *pUncompressedDestBuffer, uint32_t cbUncompressedDestBufferSize, uint32_t *nUncompressBytesWritten, uint32_t nUncompressedVoiceDesiredSampleRate );
    int (*DecompressVoice)( void *, const void *pCompressed, uint32_t cbCompressed, void *pDestBuffer, uint32_t cbDestBufferSize, uint32_t *nBytesWritten, uint32_t nDesiredSampleRate );
    uint32_t (*GetVoiceOptimalSampleRate)( void * );
    int (*GetAuthSessionTicket)( void *, void *pTicket, int cbMaxTicket, uint32_t *pcbTicket );
    int (*BeginAuthSession)( void *, const void *pAuthTicket, int cbAuthTicket, CSteamID steamID );
    void (*EndAuthSession)( void *, CSteamID steamID );
    void (*CancelAuthTicket)( void *, int hAuthTicket );
    int (*UserHasLicenseForApp)( void *, CSteamID steamID, AppId_t appID );
    bool (*BIsBehindNAT)( void * );
    void (*AdvertiseGame)( void *, CSteamID steamIDGameServer, uint32_t unIPServer, uint16_t usPortServer );
    SteamAPICall_t (*RequestEncryptedAppTicket)( void *, void *pDataToInclude, int cbDataToInclude );
    bool (*GetEncryptedAppTicket)( void *, void *pTicket, int cbMaxTicket, uint32_t *pcbTicket );
    int (*GetGameBadgeLevel)( void *, int nSeries, bool bFoil );
    int (*GetPlayerSteamLevel)( void * );
    SteamAPICall_t (*RequestStoreAuthURL)( void *, const char *pchRedirectURL );
    bool (*BIsPhoneVerified)( void * );
    bool (*BIsTwoFactorEnabled)( void * );
} ISteamUser;
#endif // EMSTEAM_ISTEAMUSER_H_