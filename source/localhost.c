#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dyld-interposing.h"

#define Local_Address_Limit 8

typedef struct {
    struct hostent entry;
    char name[256];
    char *aliases[1];
    struct in_addr addresses[Local_Address_Limit];
    char *address_list[Local_Address_Limit + 1];
} LocalHostEntry;

static __thread LocalHostEntry local_host_entry;

static bool is_local_host_name(const char *name) {
    char local_name[256];
    if (name == NULL || gethostname(local_name, sizeof(local_name)) != 0)
        return false;
    local_name[sizeof(local_name) - 1] = '\0';
    return strcasecmp(name, local_name) == 0;
}

static int collect_ipv4_addresses(struct in_addr *addresses, int limit) {
    struct ifaddrs *interfaces = NULL;
    int count = 0;
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs *interface = interfaces; interface != NULL && count < limit; interface = interface->ifa_next) {
            if (interface->ifa_addr == NULL || interface->ifa_addr->sa_family != AF_INET)
                continue;
            if (!(interface->ifa_flags & IFF_UP) || (interface->ifa_flags & IFF_LOOPBACK))
                continue;
            addresses[count++] = ((struct sockaddr_in *)interface->ifa_addr)->sin_addr;
        }
        freeifaddrs(interfaces);
    }
    if (count == 0)
        addresses[count++].s_addr = htonl(INADDR_LOOPBACK);
    return count;
}

static struct hostent *local_host_lookup(const char *name) {
    LocalHostEntry *local = &local_host_entry;
    strlcpy(local->name, name, sizeof(local->name));
    int count = collect_ipv4_addresses(local->addresses, Local_Address_Limit);
    for (int i = 0; i < count; i++)
        local->address_list[i] = (char *)&local->addresses[i];
    local->address_list[count] = NULL;
    local->aliases[0] = NULL;
    local->entry.h_name = local->name;
    local->entry.h_aliases = local->aliases;
    local->entry.h_addrtype = AF_INET;
    local->entry.h_length = sizeof(struct in_addr);
    local->entry.h_addr_list = local->address_list;
    return &local->entry;
}

static struct hostent *gethostbyname_without_mdns_wait(const char *name) {
    if (is_local_host_name(name))
        return local_host_lookup(name);
    return gethostbyname(name);
}
DYLD_INTERPOSE(gethostbyname_without_mdns_wait, gethostbyname);
