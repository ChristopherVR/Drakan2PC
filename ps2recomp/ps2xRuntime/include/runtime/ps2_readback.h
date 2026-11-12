#pragma once
// GS local-to-host reads ("store image") without stopping the frame pipeline.
//
// A game reads pixels back from the GS in three steps: it sends the transfer set-up with a FINISH register write,
// polls CSR.FINISH until the GS has drawn everything queued so far, then starts a VIF1 DMA with CHCR.DIR=0 that
// carries the pixels to memory. Here the VU1 worker and the GS worker run behind the EE thread, so both waits stop
// the EE until the two workers have finished the frame: the three threads run one after another instead of side by
// side (docs/journal/2026-11-12/perf-round2.md).
//
// PS2X_READBACK selects how such a read is served:
//   poll   the guest's own FINISH poll, then a blocking read (the behaviour up to 2026-11-12)
//   eager  the FINISH poll of a known store-image routine succeeds at once; the read itself still waits for the
//          workers, so the bytes are exactly the same and only the spinning poll is gone
//   late   as eager, and a read whose caller and size were seen before is answered with the bytes of that caller's
//          previous read; the current pixels are fetched by a task queued behind the frame's draws and become the
//          answer to the next read. The guest sees such data one read late.
// PS2X_READBACK_VERIFY=1 (with late): every read is also done the blocking way; reads whose late answer would have
// differed are counted (`[readback] verify` at exit and `rb_diff/s` in the PS2X_PERF line) and the exact bytes are
// served.
//
// The game side names the routine: setFinishPollPc (guest pc of the routine's first CSR read) and noteCaller (a key
// for the next read, taken from the routine's arguments). Without them every read is served the poll way.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ps2x_readback {
enum class Mode { Poll, Eager, Late };

inline Mode mode() {
    static const Mode value = [] {
        const char* text = std::getenv("PS2X_READBACK");
        if (text && std::strcmp(text, "eager") == 0)
            return Mode::Eager;
        if (text && std::strcmp(text, "late") == 0)
            return Mode::Late;
        return Mode::Poll;
    }();
    return value;
}

inline bool verify() {
    static const bool value = [] {
        const char* text = std::getenv("PS2X_READBACK_VERIFY");
        return text && text[0] == '1';
    }();
    return value;
}

constexpr uint32_t kMaxLateBytes = 16384; // a 64x64 CT32 rectangle; larger reads (screen grabs) always wait

struct State {
    std::atomic<uint32_t> finishPollPc{0};
    std::atomic<uint64_t> callerKey{0}; // key of the store-image call in progress on the EE thread, 0 = unknown
    // counters (PS2X_PERF line, exit line)
    std::atomic<uint64_t> finishPolls{0}; // CSR reads that found FINISH clear
    std::atomic<uint64_t> eagerFinish{0}; // polls answered at once
    std::atomic<uint64_t> reads{0}, readBytes{0}, waitNs{0};
    std::atomic<uint64_t> lateServed{0}, lateMissed{0};
    std::atomic<uint64_t> verified{0}, verifyDifferent{0};

    std::mutex lock;
    std::unordered_map<uint64_t, std::vector<uint8_t>> previous; // caller key -> bytes of its last completed read

    bool lookup(uint64_t key, uint32_t bytes, uint8_t* out) {
        std::lock_guard<std::mutex> guard(lock);
        const auto found = previous.find(key);
        if (found == previous.end() || found->second.size() != bytes)
            return false;
        std::memcpy(out, found->second.data(), bytes);
        return true;
    }
    void store(uint64_t key, const uint8_t* data, uint32_t bytes) {
        std::lock_guard<std::mutex> guard(lock);
        if (previous.size() >= 64 && previous.find(key) == previous.end())
            previous.clear(); // callers are a handful; a runaway key set must not grow without bound
        previous[key].assign(data, data + bytes);
    }
};

inline State& state() {
    static State value;
    return value;
}

inline void setFinishPollPc(uint32_t pc) {
    state().finishPollPc.store(pc, std::memory_order_relaxed);
}
// The store-image routine was entered with these arguments (anything that identifies what is read, not where to).
inline void noteCaller(uint64_t key) {
    state().callerKey.store(key ? key : 1, std::memory_order_relaxed);
}
// True when a CSR read at this guest pc is the FINISH poll of the known routine and may be answered at once.
inline bool eagerFinish(uint32_t pc) {
    if (mode() == Mode::Poll)
        return false;
    State& s = state();
    const uint32_t poll = s.finishPollPc.load(std::memory_order_relaxed);
    if (!poll || pc != poll)
        return false;
    s.eagerFinish.fetch_add(1, std::memory_order_relaxed);
    return true;
}
} // namespace ps2x_readback
