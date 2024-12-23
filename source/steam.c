#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "steam.h"

static CSteamAPIContext *ctx = NULL;

static CSteamID myId = { 0 };
static const char *originalName = NULL;

static ISteamFriends isteamfriends;
static ISteamApps isteamapps;

void init_steam_context(CSteamAPIContext *context) {
    ctx = context;
    // keep note of our steamid and original name
    myId = (*ctx->m_pSteamUser)->GetSteamID(ctx->m_pSteamUser);
    originalName = (*ctx->m_pSteamFriends)->GetPersonaName(ctx->m_pSteamFriends);
    // keep track of our original vtables since we're overwriting the ones in the context
    memcpy(&isteamfriends, *ctx->m_pSteamFriends, sizeof(ISteamFriends));
    memcpy(&isteamapps, *ctx->m_pSteamApps, sizeof(ISteamApps));
}

CSteamID steam_id() { return myId; }
const char *steam_name() { return originalName; }

// according to steam support
#define MAX_FRIENDS_COUNT 2000
static CSteamID friendsCache[MAX_FRIENDS_COUNT];
static int numFriendsInCache;

void steam_friends_refresh_cache() {
    memset(friendsCache, 0, sizeof(friendsCache));
    numFriendsInCache = 0;
    int friendCount = isteamfriends.GetFriendCount(ctx->m_pSteamFriends, 0x04); // k_EFriendFlagImmediate
    for (int i = 0; i < friendCount && i < MAX_FRIENDS_COUNT; i++) {
        CSteamID friend = isteamfriends.GetFriendByIndex(ctx->m_pSteamFriends, i, 0x04);
        friendsCache[i] = friend;
        numFriendsInCache++;
    }
    printf("populated friends cache with %i users\n", numFriendsInCache);
}

bool steam_friends_with_user(CSteamID id) {
    for (int i = 0; i < numFriendsInCache; i++) {
        if (id.id == friendsCache[i].id)
            return true;
    }
    return false;
}

// there's like 26 dlc but what if they came back!!!1
#define MAX_DLC_COUNT 32
typedef struct _DlcCacheEntry {
    AppId_t app;
    bool installed;
    bool owned;
} DlcCacheEntry;
static DlcCacheEntry dlcCache[MAX_DLC_COUNT];
static int numDlcInCache;

// all released DLC for the game, so we don't need to wait for the game to request it
static const AppId_t allBo3DLCIDs[] = {
    830461, 830460, 830451, 830450, 660850, 648460, 624760, 581450,
    470190, 464370, 460110, 437351, 437350, 426730, 408630, 366850,
    366849, 366848, 366847, 366846, 366845, 366844, 366843, 366842,
    366841, 366840
};
static const int numBo3DLCIDs = 26;

// creates an entry in the cache table, or if it doesn't exist, creates it
// if we have too many entries then
static DlcCacheEntry *get_cache_entry(AppId_t appid) {
    // check if we have a cache entry for that appid
    for (int i = 0; i < numDlcInCache; i++) {
        if (dlcCache[i].app == appid)
            return &dlcCache[i];
    }
    // if we've reached the max amount then return null
    if (numDlcInCache == MAX_DLC_COUNT)
        return NULL;
    // otherwise create a new dlc cache entry
    DlcCacheEntry *new = &dlcCache[numDlcInCache++];
    new->app = appid;
    new->owned = isteamapps.BIsSubscribedApp(ctx->m_pSteamApps, appid);
    new->installed = isteamapps.BIsDlcInstalled(ctx->m_pSteamApps, appid);
    printf("added %i to dlc cache (owned: %s, installed: %s)\n", new->app, new->owned ? "true" : "false", new->installed ? "true" : "false");
    return new;
}

void steam_dlc_refresh_cache() {
    numDlcInCache = 0;
    memset(dlcCache, 0, sizeof(dlcCache));
    for (int i = 0; i < numBo3DLCIDs; i++) {
        get_cache_entry(allBo3DLCIDs[i]);
    }
    printf("populated dlc cache with %i dlc\n", numDlcInCache);
}

bool steam_dlc_owns(AppId_t id) {
    DlcCacheEntry *cached = get_cache_entry(id);
    if (cached != NULL)
        return cached->owned;
    else
        return isteamapps.BIsSubscribedApp(ctx->m_pSteamApps, id);
}

bool steam_dlc_installed(AppId_t id) {
    DlcCacheEntry *cached = get_cache_entry(id);
    if (cached != NULL)
        return cached->installed;
    else
        return isteamapps.BIsDlcInstalled(ctx->m_pSteamApps, id);
}
