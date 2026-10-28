#ifndef PS2_GIF_ARBITER_H
#define PS2_GIF_ARBITER_H

#include <cstdint>
#include <functional>
#include <vector>

enum class GifPathId : uint8_t {
    Path1 = 1,
    Path2 = 2,
    Path3 = 3,
};

struct GifArbiterPacket {
    GifPathId pathId;
    bool path2DirectHl = false;
    bool path3Image = false;
    std::vector<uint8_t> data;
};

class GifArbiter {
public:
    using ProcessPacketFn = std::function<void(const uint8_t*, uint32_t)>;

    GifArbiter() = default;
    explicit GifArbiter(ProcessPacketFn processFn);

    void setProcessPacketFn(ProcessPacketFn fn) { m_processFn = std::move(fn); }
    // Drakan: variant that also receives the path (1..3); takes precedence when set.
    using ProcessPathPacketFn = std::function<void(uint8_t, const uint8_t*, uint32_t)>;
    void setProcessPathPacketFn(ProcessPathPacketFn fn) { m_processPathFn = std::move(fn); }

    void submit(GifPathId pathId, const uint8_t* data, uint32_t sizeBytes, bool path2DirectHl = false);

    void drain();
    bool empty() const { return m_count == 0; }

private:
    ProcessPacketFn m_processFn;
    ProcessPathPacketFn m_processPathFn;
    std::vector<GifArbiterPacket> m_queue; // slots [0, m_count) are queued; the rest keep their buffers for reuse
    size_t m_count = 0;

    static bool isImagePacket(const uint8_t* data, uint32_t sizeBytes);
    static uint8_t pathPriority(GifPathId id);
};

#endif
