#pragma once
#include "runtime/object_motion_job_fwd.h"
#include <cstdint>
#include <functional>
#include "runtime/gs/ps2_gif_arbiter.h"
// Execute the production queue against a controlled job, without a game image.
class PS2Memory {
public:
    std::function<void(const uint8_t*, uint32_t)> execute;
    void submitGifPacket(GifPathId, const uint8_t* data, uint32_t size, bool, bool = false) { execute(data, size); }
    void runVif1Job(const uint8_t* data, uint32_t size, const drakan_object_motion::Job*) { execute(data, size); }
    void runVif1Slice(const uint8_t* data, uint32_t size, uint32_t, const drakan_object_motion::Job*, bool, bool) {
        execute(data, size);
    }
};
