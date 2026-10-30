#include "net/UdpTransport.h"

#include <chrono>
#include <cstring>
#include <deque>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace drakan::net {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
const SocketHandle kBadSocket = INVALID_SOCKET;
void closeSocket(SocketHandle s) {
    closesocket(s);
}
bool setNonBlocking(SocketHandle s) {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
bool wouldBlock() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}
bool socketsStartup() {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
}
void socketsShutdown() {
    WSACleanup();
}
#else
using SocketHandle = int;
const SocketHandle kBadSocket = -1;
void closeSocket(SocketHandle s) {
    ::close(s);
}
bool setNonBlocking(SocketHandle s) {
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
bool wouldBlock() {
    return errno == EWOULDBLOCK || errno == EAGAIN;
}
bool socketsStartup() {
    return true;
}
void socketsShutdown() {}
#endif

constexpr uint8_t kMagic0 = 'D';
constexpr uint8_t kMagic1 = 'K';
enum PacketType : uint8_t { kConnect = 1, kConnectAck = 2, kDisconnect = 3, kUnreliable = 4, kReliable = 5, kAck = 6 };

constexpr int64_t kConnectRetryMs = 250;
constexpr int64_t kConnectTimeoutMs = 5000;
constexpr int64_t kResendMs = 150;
constexpr int64_t kKeepAliveMs = 1000;
constexpr int64_t kTimeoutMs = 10000;
constexpr size_t kWindow = 128;
constexpr size_t kMaxBacklog = 2048;
constexpr size_t kHeaderSize = 3;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

struct UdpTransport::Impl {
    struct InFlight {
        uint32_t seq = 0;
        std::vector<uint8_t> packet;
        int64_t lastSent = 0;
    };
    struct Peer {
        PeerId id = kInvalidPeer;
        sockaddr_in addr{};
        bool connected = false;
        int64_t connectStart = 0;
        int64_t lastRecv = 0;
        int64_t lastSend = 0;
        uint32_t nextSeq = 1;
        uint32_t expectedSeq = 1;
        bool ackPending = false;
        std::deque<InFlight> inflight;
        std::deque<std::vector<uint8_t>> backlog; // payloads waiting for window space
    };

    bool started = false;
    SocketHandle sock = kBadSocket;
    uint16_t port = 0;
    bool listening = false;
    PeerId nextPeer = 1;
    std::unordered_map<PeerId, Peer> peers;
    std::unordered_map<uint64_t, PeerId> byAddr;
    std::deque<TransportEvent> events;

    Impl() { started = socketsStartup(); }
    ~Impl() {
        // Tell the remote side we are gone.
        for (auto& kv : peers)
            sendControl(kv.second, kDisconnect);
        closeSock();
        if (started)
            socketsShutdown();
    }

    void closeSock() {
        if (sock != kBadSocket) {
            closeSocket(sock);
            sock = kBadSocket;
        }
    }

    static uint64_t key(const sockaddr_in& a) { return (uint64_t(a.sin_addr.s_addr) << 16) | a.sin_port; }

    bool ensureSocket(uint16_t bindPort) {
        if (sock != kBadSocket)
            return bindPort == 0 || bindPort == port;
        if (!started)
            return false;
        SocketHandle s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == kBadSocket)
            return false;
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(bindPort);
        if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || !setNonBlocking(s)) {
            closeSocket(s);
            return false;
        }
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
            closeSocket(s);
            return false;
        }
        sock = s;
        port = ntohs(bound.sin_port);
        return true;
    }

    void rawSend(const sockaddr_in& to, const std::vector<uint8_t>& pkt) {
        if (sock == kBadSocket)
            return;
        ::sendto(sock, reinterpret_cast<const char*>(pkt.data()), static_cast<int>(pkt.size()), 0,
                 reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }

    static std::vector<uint8_t> header(uint8_t type) { return {kMagic0, kMagic1, type}; }

    void sendControl(Peer& p, uint8_t type) {
        rawSend(p.addr, header(type));
        p.lastSend = nowMs();
    }

    void sendAck(Peer& p) {
        auto pkt = header(kAck);
        putU32(pkt, p.expectedSeq);
        rawSend(p.addr, pkt);
        p.lastSend = nowMs();
        p.ackPending = false;
    }

    void queueReliable(Peer& p, std::vector<uint8_t> payload) {
        InFlight f;
        f.seq = p.nextSeq++;
        f.packet = header(kReliable);
        putU32(f.packet, f.seq);
        f.packet.insert(f.packet.end(), payload.begin(), payload.end());
        f.lastSent = nowMs();
        rawSend(p.addr, f.packet);
        p.lastSend = f.lastSent;
        p.inflight.push_back(std::move(f));
    }

    Peer* newPeer(const sockaddr_in& addr, bool connected) {
        Peer p;
        p.id = nextPeer++;
        p.addr = addr;
        p.connected = connected;
        p.connectStart = p.lastRecv = p.lastSend = nowMs();
        byAddr[key(addr)] = p.id;
        auto it = peers.emplace(p.id, std::move(p)).first;
        return &it->second;
    }

    void removePeer(PeerId id, bool emit) {
        auto it = peers.find(id);
        if (it == peers.end())
            return;
        byAddr.erase(key(it->second.addr));
        peers.erase(it);
        if (emit) {
            TransportEvent ev;
            ev.type = TransportEventType::Disconnected;
            ev.peer = id;
            events.push_back(std::move(ev));
        }
    }

    void emitConnected(PeerId id) {
        TransportEvent ev;
        ev.type = TransportEventType::Connected;
        ev.peer = id;
        events.push_back(std::move(ev));
    }

    void handleDatagram(const sockaddr_in& from, const uint8_t* d, size_t n) {
        if (n < kHeaderSize || d[0] != kMagic0 || d[1] != kMagic1)
            return;
        const uint8_t type = d[2];
        const uint8_t* body = d + kHeaderSize;
        const size_t bodyLen = n - kHeaderSize;
        auto known = byAddr.find(key(from));
        Peer* p = known == byAddr.end() ? nullptr : &peers.at(known->second);

        if (type == kConnect) {
            if (!listening)
                return;
            if (!p) {
                p = newPeer(from, true);
                emitConnected(p->id);
            }
            p->lastRecv = nowMs();
            sendControl(*p, kConnectAck);
            return;
        }
        if (!p)
            return;
        p->lastRecv = nowMs();
        switch (type) {
            case kConnectAck:
                if (!p->connected) {
                    p->connected = true;
                    emitConnected(p->id);
                }
                break;
            case kDisconnect:
                removePeer(p->id, true);
                break;
            case kUnreliable:
                if (p->connected) {
                    TransportEvent ev;
                    ev.type = TransportEventType::Message;
                    ev.peer = p->id;
                    ev.channel = Channel::Unreliable;
                    ev.data.assign(body, body + bodyLen);
                    events.push_back(std::move(ev));
                }
                break;
            case kReliable:
                if (p->connected && bodyLen >= 4) {
                    const uint32_t seq = getU32(body);
                    if (seq == p->expectedSeq) {
                        ++p->expectedSeq;
                        TransportEvent ev;
                        ev.type = TransportEventType::Message;
                        ev.peer = p->id;
                        ev.channel = Channel::Reliable;
                        ev.data.assign(body + 4, body + bodyLen);
                        events.push_back(std::move(ev));
                    }
                    p->ackPending = true; // duplicates and gaps also re-announce what we expect
                }
                break;
            case kAck:
                if (p->connected && bodyLen >= 4) {
                    const uint32_t next = getU32(body);
                    while (!p->inflight.empty() && static_cast<int32_t>(next - p->inflight.front().seq) > 0)
                        p->inflight.pop_front();
                    while (!p->backlog.empty() && p->inflight.size() < kWindow) {
                        queueReliable(*p, std::move(p->backlog.front()));
                        p->backlog.pop_front();
                    }
                }
                break;
            default:
                break;
        }
    }

    void receiveAll() {
        if (sock == kBadSocket)
            return;
        uint8_t buf[2048];
        for (int i = 0; i < 512; ++i) {
            sockaddr_in from{};
            socklen_t len = sizeof(from);
            const int n = static_cast<int>(::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                                      reinterpret_cast<sockaddr*>(&from), &len));
            if (n < 0) {
                if (wouldBlock())
                    break;
                continue; // e.g. ICMP port unreachable surfaced as a reset on Windows
            }
            handleDatagram(from, buf, static_cast<size_t>(n));
        }
    }

    void timers() {
        const int64_t now = nowMs();
        std::vector<PeerId> dead;
        for (auto& kv : peers) {
            Peer& p = kv.second;
            if (!p.connected) {
                if (now - p.connectStart > kConnectTimeoutMs) {
                    dead.push_back(p.id);
                } else if (now - p.lastSend >= kConnectRetryMs) {
                    sendControl(p, kConnect);
                }
                continue;
            }
            if (now - p.lastRecv > kTimeoutMs) {
                dead.push_back(p.id);
                continue;
            }
            for (auto& f : p.inflight) {
                if (now - f.lastSent >= kResendMs) {
                    rawSend(p.addr, f.packet);
                    f.lastSent = now;
                    p.lastSend = now;
                }
            }
            if (p.ackPending || now - p.lastSend >= kKeepAliveMs)
                sendAck(p);
        }
        for (PeerId id : dead)
            removePeer(id, true);
    }
};

UdpTransport::UdpTransport() : impl_(new Impl) {}
UdpTransport::~UdpTransport() = default;

bool UdpTransport::available() const {
    return impl_->started;
}

bool UdpTransport::listen(uint16_t port) {
    if (impl_->listening || !impl_->ensureSocket(port))
        return false;
    impl_->listening = true;
    return true;
}

PeerId UdpTransport::connect(const std::string& host, uint16_t port) {
    if (!impl_->ensureSocket(0))
        return kInvalidPeer;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
        return kInvalidPeer;
    sockaddr_in addr{};
    std::memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);
    addr.sin_port = htons(port);
    if (impl_->byAddr.count(Impl::key(addr)))
        return kInvalidPeer;
    Impl::Peer* p = impl_->newPeer(addr, false);
    impl_->sendControl(*p, kConnect);
    return p->id;
}

void UdpTransport::disconnect(PeerId peer) {
    auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end())
        return;
    impl_->sendControl(it->second, kDisconnect);
    impl_->removePeer(peer, false);
}

bool UdpTransport::send(PeerId peer, Channel channel, const uint8_t* data, size_t size) {
    if (size > kMaxPacketPayload)
        return false;
    auto it = impl_->peers.find(peer);
    if (it == impl_->peers.end() || !it->second.connected)
        return false;
    Impl::Peer& p = it->second;
    if (channel == Channel::Unreliable) {
        auto pkt = Impl::header(kUnreliable);
        pkt.insert(pkt.end(), data, data + size);
        impl_->rawSend(p.addr, pkt);
        p.lastSend = nowMs();
        return true;
    }
    std::vector<uint8_t> payload(data, data + size);
    if (p.inflight.size() < kWindow && p.backlog.empty()) {
        impl_->queueReliable(p, std::move(payload));
    } else if (p.backlog.size() < kMaxBacklog) {
        p.backlog.push_back(std::move(payload));
    } else {
        return false;
    }
    return true;
}

bool UdpTransport::poll(TransportEvent& out) {
    if (impl_->events.empty()) {
        impl_->receiveAll();
        impl_->timers();
    }
    if (impl_->events.empty())
        return false;
    out = std::move(impl_->events.front());
    impl_->events.pop_front();
    return true;
}

uint16_t UdpTransport::localPort() const {
    return impl_->port;
}

} // namespace drakan::net
