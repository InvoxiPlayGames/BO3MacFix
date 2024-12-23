#ifndef EMSTEAM_ISTEAMAPPS_H_
#define EMSTEAM_ISTEAMAPPS_H_
#include "types.h"

typedef struct _ISteamApps {
    bool (*BIsSubscribed)( void * );
    bool (*BIsLowViolence)( void * );
    bool (*BIsCybercafe)( void * );
    bool (*BIsVACBanned)( void * );
    const char *(*GetCurrentGameLanguage)( void * );
    const char *(*GetAvailableGameLanguages)( void * );
    bool (*BIsSubscribedApp)( void *, AppId_t appID );
    bool (*BIsDlcInstalled)( void *, AppId_t appID );
    uint32_t (*GetEarliestPurchaseUnixTime)( void *, AppId_t nAppID );
    bool (*BIsSubscribedFromFreeWeekend)( void * );
    int (*GetDLCCount)( void * );
    bool (*BGetDLCDataByIndex)( void *, int iDLC, AppId_t *pAppID, bool *pbAvailable, char *pchName, int cchNameBufferSize );
    void (*InstallDLC)( void *, AppId_t nAppID );
    void (*UninstallDLC)( void *, AppId_t nAppID );
    void (*RequestAppProofOfPurchaseKey)( void *, AppId_t nAppID );
    bool (*GetCurrentBetaName)( void *, char *pchName, int cchNameBufferSize );
    bool (*MarkContentCorrupt)( void *, bool bMissingFilesOnly );
    uint32_t (*GetInstalledDepots)( void *, AppId_t appID, DepotId_t *pvecDepots, uint32_t cMaxDepots );
    uint32_t (*GetAppInstallDir)( void *, AppId_t appID, char *pchFolder, uint32_t cchFolderBufferSize );
    bool (*BIsAppInstalled)( void *, AppId_t appID );
    CSteamID (*GetAppOwner)( void * );
    const char *(*GetLaunchQueryParam)( void *, const char *pchKey );
    bool (*GetDlcDownloadProgress)( void *, AppId_t nAppID, uint64_t *punBytesDownloaded, uint64_t *punBytesTotal );
    int (*GetAppBuildId)( void * );
    void (*RequestAllProofOfPurchaseKeys)( void * );
} ISteamApps;
#endif // EMSTEAM_ISTEAMAPPS_H_
