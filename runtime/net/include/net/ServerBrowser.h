#pragma once
// LAN / direct server discovery (protocol v5): sends Query datagrams (UDP broadcast to kDefaultPort ..
// kDefaultPort + kDiscoveryPortCount - 1, plus loopback for a server on this PC, or one unicast host:port) and collects
// the ServerInfo answers. Non-blocking; call poll() regularly. Not built for Emscripten (needs raw UDP).
#include "net/Protocol.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace drakan::net {

struct ServerEntry {
    std::string host; // dotted IPv4
    uint16_t port = 0;
    ServerInfo info;
    uint32_t pingMs = 0;     // query send to answer
    uint64_t lastSeenMs = 0; // steady clock (UdpTransport epoch): only for ageMs
    uint32_t ageMs = 0;      // since the last answer, refreshed by every poll()
};

class ServerBrowser {
public:
    ServerBrowser();
    ~ServerBrowser();
    ServerBrowser(const ServerBrowser&) = delete;
    ServerBrowser& operator=(const ServerBrowser&) = delete;

    bool available() const;
    void queryLan();
    bool queryHost(const std::string& host, uint16_t port);
    void clear();
    // Drain received answers; entries are keyed by host:port (a repeated answer refreshes the entry).
    const std::vector<ServerEntry>& poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace drakan::net
