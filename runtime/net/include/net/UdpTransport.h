#pragma once
// UDP transport (IPv4, non-blocking). Winsock on Windows, BSD sockets elsewhere; none of that leaks into this header.
// Wire format of one datagram: 'D' 'K' type(u8) then per type:
//   Connect / ConnectAck / Disconnect: nothing
//   Unreliable: payload
//   Reliable:   seq(u32 LE) payload     (per-peer sequence, delivered in order)
//   Ack:        next expected reliable seq (u32 LE); cumulative, also the keep-alive
//   Query / QueryReply (v5, no connection needed): an encoded net::Query / net::ServerInfo message
// Reliability is go-back-N flavoured: the receiver accepts only the next expected sequence and the sender
// resends unacked packets after a timeout. Not built for the browser (no raw UDP there); see Transport.h.
#include "net/Transport.h"

#include <functional>
#include <memory>

namespace drakan::net {

class UdpTransport final : public Transport {
public:
    UdpTransport();
    ~UdpTransport() override;
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    // False if sockets cannot be initialised on this host.
    bool available() const;

    bool listen(uint16_t port) override;
    PeerId connect(const std::string& host, uint16_t port) override;
    void disconnect(PeerId peer) override;
    using Transport::send;
    bool send(PeerId peer, Channel channel, const uint8_t* data, size_t size) override;
    bool poll(TransportEvent& out) override;
    uint16_t localPort() const override;

    // v5 LAN / direct discovery: a Query datagram that reaches a listening transport is answered with the bytes this
    // function returns for the request body (empty = no answer). Call before or after listen().
    using QueryResponder = std::function<std::vector<uint8_t>(const std::vector<uint8_t>& request)>;
    void setQueryResponder(QueryResponder responder);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace drakan::net
