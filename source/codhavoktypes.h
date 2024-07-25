#include <stdint.h>

#define ASSET_TYPE_NAVMESH 0x61
#define ASSET_TYPE_NAVVOLUME 0x62

typedef struct _NavMeshData {
    char *name;
    int navMeshSettingsBufferSize;
    uint8_t *navMeshSettingsBuffer;
    int userEdgePairsBufferSize;
    uint8_t *userEdgePairsBuffer;
    int clusterGraphBufferSize;
    uint8_t *clusterGraphBuffer;
    int mediatorBufferSize;
    uint8_t *mediatorBuffer;
    int metadataBufferSize;
    uint8_t *metadataBuffer;
    int clearanceCacheSeederBufferSize;
    uint8_t *clearanceCacheSeederBuffer;
} NavMeshData;

typedef struct _NavVolumeData {
    char *name;
    int smallNavVolumeSettingsBufferSize;
    uint8_t *smallNavVolumeSettingsBuffer;
    int smallNavVolumeMediatorBufferSize;
    uint8_t *smallNavVolumeMediatorBuffer;
    int bigNavVolumeSettingsBufferSize;
    uint8_t *bigNavVolumeSettingsBuffer;
    int bigNavVolumeMediatorBufferSize;
    uint8_t *bigNavVolumeMediatorBuffer;
} NavVolumeData;

typedef struct _clumpheader {
    int bool_hasNavVolume; // 0x0

    int navMeshSettingsBufferSize; // 0x4
    int userEdgePairsBufferSize; // 0x8
    int clusterGraphBufferSize; // 0xC
    int mediatorBufferSize; // 0x10
    int metadataBufferSize; // 0x14
    int clearanceCacheSeederBufferSize; // 0x18

    int smallNavVolumeSettingsBufferSize; // 0x1C
    int smallNavVolumeMediatorBufferSize; // 0x20
    int bigNavVolumeSettingsBufferSize; // 0x24
    int bigNavVolumeMediatorBufferSize; // 0x28

    int unused; // 0x2C
} clumpheader;

