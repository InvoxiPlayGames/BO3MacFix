#ifndef EMSTEAM_TYPES_H_
#define EMSTEAM_TYPES_H_

#include <stdint.h>
typedef uint32_t AppId_t;
typedef uint32_t BundleId_t;
typedef uint32_t PackageId_t;
typedef uint32_t DepotId_t;
typedef uint64_t ManifestId_t;
typedef uint64_t SteamAPICall_t;
typedef int HSteamPipe;
typedef int HSteamUser;

typedef struct _CSteamID {
    uint64_t id;
} CSteamID;
typedef struct _CGameID {
    uint64_t id;
} CGameID;

#endif // EMSTEAM_TYPES_H_