//online: the hosts file for the game's servers
#include "ge.h"
#include <cstdio>
#include <cstdlib>

namespace ge {

//the real servers (Nintendo WFC, DemonWare) are gone, a community server answers instead
//so the names get pointed at it here, same format as /etc/hosts
#ifndef GE_HOSTS_DEFAULT
#define GE_HOSTS_DEFAULT "config/hosts.txt"
#endif
void install_hosts() {
    const char* path = std::getenv("GE_HOSTS");
    if (!path) path = GE_HOSTS_DEFAULT;
    ios_net_log(std::getenv("GE_NETLOG") != nullptr);
    if (!*path) return;
    FILE* f = std::fopen(path, "r");
    if (!f) { rt_log("goldeneye: no hosts file %s: the game's servers as the system resolves them", path); return; }
    char line[512], ip[64], name[256];
    int n = 0;
    while (std::fgets(line, sizeof line, f)) {
        if (char* h = std::strchr(line, '#')) *h = 0;
        if (std::sscanf(line, "%63s %255s", ip, name) == 2) { ios_net_add_host(name, ip); ++n; }
    }
    std::fclose(f);
    rt_log("goldeneye: %d host name(s) from %s", n, path);
}

} //namespace ge
