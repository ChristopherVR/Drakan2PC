#pragma once
// Vertex-program draws from VU1 runs (prototype, off by default; gsr/vertex_program.h,
// docs/journal/2026-11-12/gpu-vertex-feasibility.md). Header only.
//
// The strip stage of the game's microprogram (an MSCNT that resumes at kStagePc after the object set-up found the
// object inside the view volume) is recognised from the VU state, and its inputs are packed into one record: the
// matrix the set-up stage left in the registers, the constants and the vertex arrays in VU memory. buildStrips() does
// that; the VU1 replay uses it to prove the record's CPU evaluation equal to the packets the microprogram sends
// (DRAKAN_VU_BENCH_GPUPROG, ps2_vu1_check.cpp). intercept() is the run-time hook (DRAKAN_GPU_VERTEX=1): the run is
// not executed and the record goes to the renderer in the PATH1 stream, at the place of the packets the stage would
// have kicked. Anything the recogniser is not sure about runs on the CPU as before.
#include "gsr/vertex_program.h"
#include "ps2_vu1_fast.h"
#include "ps2_vu1_func.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace vu1gpu {
// Data layout of the strip stage (quadword addresses in VU data memory).
inline constexpr uint32_t kStagePc = 0x2A60;  // resume point of the strip stage
inline constexpr uint32_t kExitFlat = 0x2C48; // where the constant-colour stage stops
inline constexpr uint32_t kExitLit = 0x2F98;  // where the lit stage stops
inline constexpr uint32_t kFirstSlot = 54;    // header slot of the first strip
inline constexpr uint32_t kAux = 240;         // slot + kAux: strip length (header slot) or integer normal (vertex)
inline constexpr uint32_t kSt = 480;          // slot + kSt: texture coordinate
inline constexpr uint32_t kLastSlot = 292;    // last slot whose texture coordinate lies below the packet buffers
inline constexpr uint32_t kMaxStrip = 31;     // vertices that fit one of the two packet buffers
inline constexpr uint32_t kFlagFlat = 8;      // flag word bit: constant colour
inline constexpr uint32_t kQwTag = 11, kQwFog = 4, kQwOffset = 5, kQwScale = 6, kQwColour = 8, kQwLights = 13;

enum class Decline : uint32_t { None, NotTheStage, TagTemplate, FirstLength, StripLength, Window };

struct Counters {
    std::atomic<uint64_t> taken{0}, vertices{0}, declined{0};
};
inline Counters& counters() {
    static Counters c;
    return c;
}

// DRAKAN_GPU_VERTEX: unset or 0 = off; 1 = on; alt = one second on, one second off with a log line at each change
// (matched pictures of a paused scene against the microprogram itself).
inline int mode() {
    static const int value = [] {
        const char* e = std::getenv("DRAKAN_GPU_VERTEX");
        if (!e || !e[0] || e[0] == '0')
            return 0;
        return e[0] == 'a' ? 2 : 1;
    }();
    return value;
}
inline bool enabled() {
    return mode() != 0;
}

// Builds the wrapped record (GIF tag + payload) of the strip stage about to resume from `state` over `data` (VU data
// memory). False: not this stage, not the data layout the stage's packets assume, or a shape the stage handles in a
// way the record cannot express. exitPc is where the stage would have stopped.
inline bool buildStrips(const VU1State& state, const uint8_t* data, uint32_t dataSize, std::vector<uint8_t>& packet,
                        uint32_t& exitPc, Decline* why = nullptr) {
    using namespace gsr::vp;
    const auto decline = [why](Decline reason) {
        if (why)
            *why = reason;
        return false;
    };
    const auto qw = [data](uint32_t index) { return data + size_t(index) * 16u; };
    if (state.pc != kStagePc || dataSize < 1024u * 16u)
        return decline(Decline::NotTheStage);
    // The strip tag template must be what the stage's packet layout assumes: packed, PRIM in the tag, four registers
    // per vertex (ST, RGBAQ, FOG, XYZ2), a triangle strip.
    uint64_t tagLo, tagHi;
    std::memcpy(&tagLo, qw(kQwTag), 8);
    std::memcpy(&tagHi, qw(kQwTag) + 8, 8);
    if (((tagLo >> 58) & 3u) != 0u || ((tagLo >> 60) & 15u) != 4u || ((tagLo >> 46) & 1u) != 1u ||
        (tagHi & 0xFFFFu) != 0x5A12u || ((tagLo >> 47) & 7u) != 4u)
        return decline(Decline::TagTemplate);

    // Walk the strips: a header slot with the length, then that many vertex slots.
    uint32_t lengths[kMaxVertices];
    uint32_t stripCount = 0, vertexCount = 0, slot = kFirstSlot;
    for (;;) {
        uint16_t raw;
        std::memcpy(&raw, qw(slot + kAux), 2);
        const int32_t n = static_cast<int16_t>(raw);
        if (n <= 0) {
            if (stripCount == 0)
                return decline(Decline::FirstLength); // the stage does not test the first length
            break;
        }
        if (static_cast<uint32_t>(n) > kMaxStrip)
            return decline(Decline::StripLength);
        if (slot + static_cast<uint32_t>(n) > kLastSlot)
            return decline(Decline::Window);
        lengths[stripCount++] = static_cast<uint32_t>(n);
        vertexCount += static_cast<uint32_t>(n);
        slot += static_cast<uint32_t>(n) + 1u;
        if (slot + 1u > kLastSlot)
            return decline(Decline::Window); // the next header would lie outside the window
    }

    const bool flat = (static_cast<uint32_t>(state.vi[1]) & kFlagFlat) != 0u;
    Header header{flat ? StripsFlat : StripsLit, static_cast<uint32_t>((tagLo >> 47) & 0x7FFu), stripCount,
                  vertexCount};
    Constants k{};
    std::memcpy(k.mvp, state.vf[1], sizeof(k.mvp)); // VF01..VF04
    std::memcpy(k.scale, qw(kQwScale), 16);
    std::memcpy(k.offset, qw(kQwOffset), 16);
    std::memcpy(k.fog, qw(kQwFog), 16);
    std::memcpy(k.colour, qw(kQwColour), 16);
    std::memcpy(k.lightDir, qw(kQwLights), 64);
    std::memcpy(k.lightColour, qw(kQwLights + 4), 64);

    const uint32_t payload = recordBytes(stripCount, vertexCount);
    packet.resize(16u + payload);
    const uint64_t wrapLo = uint64_t(payload / 16u) | (1ull << 15) | (3ull << 58);
    std::memcpy(packet.data(), &wrapLo, 8);
    std::memcpy(packet.data() + 8, &kTagMagic, 8);
    uint8_t* out = packet.data() + 16;
    std::memcpy(out, &header, 16);
    out += 16;
    std::memcpy(out, &k, sizeof(k));
    out += sizeof(k);
    std::memset(out, 0, stripTableBytes(stripCount));
    std::memcpy(out, lengths, size_t(stripCount) * 4u);
    out += stripTableBytes(stripCount);
    slot = kFirstSlot;
    for (uint32_t s = 0; s < stripCount; ++s) {
        for (uint32_t i = 1; i <= lengths[s]; ++i) {
            std::memcpy(out, qw(slot + i), 16);
            std::memcpy(out + 16, qw(slot + i + kSt), 16);
            std::memcpy(out + 32, qw(slot + i + kAux), 16);
            out += sizeof(Vertex);
        }
        slot += lengths[s] + 1u;
    }
    exitPc = flat ? kExitFlat : kExitLit;
    return true;
}

// The MSCNT hook: true when the run was handed to the renderer and must not be executed.
inline bool intercept(VU1Interpreter& vu, const uint8_t* data, uint32_t dataSize, PS2Memory& memory) {
    if (vu.state().pc != kStagePc)
        return false;
    if (mode() == 2) {
        static bool on = false;
        const bool now =
            (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch())
                 .count() &
             1) != 0;
        if (now != on) {
            on = now;
            std::fprintf(stderr, "[vu1gpu] strip runs now %s\n", on ? "sent as records" : "run by the microprogram");
        }
        if (!on)
            return false;
    }
    Counters& count = counters();
    // Only while the microcode is the compiled image and no exact-engine pipeline is pending: then a run leaves
    // nothing behind but its registers, and the next MSCAL reloads every register this stage wrote.
    if (Vu1Fast::spare(vu).aotValid == 0u || !Vu1Func::enabled() || !Vu1Func::canEnter(vu)) {
        count.declined.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    thread_local std::vector<uint8_t> packet;
    uint32_t exitPc = 0;
    if (!buildStrips(vu.state(), data, dataSize, packet, exitPc)) {
        count.declined.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    memory.submitGifPacket(GifPathId::Path1, packet.data(), static_cast<uint32_t>(packet.size()));
    vu.state().pc = exitPc;
    count.taken.fetch_add(1, std::memory_order_relaxed);
    uint32_t vertices;
    std::memcpy(&vertices, packet.data() + 16 + 12, 4);
    count.vertices.fetch_add(vertices, std::memory_order_relaxed);
    return true;
}
} // namespace vu1gpu
