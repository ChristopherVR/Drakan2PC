#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_capture.h"
#include <algorithm>
#include <cstring>

GifArbiter::GifArbiter(ProcessPacketFn processFn) : m_processFn(std::move(processFn)) {}

bool GifArbiter::isImagePacket(const uint8_t* data, uint32_t sizeBytes) {
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    return flg == 2u;
}

void GifArbiter::submit(GifPathId pathId, const uint8_t* data, uint32_t sizeBytes, bool path2DirectHl) {
    if (!data || sizeBytes < 16 || !m_processFn)
        return;

    if (m_count == m_queue.size())
        m_queue.emplace_back();
    GifArbiterPacket& pkt = m_queue[m_count++];
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.data.assign(data, data + sizeBytes);
}

void GifArbiter::drain() {
    if (!m_processFn)
        return;

    std::stable_sort(m_queue.begin(), m_queue.begin() + m_count,
                     [](const GifArbiterPacket& a, const GifArbiterPacket& b) {
                         // DIRECTHL cannot preempt PATH3 IMAGE transfers.
                         if (a.path2DirectHl != b.path2DirectHl || a.path3Image != b.path3Image) {
                             if (a.path3Image && b.path2DirectHl)
                                 return true;
                             if (a.path2DirectHl && b.path3Image)
                                 return false;
                         }
                         return pathPriority(a.pathId) < pathPriority(b.pathId);
                     });

    for (size_t i = 0; i < m_count; ++i) {
        auto& pkt = m_queue[i];
        if (!pkt.data.empty()) {
            GsCapture::instance().onPacket(static_cast<uint8_t>(pkt.pathId), pkt.path2DirectHl, pkt.path3Image,
                                           pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
            if (m_processPathFn)
                m_processPathFn(static_cast<uint8_t>(pkt.pathId), pkt.data.data(),
                                static_cast<uint32_t>(pkt.data.size()));
            else
                m_processFn(pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
        }
    }
    m_count = 0;
}

uint8_t GifArbiter::pathPriority(GifPathId id) {
    return static_cast<uint8_t>(id);
}
