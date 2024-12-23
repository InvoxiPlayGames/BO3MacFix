#include <stdbool.h>
#include "steam/CSteamAPIContext.h"

void init_steam_context(CSteamAPIContext *context);

CSteamID steam_id();
const char *steam_name();

void steam_friends_refresh_cache();
bool steam_friends_with_user(CSteamID id);

void steam_dlc_refresh_cache();
bool steam_dlc_owns(AppId_t id);
bool steam_dlc_installed(AppId_t id);
