// SID 0x1202 "Resource Request"
#pragma once
#include "FileService.h"
#include "ResourceOverride.h"
#include <functional>
#include <memory>

namespace hle {

struct ResourceRequestWire { // 28 bytes, little endian, as written by the EE (0x1f0d84..)
    u32 cookie;
    u16 flags;
    u16 flagsHi;
    u16 id;
    u16 type;
    u16 sub;
    u16 aux;
    u32 offset;
    u32 length;
    u32 dest; // id at +8, type at +0x0a (measured)
};
static_assert(sizeof(ResourceRequestWire) == 28, "wire size");

class ResourceService {
public:
    enum class Delivery {
        // Default: write data straight into EE/SPU2 memory, then post {cookie,status} batches to EE 0x1302.
        Direct,
        // Faithful-to-IOP experiment: per request post to EE 0x1301 (fno = cookie, payload = 16-byte header
        // {0x10, len, 0, 0 (=no checksum)} followed by the data for EE-memory destinations).
        // No 0x1302 batch is sent in this mode. See README "Open questions".
        Rpc1301,
    };

    ResourceService(FileService& files, IEeMemory& mem, IEeRpcPoster& ee, ISpu2& spu)
        : files_(files), mem_(mem), ee_(ee), spu_(spu) {}
    void setDelivery(Delivery d) { delivery_ = d; }
    // Payload override layer (see ResourceOverride.h). Register before the first request; consulted in registration
    // order.
    void addOverride(std::shared_ptr<IResourceOverride> o) { overrides_.push_back(std::move(o)); }
    // Request observer: called for every successfully located resource request (before the payload is read) on the
    // 0x1202 worker thread, with the decoded id and the request's byte offset. Used for host-side triggers (narration
    // subtitles). Register before the first request.
    using RequestObserver = std::function<void(const ResourceId&, u32 offset)>;
    void setRequestObserver(RequestObserver o) { observer_ = std::move(o); }
    bool skipFailureCompletions = false; // experiment: do not report failed requests (status != 0) to the EE at all

    // `handle` = RPC fno. Processes all entries synchronously, posts completions. Call from a worker thread.
    void handle(u32 handle, const void* send, u32 sendSize);

    static constexpr s32 ST_OK = 0, ST_NOT_FOUND = -1, ST_ZERO = -2, ST_BAD_HANDLE = -4, ST_IO = -5, ST_ALLOC = -6,
                         ST_TOO_BIG = -9; // only -1 and -9 are documented
    static constexpr u32 MAX_LEN = 0x40800;
    static constexpr u32 MAX_BATCH = 64; // completions per 0x1302 call

    // SPU ADPCM fix-ups for flags 0x20 / 0x10 / 0x40 (RSRCIOP 0x4fe8/0x4ffc/0x5048). Exposed for tests.
    static void adpcmFixup(std::vector<u8>& d, u16 flags);

private:
    s32 process(const FileService::Open& f, const ResourceRequestWire& r);
    FileService& files_;
    IEeMemory& mem_;
    IEeRpcPoster& ee_;
    ISpu2& spu_;
    std::vector<std::shared_ptr<IResourceOverride>> overrides_;
    RequestObserver observer_;
    Delivery delivery_ =
        Delivery::Rpc1301; // faithful path; Direct only works when the request carries a real destination
};

} // namespace hle
