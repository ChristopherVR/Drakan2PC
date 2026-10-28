// Live VIF input batches with isolated computation and one ordered collector.
// A speculative result is usable only when its architectural inputs are still
// current. Optional dependency tracing validates consumed lanes/words and merges
// written state. Rejected groups execute through the original callbacks.
#include "../ps2_vu1_batch.h"
#include "../ps2_vu1_batch_capture.h"
#include "../ps2_vu1_batch_dispatch.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#include "../drakan_object_motion.h"
#include "ps2_vu1_fast.h"
#include "ps2_vu1_decode.h"
#include "aot/ps2_vu1_aot.h"
#include "aot/ps2_vu1_blocks.h"
#include "aot/ps2_vu1_image.inc"
#include "ps2_vu1_image_fingerprint.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <bit>
#include <limits>
#include <chrono>

namespace {
enum class Phase { Prepare, Queue, History, Serial, Wait, Validate, Collect, Worker, Count };
std::array<std::atomic<uint64_t>, size_t(Phase::Count)> phaseNs{};
bool profileEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_PROFILE");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
bool predicateEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_PREDICATES");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
unsigned lazyLoadsMode() {
    static const unsigned mode = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_LAZY_LOADS");
        return unsigned(std::clamp(e ? std::strtol(e, nullptr, 10) : 0l, 0l, 2l));
    }();
    return mode;
}
unsigned packetKernelMode() {
    static const unsigned mode = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_NATIVE_PACKET");
        return unsigned(std::clamp(e ? std::strtol(e, nullptr, 10) : 0l, 0l, 2l));
    }();
    return mode;
}
bool streamingEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_STREAM");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
bool reuseProfileEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_REUSE_PROFILE");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
bool staticRegsEnabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_STATIC_REGS");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
bool staticRegsVerify() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_STATIC_REGS_VERIFY");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
// On this fixed positive branch the loaded clipping count is used only by
// IBLEZ, then overwritten before any other use or program end. Nonpositive
// exits retain the value in VI7 and therefore still require the exact input.
static_assert(Vu1Fingerprint::ofRange(kVu1Image, 0x1408 / 8, 7) == 0x0667cc270fc760b6ull);
// After the packet leave branch, every path reaches E or another VF12
// overwrite before reading VF12. Check all intervening instructions and the
// branch edges. The usual MSCNT entry also overwrites it before use when
// q9.z > 0 and q7.x bit zero is clear; those controls are validated normally.
constexpr bool kPacketExitDeadLoad = [] {
    constexpr uint32_t pcs[] = {489, 490, 491, 492, 493, 494, 506, 507, 508, 509, 510, 511, 512,
                                513, 514, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527};
    for (auto pc : pcs) {
        const auto d = Vu1Decode::decodePair(uint32_t(kVu1Image[pc]), uint32_t(kVu1Image[pc] >> 32), true);
        if ((d.rdLo & (15ull << 48)) || d.upper != (pc == 513 ? 0x400002ffu : 0x2ffu))
            return false;
        if (d.lowerUsage.pipeline == Vu1Decode::PipelineBranch && pc != 493 && pc != 506 && pc != 511 && pc != 517 &&
            pc != 526)
            return false;
    }
    for (unsigned pc = 464; pc <= 494; ++pc) {
        const auto d = Vu1Decode::decodePair(uint32_t(kVu1Image[pc]), uint32_t(kVu1Image[pc] >> 32), true);
        if ((d.rdLo & (15ull << 48)) || d.upper != 0x2ffu)
            return false;
        if (d.lowerUsage.pipeline == Vu1Decode::PipelineBranch && pc != 468 && pc != 478 && pc != 493)
            return false;
    }
    return true;
}();
constexpr uint32_t kPacketExitPcs[] = {494, 493, 506, 511, 517, 526, 515, 516, 468, 478};
static_assert(kPacketExitDeadLoad && Vu1Fingerprint::ofPcs(kVu1Image, kPacketExitPcs) == 0x3cdaabfb3f91a46bull);
struct PhaseClock {
    using Clock = std::chrono::steady_clock;
    Clock::time_point last = profileEnabled() ? Clock::now() : Clock::time_point{};
    void mark(Phase phase) {
        if (profileEnabled()) {
            const auto now = Clock::now();
            phaseNs[size_t(phase)].fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count(),
                                             std::memory_order_relaxed);
            last = now;
        }
    }
};
constexpr uint32_t kEntries[] = {0u, 0x26e0u / 8u}, kRecovery[] = {127u, 425u, 502u, 609u};
const auto kLayout = Vu1Blocks::build(kVu1Image, kEntries, 2, kRecovery, 4);
const auto kBlockAt = [] {
    std::array<const Vu1Blocks::Info*, std::size(kVu1Image)> out{};
    for (uint32_t i = 0; i < kLayout.count; ++i)
        out[kLayout.b[i].first] = &kLayout.b[i];
    return out;
}();
constexpr uint32_t kPacketTracePcs[] = {489, 490, 491, 492, 493, 494, 495, 496, 498,
                                        499, 501, 502, 503, 504, 505, 506, 507};
struct PacketTraceImage {
    uint64_t words[17]{};
};
constexpr auto kPacketTraceImage = [] {
    PacketTraceImage image;
    for (unsigned i = 0; i < 17; ++i)
        image.words[i] = kVu1Image[kPacketTracePcs[i]];
    return image;
}();
constexpr auto kPacketTraceSchedule = Vu1Blocks::schedule(kPacketTraceImage.words, 0, 17, true, 4u);
// Shared compact dependency plans avoid streaming each interpreter's much
// larger decoded execution records through the cache on every observed pair.
struct TraceUsage {
    struct Write {
        uint8_t reg, lanes;
    } vfWrite;
    uint16_t viRead, viWrite;
    uint8_t accWrite, pipeline;
    bool writesClip, delaysNextBranchRead;
};
struct TracePlan {
    uint64_t rdLo, rdHi;
    uint32_t lower, upper, rdVa;
    TraceUsage lowerUsage, upperUsage;
    uint8_t fmB, fmKind, suppressedLowerVf;
    bool iBit;
};
static_assert(sizeof(TracePlan) <= 64);
const auto kTracePlan = [] {
    std::array<TracePlan, std::size(kVu1Image)> result{};
    auto usage = [](const auto& u) {
        return TraceUsage{{u.vfWrite.reg, u.vfWrite.lanes},
                          u.viRead,
                          u.viWrite,
                          u.accWrite,
                          uint8_t(u.pipeline),
                          u.writesClip,
                          u.delaysNextBranchRead};
    };
    for (size_t i = 0; i < result.size(); ++i) {
        const auto d = Vu1Decode::decodePair(uint32_t(kVu1Image[i]), uint32_t(kVu1Image[i] >> 32), true);
        result[i] = {d.rdLo,
                     d.rdHi,
                     d.lower,
                     d.upper,
                     d.rdVa,
                     usage(d.lowerUsage),
                     usage(d.upperUsage),
                     d.fmB,
                     d.fmKind,
                     d.suppressedLowerVf,
                     d.iBit};
    }
    return result;
}();
constexpr uint8_t reversed(uint8_t x) {
    return uint8_t(((x & 1) << 3) | ((x & 2) << 1) | ((x & 4) >> 1) | ((x & 8) >> 3));
}
struct PacketDependencies {
    uint64_t readLo = 0, readHi = 0, writeLo = 0, writeHi = 0;
    uint32_t readVa = 0, writeVa = 0;
    bool fmac = false;
};
constexpr PacketDependencies packetDependencies(unsigned count) {
    PacketDependencies out;
    for (unsigned i = 0; i < count; ++i) {
        const auto d = Vu1Decode::decodePair(uint32_t(kVu1Image[kPacketTracePcs[i]]),
                                             uint32_t(kVu1Image[kPacketTracePcs[i]] >> 32), true);
        out.readLo |= d.rdLo & ~out.writeLo;
        out.readHi |= d.rdHi & ~out.writeHi;
        out.readVa |= d.rdVa & ~out.writeVa;
        auto write = [&](const auto& w) {
            if (w.reg) {
                const uint64_t bits = uint64_t(reversed(w.lanes)) << ((w.reg & 15u) * 4u);
                (w.reg < 16 ? out.writeLo : out.writeHi) |= bits;
            }
        };
        write(d.upperUsage.vfWrite);
        if (!d.iBit && d.lowerUsage.vfWrite.reg != d.suppressedLowerVf)
            write(d.lowerUsage.vfWrite);
        out.writeVa |= uint32_t(reversed(d.upperUsage.accWrite)) << 16u;
        if (!d.iBit)
            out.writeVa |= d.lowerUsage.viWrite & 0xfffeu;
        out.fmac |= d.fmKind && ((d.upper >> 21) & 15u);
    }
    return out;
}
constexpr PacketDependencies kPacketDependencies[] = {packetDependencies(6), packetDependencies(8),
                                                      packetDependencies(10), packetDependencies(17)};
struct StaticRegisterPlan {
    struct Prefix {
        uint64_t lo = 0, hi = 0;
        uint32_t va = 0;
    };
    PacketDependencies deps;
    std::array<Prefix, Vu1Blocks::kMaxPairs> before{};
    uint32_t count = 0;
};
const auto kStaticRegisterPlans = [] {
    std::array<StaticRegisterPlan, Vu1Blocks::kMaxBlocks> plans{};
    for (unsigned b = 0; b < kLayout.count; ++b) {
        auto& p = plans[b];
        const auto& block = kLayout.b[b];
        p.count = block.count;
        for (unsigned i = 0; i < block.count; ++i) {
            auto& out = p.deps;
            p.before[i] = {out.writeLo, out.writeHi, out.writeVa};
            const auto& d = kTracePlan[block.first + i];
            out.readLo |= d.rdLo & ~out.writeLo;
            out.readHi |= d.rdHi & ~out.writeHi;
            out.readVa |= d.rdVa & ~out.writeVa;
            auto write = [&](const auto& w) {
                if (w.reg) {
                    const uint64_t bits = uint64_t(reversed(w.lanes)) << ((w.reg & 15u) * 4u);
                    (w.reg < 16 ? out.writeLo : out.writeHi) |= bits;
                }
            };
            write(d.upperUsage.vfWrite);
            if (!d.iBit && d.lowerUsage.vfWrite.reg != d.suppressedLowerVf)
                write(d.lowerUsage.vfWrite);
            out.writeVa |= uint32_t(reversed(d.upperUsage.accWrite)) << 16u;
            if (!d.iBit)
                out.writeVa |= d.lowerUsage.viWrite & 0xfffeu;
        }
    }
    return plans;
}();
const auto kStaticRegisterAt = [] {
    std::array<const StaticRegisterPlan*, std::size(kVu1Image)> at{};
    for (unsigned b = 0; b < kLayout.count; ++b)
        at[kLayout.b[b].first] = &kStaticRegisterPlans[b];
    return at;
}();
static_assert(Vu1Decode::decodePair(uint32_t(kVu1Image[501]), uint32_t(kVu1Image[501] >> 32), true).fmB < 4);
// This image never reads or explicitly replaces STATUS. FMAC and Q only
// accumulate its sticky bits. A worker can measure its own contribution with
// those bits initially clear, then restore the exact inherited contribution.
constexpr bool kMonotoneStatus = [] {
    for (const uint64_t pair : kVu1Image) {
        if (uint32_t(pair >> 32) & 0x80000000u)
            continue;
        const auto op = uint32_t(pair) >> 25;
        if (op >= 0x14 && op <= 0x17)
            return false;
    }
    return true;
}();
} // namespace

struct Vu1Batch {
    // Compact, relocatable VU state. Decoded instructions and their host cache
    // keys stay with each interpreter. No call-frame pointer is transferred.
    struct Checkpoint {
        static constexpr size_t a = offsetof(VU1Interpreter, m_flagMask);
        static constexpr size_t b = offsetof(VU1Interpreter, m_xgkick);
        static constexpr size_t c = offsetof(VU1Interpreter, m_vfReady);
        using Kick = VU1Interpreter::XgkickPipeline;
        static constexpr size_t k = offsetof(Kick, sourceAddress);
        VU1State state{};
        std::array<uint8_t, b - a> scheduler{};
        std::array<uint8_t, sizeof(VU1Interpreter) - c> tail{};
        std::array<uint8_t, sizeof(Kick) - k> kick{};
        std::vector<uint8_t> packet;

        explicit Checkpoint(const VU1Interpreter& v) : state(v.m_state) {
            static_assert(std::is_standard_layout_v<VU1Interpreter>);
            static_assert(std::is_trivially_copyable_v<VU1Interpreter>);
            const auto* p = reinterpret_cast<const uint8_t*>(&v);
            std::memcpy(scheduler.data(), p + a, scheduler.size());
            std::memcpy(tail.data(), p + c, tail.size());
            std::memcpy(kick.data(), reinterpret_cast<const uint8_t*>(&v.m_xgkick) + k, kick.size());
            auto clear = [&](size_t offset, size_t bytes) {
                if (offset >= a && offset + bytes <= b)
                    std::memset(scheduler.data() + offset - a, 0, bytes);
                else if (offset >= c && offset + bytes <= sizeof(v))
                    std::memset(tail.data() + offset - c, 0, bytes);
                else
                    std::abort();
            };
            clear(offsetof(VU1Interpreter, m_packetTap), sizeof(v.m_packetTap));
            clear(offsetof(VU1Interpreter, m_upperDst), sizeof(v.m_upperDst));
            clear(offsetof(VU1Interpreter, m_upperAccDst), sizeof(v.m_upperAccDst));
            clear(offsetof(VU1Interpreter, m_activeVuData), sizeof(v.m_activeVuData));
            clear(offsetof(VU1Interpreter, m_activeVuDataSize), sizeof(v.m_activeVuDataSize));
            clear(offsetof(VU1Interpreter, m_activeGs), sizeof(v.m_activeGs));
            clear(offsetof(VU1Interpreter, m_activeMemory), sizeof(v.m_activeMemory));
            // Profiling totals are not architectural inputs or portable state.
            const size_t spare = offsetof(VU1Interpreter, m_spare);
            clear(spare + offsetof(Vu1Fast::Spare, aotValid), sizeof(Vu1Fast::Spare::aotValid));
            clear(spare + offsetof(Vu1Fast::Spare, blkFast), sizeof(uint64_t) * 3);
            clear(spare + offsetof(Vu1Fast::Spare, why), sizeof(Vu1Fast::Spare::why));
            auto copyField = [&](size_t offset, const auto& value) {
                if (offset >= a && offset + sizeof(value) <= b)
                    std::memcpy(scheduler.data() + offset - a, &value, sizeof(value));
                else if (offset >= c && offset + sizeof(value) <= sizeof(v))
                    std::memcpy(tail.data() + offset - c, &value, sizeof(value));
                else
                    std::abort();
            };
            // Struct padding is not pipeline state. Independent executions can
            // leave different padding when a small entry is queued by value.
            auto entry = [&]<class T>(size_t offset, const T& e) {
                clear(offset, sizeof(T));
                if (!e.valid)
                    return;
#define FIELD(f) copyField(offset + offsetof(T, f), e.f)
                FIELD(readyCycle);
                if constexpr (requires { e.issueCycle; }) {
                    FIELD(issueCycle);
                    FIELD(mac);
                    FIELD(status);
                    FIELD(extraSticky);
                    FIELD(clip);
                    FIELD(valid);
                    FIELD(writesMac);
                    FIELD(writesStatus);
                    FIELD(writesSticky);
                    FIELD(writesClip);
                } else if constexpr (requires { e.statusDi; }) {
                    FIELD(value);
                    FIELD(statusDi);
                    FIELD(valid);
                } else if constexpr (requires { e.address; }) {
                    FIELD(address);
                    FIELD(words);
                    FIELD(laneMask);
                    FIELD(valid);
                } else {
                    FIELD(sequence);
                    FIELD(value);
                    FIELD(valid);
                    if constexpr (requires { e.reg; }) {
                        FIELD(reg);
                    }
                    if constexpr (requires { e.laneMask; }) {
                        FIELD(laneMask);
                    }
                }
#undef FIELD
            };
#define ARRAY(f)                                                                                                       \
    for (size_t i = 0; i < v.f.size(); ++i)                                                                            \
    entry(offsetof(VU1Interpreter, f) + i * sizeof(v.f[0]), v.f[i])
            ARRAY(m_flagPipeline);
            ARRAY(m_efu);
            ARRAY(m_storePipeline);
            ARRAY(m_vfWritePipeline);
            ARRAY(m_viWritePipeline);
            ARRAY(m_accWritePipeline);
#undef ARRAY
            entry(offsetof(VU1Interpreter, m_fdiv), v.m_fdiv);
            auto busy = [&](size_t offset, const Vu1Fast::Spare::Busy& e) {
                clear(offset, sizeof e);
                copyField(offset + offsetof(Vu1Fast::Spare::Busy, lo), e.lo);
                copyField(offset + offsetof(Vu1Fast::Spare::Busy, hi), e.hi);
                copyField(offset + offsetof(Vu1Fast::Spare::Busy, va), e.va);
            };
            const auto& sp = Vu1Fast::spare(v);
            // The flag FIFO retires by moving its cursor; retired slots retain
            // valid=true and old timestamps until reused. They are not inputs.
            uint32_t liveFlags = 0;
            for (uint32_t i = 0; i < sp.flagCount; ++i)
                liveFlags |= 1u << ((sp.flagHead + i) & 7u);
            for (size_t i = 0; i < v.m_flagPipeline.size(); ++i)
                if (!(liveFlags & (1u << i)))
                    clear(offsetof(VU1Interpreter, m_flagPipeline) + i * sizeof(v.m_flagPipeline[0]),
                          sizeof(v.m_flagPipeline[0]));
            busy(spare + offsetof(Vu1Fast::Spare, pending), sp.pending);
            for (size_t i = 0; i < 16; ++i)
                busy(spare + offsetof(Vu1Fast::Spare, clearAt) + i * sizeof(sp.clearAt[0]), sp.clearAt[i]);
            // Like the decoded-code cache, the adhoc decoder scratch is always
            // rebuilt before use; it is not an inherited input.
            clear(offsetof(VU1Interpreter, m_adhocDecoded), sizeof(v.m_adhocDecoded));
            clear(offsetof(VU1Interpreter, m_currentUpperInstruction), sizeof(v.m_currentUpperInstruction));
            if (!v.m_viBranchBackupValid) {
                clear(offsetof(VU1Interpreter, m_viBranchBackupValue), sizeof(v.m_viBranchBackupValue));
                clear(offsetof(VU1Interpreter, m_viBranchBackupReg), sizeof(v.m_viBranchBackupReg));
            }
            if (v.m_xgkick.active) {
                if (v.m_xgkick.copiedBytes > v.m_xgkick.packet.size())
                    std::abort();
                packet.assign(v.m_xgkick.packet.begin(), v.m_xgkick.packet.begin() + v.m_xgkick.copiedBytes);
            }
        }
        void restore(VU1Interpreter& v) const {
            // Compiled-image validity belongs to this interpreter's own code
            // cache and memory generation, rather than the transported state.
            const uint32_t aotValid = Vu1Fast::spare(v).aotValid;
            auto* p = reinterpret_cast<uint8_t*>(&v);
            v.m_state = state;
            std::memcpy(p + a, scheduler.data(), scheduler.size());
            std::memcpy(p + c, tail.data(), tail.size());
            std::memcpy(reinterpret_cast<uint8_t*>(&v.m_xgkick) + k, kick.data(), kick.size());
            std::copy(packet.begin(), packet.end(), v.m_xgkick.packet.begin());
            Vu1Fast::spare(v).aotValid = aotValid;
        }
        bool operator==(const Checkpoint& q) const {
            return !std::memcmp(&state, &q.state, sizeof state) && scheduler == q.scheduler && tail == q.tail &&
                   kick == q.kick && packet == q.packet;
        }
        uint64_t cycle() const {
            uint64_t value;
            std::memcpy(&value, tail.data() + offsetof(VU1Interpreter, m_cycle) - c, sizeof value);
            return value;
        }
        void cycle(uint64_t value) {
            std::memcpy(tail.data() + offsetof(VU1Interpreter, m_cycle) - c, &value, sizeof value);
        }
    };

    // All groups share the immutable scheduling checkpoint. Prediction only
    // changes architectural inputs and the origin clock; keep those small
    // values local rather than copying the full checkpoint at every MSCAL.
    struct Seed {
        const Checkpoint* base;
        VU1State state;
        uint64_t clock;
        explicit Seed(const Checkpoint& s) : base(&s), state(s.state), clock(s.cycle()) {}
        uint64_t cycle() const { return clock; }
        void cycle(uint64_t value) { clock = value; }
        void restore(VU1Interpreter& v) const {
            base->restore(v);
            v.m_state = state;
            v.m_cycle = clock;
        }
        bool operator==(const Checkpoint& q) const {
            constexpr size_t at = offsetof(VU1Interpreter, m_cycle) - Checkpoint::c;
            return !std::memcmp(&state, &q.state, sizeof state) && base->scheduler == q.scheduler &&
                   base->kick == q.kick && base->packet == q.packet && clock == q.cycle() &&
                   !std::memcmp(base->tail.data(), q.tail.data(), at) &&
                   !std::memcmp(base->tail.data() + at + sizeof clock, q.tail.data() + at + sizeof clock,
                                base->tail.size() - at - sizeof clock);
        }
    };

    template <class Trace>
    static bool merge(const Seed& seed, const Checkpoint& result, const VU1Interpreter& actual, VU1Interpreter& out,
                      const Trace& t, unsigned& reason, const uint8_t* input) {
        reason = 2;
        if (!t.supported)
            return false;
        reason = 3;
        if (!t.startsReset)
            return false;
        reason = 4;
        if (!t.complete)
            return false;
        reason = 5;
        if (!t.idle)
            return false;
        const auto& a = actual.m_state;
        const auto& s = seed.state;
        reason = 6;
        if (a.vi[0] || s.vi[0] || std::memcmp(a.vf[0], s.vf[0], sizeof a.vf[0]))
            return false;
        auto equal = [](const auto& x, const auto& y) { return !std::memcmp(&x, &y, sizeof x); };
        reason = 7;
        for (unsigned r = 0; r < 32; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                const auto bits = r < 16 ? t.liveLo : t.liveHi;
                if ((bits >> (4 * (r & 15) + c) & 1) && !equal(a.vf[r][c], s.vf[r][c]))
                    return false;
            }
        reason = 8;
        for (unsigned r = 0; r < 16; ++r)
            if ((t.liveVa >> r & 1) && a.vi[r] != s.vi[r])
                return false;
        reason = 9;
        for (unsigned c = 0; c < 4; ++c)
            if ((t.liveVa >> (16 + c) & 1) && !equal(a.acc[c], s.acc[c]))
                return false;
        reason = 10;
        if ((t.readQ && !equal(a.q, s.q)) || (t.readP && !equal(a.p, s.p)) || (t.readI && !equal(a.i, s.i)))
            return false;
        reason = 11;
        if ((a.clip ^ s.clip) & t.clipInputMask)
            return false;
        reason = 12;
        if (t.needMac && a.mac != s.mac)
            return false;
        reason = 13;
        if (t.statusWritten && !t.sanitizedStatus && (a.status & ~15u) != (s.status & ~15u))
            return false;
        result.restore(out);
        auto& z = out.m_state;
        for (unsigned r = 0; r < 32; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                const auto bits = r < 16 ? t.writtenLo : t.writtenHi;
                if (!(bits >> (4 * (r & 15) + c) & 1))
                    z.vf[r][c] = a.vf[r][c];
            }
        for (unsigned half = 0; half < 2; ++half) {
            uint64_t aliases = half ? t.aliasHi : t.aliasLo;
            while (aliases) {
                const unsigned lane = std::countr_zero(aliases) + half * 64u;
                const unsigned word = t.aliasWords[lane] - 1u;
                std::memcpy(&z.vf[lane / 4][lane & 3], input + word * 4u, 4);
                aliases &= aliases - 1;
            }
        }
        for (unsigned r = 0; r < 16; ++r)
            if (!(t.writtenVa >> r & 1))
                z.vi[r] = a.vi[r];
        for (unsigned c = 0; c < 4; ++c)
            if (!(t.writtenVa >> (16 + c) & 1))
                z.acc[c] = a.acc[c];
        if (!t.qWritten)
            z.q = a.q;
        if (!t.pWritten)
            z.p = a.p;
        if (!t.iWritten)
            z.i = a.i;
        z.r = a.r;
        if (!t.fmacWritten)
            z.mac = a.mac;
        if (!t.statusWritten)
            z.status = a.status;
        else {
            if (!t.fmacWritten)
                z.status = (z.status & ~15u) | (a.status & 15u);
            if (t.sanitizedStatus) {
                if (!t.qWritten)
                    z.status = (z.status & ~0x30u) | (a.status & 0x30u);
                z.status = (z.status & ~0xfc0u) | (a.status & 0xfc0u) | t.generatedSticky;
            }
        }
        if (!t.clipWritten) {
            z.clip = a.clip;
            out.m_workingClip = a.clip;
        } else {
            auto patchClip = [&](uint32_t value, unsigned shift) {
                const uint32_t mask = shift >= 24 ? 0u : (0xffffffu << shift) & 0xffffffu;
                return (value & ~mask) | ((a.clip << shift) & mask);
            };
            z.clip = patchClip(z.clip, t.clipCommittedShift);
            out.m_workingClip = patchClip(out.m_workingClip, t.clipWorkingShift);
        }
        if (out.m_viBranchBackupValid && t.backupBase)
            out.m_viBranchBackupValue = a.vi[out.m_viBranchBackupReg];
        // Every group starts with MSCAL, which resets scheduling but preserves
        // architectural registers. Translate its absolute timeline to the
        // collector's clock without changing instruction or write sequences.
        const uint64_t delta = actual.m_cycle - seed.cycle();
        out.m_cycle += delta;
        z.cycles += delta;
        auto rebase = [&](uint64_t& cycle) {
            if (cycle && cycle != ~0ull)
                cycle += delta;
        };
        for (auto& r : out.m_vfReady)
            for (auto& c : r)
                rebase(c);
        for (auto& c : out.m_viReady)
            rebase(c);
        for (auto& c : out.m_accReady)
            rebase(c);
        rebase(out.m_minReady);
        rebase(out.m_maxRegReady);
        rebase(out.m_efuResourceReady);
        auto& sp = Vu1Fast::spare(out);
        sp.runBase += delta;
        rebase(sp.storeReady);
        rebase(sp.xgLast);
        // Busy retirement is indexed by the absolute cycle modulo 16.
        const auto old = sp;
        for (unsigned i = 0; i < 16; ++i)
            sp.clearAt[(i + delta) & 15u] = old.clearAt[i];
        if (out.m_xgkick.issueCycle)
            out.m_xgkick.issueCycle += delta;
        return true;
    }

    template <bool StaticRegisters, class Trace>
    static bool dispatch(VU1Interpreter& v, Vu1Step::Ctx& c, Trace& trace) {
        const uint32_t pc = v.m_state.pc / 8u;
        const unsigned lazyMode = lazyLoadsMode();
        const Vu1Blocks::Info* block = nullptr;
        if (Vu1Aot::blocksEnabled() && pc != 489u) {
            block = kBlockAt[pc];
            if (block && block->recovery && !Vu1Aot::recoveryEnabled())
                block = nullptr;
        }
        bool enter = block && Vu1Step::blockCanEnter(v, c, block->cycles, block->qSafe, block->eLo, block->eHi,
                                                     block->eVa, block->rLo, block->rHi, block->rVa);
        // The UV kernel has a fixed seven-pair backedge: VI11 counts down,
        // VI12 advances by one, and no float result controls its path. Observe
        // the same bounded iterations while retaining its fused arithmetic.
        static const unsigned uvLimit = [] {
            const char* e = std::getenv("DRAKAN_VU_NATIVE_LOOP_LIMIT");
            return unsigned(std::clamp(e ? std::strtol(e, nullptr, 10) : 32l, 1l, 256l));
        }();
        bool nativePacket = false;
        if (pc == 489u && packetKernelMode() && Vu1Aot::g_dispatch[pc] == &Vu1Step::packetKernel &&
            int16_t(v.m_state.vi[1]) >= 0 && int16_t(v.m_state.vi[1]) <= 8 && Vu1Step::traceScalarsIdle(v)) {
            const auto& p = kPacketTraceSchedule;
            if (Vu1Step::blockCanEnter(v, c, p.cycles, false, p.eLo, p.eHi, p.eVa, p.rLo, p.rHi, p.rVa)) {
                block = &p;
                enter = true;
                nativePacket = true;
            }
        }
        const bool nativeUv = enter && pc == 155u && block->count == 7u && uvLimit <= 32u &&
                              Vu1Aot::g_dispatch[pc] == &Vu1Step::uvKernel && Vu1Step::traceScalarsIdle(v);
        const bool nativeTransform = enter && pc == 334u && block->count == 7u && uvLimit <= 32u &&
                                     Vu1Aot::g_dispatch[pc] == &Vu1Step::transformKernel &&
                                     Vu1Step::traceScalarsIdle(v);
        unsigned iterations = 1;
        if (nativeUv || nativeTransform) {
            iterations =
                std::min(uvLimit, unsigned(std::max(1, int(int16_t(v.m_state.vi[nativeTransform ? 10 : 11])))));
            iterations =
                std::min(iterations, unsigned(std::min<uint64_t>(32u, (c.budgetEnd - v.m_cycle - 1u) / block->cycles)));
            iterations = std::max(1u, iterations);
        }
        uint32_t packetIndices[4];
        unsigned packetPairs = 0;
        if (nativePacket) {
            std::memcpy(packetIndices, v.m_state.vf[19], sizeof packetIndices);
            unsigned limit = std::min(uvLimit, unsigned(std::max(1, int(int16_t(v.m_state.vi[1])))));
            limit = std::min(limit, unsigned(std::min<uint64_t>(8u, (c.budgetEnd - v.m_cycle - 1u) / block->cycles)));
            limit = std::max(1u, limit);
            for (unsigned n = 0; n < limit; ++n) {
                const unsigned prefix = int16_t(packetIndices[n & 3u]) <= 0 ? 6u
                                        : int16_t(v.m_state.vi[3]) <= 0     ? 8u
                                        : int16_t(v.m_state.vi[4]) <= 0     ? 10u
                                                                            : 17u;
                packetPairs += prefix;
                if (prefix != 17)
                    break;
            }
        }
        const uint32_t width = enter ? block->count : 1u, count = nativePacket ? packetPairs : width * iterations;
        const auto* regPlan = kStaticRegisterAt[pc];
        const bool bulkRegs = StaticRegisters && enter && !nativePacket && lazyMode != 1 && !trace.aliasLo &&
                              !trace.aliasHi && regPlan && regPlan->count == width &&
                              !(pc <= 494u && pc + width > 494u);
        const bool checkRegs = bulkRegs && staticRegsVerify();
        bool badPrefix = false;
        struct RegisterShadow {
            uint64_t liveLo = 0, liveHi = 0, writtenLo = 0, writtenHi = 0;
            uint32_t liveVa = 0, writtenVa = 0;
        } shadow;
        const uint32_t beforeWrittenVa = trace.writtenVa;
        if (checkRegs)
            shadow = {trace.liveLo, trace.liveHi, trace.writtenLo, trace.writtenHi, trace.liveVa, trace.writtenVa};
        if (bulkRegs) {
            const auto& d = regPlan->deps;
            trace.liveLo |= d.readLo & ~(trace.writtenLo | 15ull);
            trace.liveHi |= d.readHi & ~trace.writtenHi;
            trace.liveVa |= d.readVa & ~(trace.writtenVa | 1u);
            trace.writtenLo |= d.writeLo;
            trace.writtenHi |= d.writeHi;
            trace.writtenVa |= d.writeVa;
            ++trace.staticRegBlocks;
            trace.staticRegPairs += count;
        }
        int32_t vi[16];
        std::memcpy(vi, v.m_state.vi, sizeof vi);
        bool known[16];
        std::fill(std::begin(known), std::end(known), true);
        if constexpr (!StaticRegisters)
            trace.blockWrites.fill(0);
        uint64_t dirtyWriteChunks = 0;
        struct Store {
            uint32_t address;
            uint8_t lanes;
        };
        Store stores[32]{};
        unsigned nstores = 0;
        uint64_t writtenLo = 0, writtenHi = 0;
        if (nativeUv)
            trace.nativeUvPairs += count;
        if (nativeTransform)
            trace.nativeTransformPairs += count;
        if (nativePacket)
            trace.nativePacketPairs += count;
        bool singleMac = false;
        bool singleClip = false, singleClipSet = false;
        bool backupValid = v.m_viBranchBackupValid;
        unsigned backupReg = v.m_viBranchBackupReg;
        auto forwardedLanes = [&](uint32_t address) {
            // VU vector/integer memory operands use aligned qword addresses,
            // so their four lane bits always share one provenance-mask word.
            if constexpr (StaticRegisters) {
                const auto lanes = uint8_t((trace.blockWrites[address / 256u] >> ((address / 4u) & 63u)) & 15u);
                if (checkRegs && trace.supported) {
                    uint8_t expected = 0;
                    for (unsigned i = 0; i < nstores; ++i)
                        if (stores[i].address == address)
                            expected |= stores[i].lanes;
                    if (lanes != expected)
                        badPrefix = true;
                }
                return lanes;
            } else {
                uint8_t lanes = 0;
                for (unsigned i = 0; i < nstores; ++i)
                    if (stores[i].address == address)
                        lanes |= stores[i].lanes;
                return lanes;
            }
        };
        auto read = [&](uint32_t address, uint8_t lanes) { trace.read(address, lanes & ~forwardedLanes(address)); };
        auto store = [&](uint32_t address, uint8_t lanes) {
            if (nstores < 32) {
                if (!StaticRegisters || checkRegs)
                    stores[nstores] = {address, lanes};
                ++nstores;
            } else
                trace.supported = false;
            for (unsigned c = 0; c < 4; ++c)
                if (lanes & (1u << c)) {
                    const unsigned w = (address / 4u + c) & 4095u;
                    trace.blockWrites[w / 64u] |= 1ull << (w & 63u);
                    if constexpr (StaticRegisters)
                        dirtyWriteChunks |= 1ull << (w / 64u);
                }
        };
        auto address = [&](unsigned reg, int32_t offset) {
            if (!known[reg])
                trace.supported = false;
            return (uint32_t(vi[reg] + offset) * 16u) & 16383u;
        };
        auto loadInt = [&](unsigned dest, uint32_t at, unsigned comp, bool positiveOnly = false) {
            const bool forwarded = forwardedLanes(at) & (1u << comp);
            uint32_t value;
            std::memcpy(&value, c.vuData + at + comp * 4u, 4);
            if (!forwarded) {
                if (positiveOnly && at == 65u * 16u && comp == 0 && dest == 7 && int16_t(value) > 0 &&
                    !(trace.writtenMemory[4] & (1ull << 4)))
                    trace.positiveClipGate = true;
                else
                    trace.readLow(at + comp * 4u);
            }
            if (dest) {
                vi[dest] = int16_t(value);
                known[dest] = !forwarded;
            }
        };
        auto writeVi = [&](unsigned reg, int32_t value, bool valid = true) {
            if (reg) {
                vi[reg] = int16_t(value);
                known[reg] = valid;
            }
        };
        if (nativePacket && packetKernelMode() == 2 && lazyMode != 1) {
            // Observe one bounded vertex iteration rather than replaying its
            // seventeen dependency instructions. All branches, addresses and
            // lanes follow the same checked packet kernel schedule.
            ++trace.nativePacketBulk;
            int32_t destination = vi[9];
            auto at = [](int32_t qword) { return (uint32_t(qword) * 16u) & 16383u; };
            for (unsigned done = 0, n = 0; done < count; ++n) {
                const int32_t index = int16_t(packetIndices[n & 3u]);
                const unsigned prefix = index <= 0 ? 6u : int16_t(vi[3]) <= 0 ? 8u : int16_t(vi[4]) <= 0 ? 10u : 17u;
                const auto& d = kPacketDependencies[prefix == 6 ? 0 : prefix == 8 ? 1 : prefix == 10 ? 2 : 3];
                trace.liveLo |= d.readLo & ~(trace.writtenLo | 15ull);
                trace.liveHi |= d.readHi & ~trace.writtenHi;
                trace.liveVa |= d.readVa & ~(trace.writtenVa | 1u);
                trace.writtenLo |= d.writeLo;
                trace.writtenHi |= d.writeHi;
                trace.writtenVa |= d.writeVa;
                trace.aliasLo &= ~(15ull << 48);
                if (lazyMode == 2 && prefix == 6) {
                    const uint32_t address = at(index + 65);
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        const bool forwarded = forwardedLanes(address) & (1u << lane);
                        const unsigned word = (address / 4u + lane) & 4095u;
                        if (!forwarded && !(trace.writtenMemory[word / 64] >> (word & 63) & 1u)) {
                            trace.aliasWords[48 + lane] = uint16_t(word + 1u);
                            trace.aliasLo |= 1ull << (48 + lane);
                            ++trace.deferredLoads;
                        }
                    }
                } else
                    read(at(index + 65), 15);
                if (prefix >= 8)
                    read(at(index + 515), 7);
                if (prefix >= 10)
                    read(at(index + 290), 15);
                if (prefix == 17) {
                    store(at(destination + 3), 15);
                    store(at(destination + 2), 8);
                    store(at(destination + 1), 15);
                    store(at(destination), 7);
                    destination = int16_t(destination + 4);
                    trace.fmacWritten = trace.statusWritten = true;
                    trace.firstMacReady =
                        std::min(trace.firstMacReady, v.m_cycle + uint64_t(n) * block->cycles + block->T[10] + 4u);
                }
                done += prefix;
            }
            // Any surviving VI9 backup came from a destination address whose
            // input was validated above; prefix exits clear the backup.
            trace.backupBase = false;
        } else
            for (unsigned i = 0; i < count; ++i) {
                const unsigned slot = i % width, instructionPc = nativePacket ? kPacketTracePcs[slot] : pc + slot;
                const auto& d = kTracePlan[instructionPc];
                // Empty scheduled slots still clear the one-pair VI branch backup,
                // but need no operand, flag or memory dependency bookkeeping.
                if (d.upper == 0x000002ffu && (d.lower == 0x8000033cu || d.lower == 0u)) {
                    backupValid = false;
                    continue;
                }
                const uint64_t issue = v.m_cycle + (enter ? uint64_t(i / width) * block->cycles + block->T[slot] : 0u);
                trace.advanceClip(issue);
                if (v.m_fdiv.valid && v.m_fdiv.readyCycle <= issue)
                    trace.qAssigned = true;
                for (const auto& p : v.m_efu)
                    if (p.valid && p.readyCycle <= issue)
                        trace.pAssigned = true;
                if (d.iBit) {
                    trace.iAssigned = true;
                    trace.iWritten = true;
                }
                if (d.fmB == 4 && d.fmKind && !trace.qAssigned)
                    trace.readQ = true;
                const uint32_t upperOp = d.upper & 63u;
                if (((d.fmB == 5 && d.fmKind) || upperOp == 0x1d || upperOp == 0x1f) && !trace.iAssigned)
                    trace.readI = true;
                if (lazyMode == 1)
                    trace.consumeAliases(d.rdLo, d.rdHi);
                if (!bulkRegs) {
                    trace.liveLo |= d.rdLo & ~(trace.writtenLo | 15ull);
                    trace.liveHi |= d.rdHi & ~trace.writtenHi;
                    trace.liveVa |= d.rdVa & ~(trace.writtenVa | 1u);
                } else if (checkRegs) {
                    shadow.liveLo |= d.rdLo & ~(shadow.writtenLo | 15ull);
                    shadow.liveHi |= d.rdHi & ~shadow.writtenHi;
                    shadow.liveVa |= d.rdVa & ~(shadow.writtenVa | 1u);
                }
                if (d.lowerUsage.pipeline == VU1Interpreter::PipelineBranch && backupValid && trace.backupBase &&
                    (d.lowerUsage.viRead & (1u << backupReg))) {
                    trace.liveVa |= 1u << backupReg;
                    if (checkRegs)
                        shadow.liveVa |= 1u << backupReg;
                }
                const uint32_t oldWritten =
                    bulkRegs ? (beforeWrittenVa | regPlan->before[slot].va | (i >= width ? regPlan->deps.writeVa : 0u))
                             : trace.writtenVa;
                if (checkRegs && oldWritten != shadow.writtenVa)
                    badPrefix = true;
                const uint32_t lower = d.lower, op = lower >> 25, fun = lower & 63u,
                               special = (lower & 3u) | ((lower >> 4) & 124u);
                const unsigned it = (lower >> 16) & 15u, is = (lower >> 11) & 15u, id = (lower >> 6) & 15u,
                               ft = (lower >> 16) & 31u, fs = (lower >> 11) & 31u;
                const uint8_t lanes = reversed(uint8_t((lower >> 21) & 15u));
                const int32_t imm = (int32_t(lower << 21)) >> 21;
                if (d.fmKind && ((d.upper >> 21) & 15u)) {
                    trace.fmacWritten = true;
                    trace.statusWritten = true;
                }
                if (((d.upper >> 21) & 15u) && (upperOp == 0x2e || (!d.fmKind && d.upperUsage.accWrite))) {
                    trace.fmacWritten = true;
                    trace.statusWritten = true;
                }
                if ((d.fmKind || upperOp == 0x2e || d.upperUsage.accWrite) && ((d.upper >> 21) & 15u)) {
                    if (enter)
                        trace.firstMacReady = std::min(trace.firstMacReady, issue + 4u);
                    else
                        singleMac = true;
                }
                if (d.upperUsage.writesClip) {
                    trace.clipWritten = true;
                    if (enter)
                        trace.queueClip(issue, false);
                    else
                        singleClip = true;
                }
                if (!d.iBit && d.lowerUsage.writesClip) {
                    trace.clipWritten = true;
                    if (enter)
                        trace.queueClip(issue, true);
                    else
                        singleClipSet = true;
                }
                uint64_t loadedAliases = 0;
                unsigned aliasReg = 0;
                auto loadVf = [&](uint32_t at) {
                    const bool certified = lazyMode == 2 && instructionPc == 494u && ft == 12u && lanes == 15u &&
                                           ((nativePacket && int16_t(packetIndices[(i / width) & 3u]) <= 0) ||
                                            (v.m_state.branchPending && v.m_state.branchDelay == 0 &&
                                             v.m_state.branchTarget == 517u * 8u));
                    if (!lazyMode || (lazyMode == 2 && !certified)) {
                        read(at, lanes);
                        return;
                    }
                    if (!ft || d.suppressedLowerVf == ft)
                        return;
                    aliasReg = ft;
                    for (unsigned lane = 0; lane < 4; ++lane)
                        if (lanes & (1u << lane)) {
                            const bool forwarded = forwardedLanes(at) & (1u << lane);
                            const unsigned word = (at / 4u + lane) & 4095u;
                            if (!forwarded && !(trace.writtenMemory[word / 64] >> (word & 63) & 1u))
                                loadedAliases |= uint64_t(word + 1u) << (lane * 16u);
                        }
                };
                if (!d.iBit && lower != 0 && lower != 0x8000033cu)
                    switch (op) {
                        case 0x00:
                            loadVf(address(is, imm));
                            break;
                        case 0x01:
                            store(address(it, imm), lanes);
                            break;
                        case 0x04:
                            loadInt(it, address(is, imm), std::countr_zero(unsigned(lanes ? lanes : 8u)),
                                    instructionPc == 0x1408u / 8u && predicateEnabled());
                            break;
                        case 0x05:
                            store(address(is, imm), lanes);
                            break;
                        case 0x08:
                            writeVi(it, vi[is] + int32_t((lower & 0x7ffu) | ((lower >> 10) & 0x7800u)), known[is]);
                            break;
                        case 0x09:
                            writeVi(it, vi[is] - int32_t((lower & 0x7ffu) | ((lower >> 10) & 0x7800u)), known[is]);
                            break;
                        case 0x10:
                        case 0x12:
                        case 0x13:
                        case 0x1c:
                            trace.requireClip(op == 0x1c   ? 0xfffu
                                              : op == 0x12 ? lower & 0xffffffu
                                              : op == 0x13 ? (~lower) & 0xffffffu
                                                           : 0xffffffu);
                            if (op == 0x13 && trace.clipCommittedShift == 0)
                                trace.clipInputMask |= 0xff000000u;
                            for (unsigned x = 1; x < 16; ++x)
                                if (d.lowerUsage.viWrite & (1u << x))
                                    known[x] = false;
                            break;
                        case 0x11:
                            break;
                        case 0x14:
                        case 0x15:
                        case 0x16:
                        case 0x17:
                            trace.supported = false;
                            break;
                        case 0x18:
                        case 0x1a:
                        case 0x1b:
                            if (issue < trace.firstMacReady)
                                trace.needMac = true;
                            if (it)
                                known[it] = false;
                            break;
                        case 0x21:
                        case 0x25:
                            writeVi(it, int32_t(instructionPc + 2u));
                            break;
                        case 0x20:
                        case 0x24:
                        case 0x28:
                        case 0x29:
                        case 0x2c:
                        case 0x2d:
                        case 0x2e:
                        case 0x2f:
                            break;
                        case 0x40:
                            switch (fun) {
                                case 0x30:
                                    writeVi(id, vi[is] + vi[it], known[is] && known[it]);
                                    break;
                                case 0x31:
                                    writeVi(id, vi[is] - vi[it], known[is] && known[it]);
                                    break;
                                case 0x32:
                                    writeVi(it, vi[is] + (int32_t(lower << 21) >> 27), known[is]);
                                    break;
                                case 0x34:
                                    writeVi(id, vi[is] & vi[it], known[is] && known[it]);
                                    break;
                                case 0x35:
                                    writeVi(id, vi[is] | vi[it], known[is] && known[it]);
                                    break;
                                case 0x3c:
                                case 0x3d:
                                case 0x3e:
                                case 0x3f:
                                    switch (special) {
                                        case 0x30:
                                        case 0x31:
                                        case 0x3d:
                                        case 0x38:
                                        case 0x39:
                                        case 0x3a:
                                        case 0x3b:
                                        case 0x6c:
                                        case 0x7b:
                                            break;
                                        case 0x34:
                                            loadVf(address(is, 0));
                                            writeVi(is, vi[is] + 1, known[is]);
                                            break;
                                        case 0x35:
                                            store(address(it, 0), lanes);
                                            writeVi(it, vi[it] + 1, known[it]);
                                            break;
                                        case 0x36:
                                            writeVi(is, vi[is] - 1, known[is]);
                                            loadVf(address(is, 0));
                                            break;
                                        case 0x37:
                                            writeVi(it, vi[it] - 1, known[it]);
                                            store(address(it, 0), lanes);
                                            break;
                                        case 0x3c: {
                                            const unsigned comp = (lower >> 21) & 3u;
                                            const uint64_t bit = 1ull << ((fs & 15u) * 4u + comp);
                                            const auto localLo = bulkRegs
                                                                     ? (regPlan->before[slot].lo |
                                                                        (i >= width ? regPlan->deps.writeLo : 0ull))
                                                                     : writtenLo;
                                            const auto localHi = bulkRegs
                                                                     ? (regPlan->before[slot].hi |
                                                                        (i >= width ? regPlan->deps.writeHi : 0ull))
                                                                     : writtenHi;
                                            const bool ready = ((fs < 16 ? localLo : localHi) & bit) == 0;
                                            if (checkRegs && ready != (((fs < 16 ? writtenLo : writtenHi) & bit) == 0))
                                                badPrefix = true;
                                            uint32_t value;
                                            if (nativePacket && instructionPc == 489u)
                                                value = packetIndices[(i / width) & 3u];
                                            else
                                                std::memcpy(&value, &v.m_state.vf[fs][comp], 4);
                                            writeVi(it, int16_t(value),
                                                    nativePacket && instructionPc == 489u ? true : ready);
                                            break;
                                        }
                                        case 0x3e:
                                            loadInt(it, address(is, 0), std::countr_zero(unsigned(lanes ? lanes : 8u)));
                                            break;
                                        case 0x3f:
                                            store(address(is, 0), lanes);
                                            break;
                                        case 0x64:
                                            if (!trace.pAssigned)
                                                trace.readP = true;
                                            break;
                                        case 0x68:
                                            writeVi(it, int32_t(v.m_state.top));
                                            break;
                                        case 0x69:
                                            writeVi(it, int32_t(v.m_state.itop));
                                            break;
                                        default:
                                            // EFU operations only produce P. Random-register and
                                            // unknown integer/memory operations stay on the strict path.
                                            if (d.lowerUsage.pipeline != VU1Interpreter::PipelineEfu)
                                                trace.supported = false;
                                            break;
                                    }
                                    break;
                                default:
                                    trace.supported = false;
                                    break;
                            }
                            break;
                        default:
                            trace.supported = false;
                            break;
                    }
                // Record effective architectural writes, excluding the lower VF
                // write suppressed by a same-pair upper destination.
                if (!bulkRegs || checkRegs) {
                    auto vfWrite = [&](const auto& w) {
                        if (w.reg) {
                            const uint64_t bits = uint64_t(reversed(w.lanes)) << ((w.reg & 15u) * 4u);
                            if (bulkRegs) {
                                (w.reg < 16 ? shadow.writtenLo : shadow.writtenHi) |= bits;
                                (w.reg < 16 ? writtenLo : writtenHi) |= bits;
                                return;
                            }
                            if (w.reg < 16) {
                                if (lazyMode == 1 || (lazyMode == 2 && w.reg == 12))
                                    trace.aliasLo &= ~bits;
                                trace.writtenLo |= bits;
                                writtenLo |= bits;
                            } else {
                                if (lazyMode == 1)
                                    trace.aliasHi &= ~bits;
                                trace.writtenHi |= bits;
                                writtenHi |= bits;
                            }
                        }
                    };
                    vfWrite(d.upperUsage.vfWrite);
                    if (!d.iBit && d.lowerUsage.vfWrite.reg != d.suppressedLowerVf)
                        vfWrite(d.lowerUsage.vfWrite);
                }
                if (aliasReg)
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        const uint16_t word = uint16_t(loadedAliases >> (lane * 16u));
                        if (word) {
                            const unsigned index = aliasReg * 4u + lane;
                            trace.aliasWords[index] = word;
                            (index < 64 ? trace.aliasLo : trace.aliasHi) |= 1ull << (index & 63u);
                            ++trace.deferredLoads;
                        }
                    }
                if (!bulkRegs) {
                    trace.writtenVa |= uint32_t(reversed(d.upperUsage.accWrite)) << 16u;
                    if (!d.iBit)
                        trace.writtenVa |= d.lowerUsage.viWrite & 0xfffeu;
                } else if (checkRegs) {
                    shadow.writtenVa |= uint32_t(reversed(d.upperUsage.accWrite)) << 16u;
                    if (!d.iBit)
                        shadow.writtenVa |= d.lowerUsage.viWrite & 0xfffeu;
                }
                backupValid = !d.iBit && d.lowerUsage.delaysNextBranchRead && (d.lowerUsage.viWrite & 0xfffeu);
                if (backupValid) {
                    backupReg = std::countr_zero(unsigned(d.lowerUsage.viWrite & 0xfffeu));
                    trace.backupBase = (oldWritten & (1u << backupReg)) == 0;
                }
            }
        if (checkRegs) {
            ++trace.staticRegChecks;
            if (badPrefix || trace.liveLo != shadow.liveLo || trace.liveHi != shadow.liveHi ||
                trace.liveVa != shadow.liveVa || trace.writtenLo != shadow.writtenLo ||
                trace.writtenHi != shadow.writtenHi || trace.writtenVa != shadow.writtenVa) {
                ++trace.staticRegBad;
                trace.supported = false;
                std::fprintf(stderr, "[vu-static-regs] MISMATCH pc=%u count=%u\n", pc, count);
            }
        }
        const uint64_t before = c.pairs;
        const auto fn = nativePacket                    ? Vu1Aot::g_dispatch[pc]
                        : (nativeUv || nativeTransform) ? Vu1Aot::g_dispatch[pc]
                        : pc == 489u                    ? Vu1Aot::g_table[pc]
                                                        : Vu1Aot::g_blockDispatch[pc];
        const bool result = fn(v, c);
        if (reuseProfileEnabled()) {
            trace.pathHash = (trace.pathHash ^ pc) * 1099511628211ull;
            trace.pathHash = (trace.pathHash ^ (c.pairs - before)) * 1099511628211ull;
            trace.pathHash = (trace.pathHash ^ v.m_state.pc) * 1099511628211ull;
        }
        if (c.pairs - before != count)
            trace.supported = false;
        // An exact pair can stall before issue. Its retirement stamp must use
        // the actual post-step clock, rather than the pre-stall prediction.
        if (singleMac && c.pairs - before == 1)
            trace.firstMacReady = std::min(trace.firstMacReady, v.m_cycle + 3u);
        if (c.pairs - before == 1) {
            if (singleClip)
                trace.queueClip(v.m_cycle - 1u, false);
            if (singleClipSet)
                trace.queueClip(v.m_cycle - 1u, true);
        }
        // Transfer callbacks use this block's stores while it executes. Fresh
        // traces start empty; completed dispatches clear every touched chunk.
        if constexpr (StaticRegisters) {
            while (dirtyWriteChunks) {
                const unsigned chunk = std::countr_zero(dirtyWriteChunks);
                trace.writtenMemory[chunk] |= trace.blockWrites[chunk];
                trace.blockWrites[chunk] = 0;
                dirtyWriteChunks &= dirtyWriteChunks - 1;
            }
        } else {
            for (unsigned i = 0; i < nstores; ++i)
                trace.write(stores[i].address, stores[i].lanes);
            trace.blockWrites.fill(0);
        }
        return result;
    }
};

namespace vu1batch {
thread_local bool captureActive = false;
namespace {
using Data = std::array<uint8_t, PS2_VU1_DATA_SIZE>;
using WordMask = std::array<uint64_t, 64>;
struct Trace {
    uint64_t pathHash = 1469598103934665603ull, controlHash = 1469598103934665603ull;
    WordMask liveMemory{}, lowMemory{}, writtenMemory{}, blockWrites{};
    std::unique_ptr<WordMask> packetInputs;
    std::vector<std::pair<uint32_t, uint32_t>> packetWords, tagInputs;
    uint64_t liveLo = 0, liveHi = 0, writtenLo = 0, writtenHi = 0;
    uint32_t liveVa = 0, writtenVa = 0;
    uint64_t nativeUvPairs = 0, nativeTransformPairs = 0, nativePacketPairs = 0, nativePacketBulk = 0,
             deferredLoads = 0;
    uint64_t staticRegBlocks = 0, staticRegPairs = 0, staticRegChecks = 0, staticRegBad = 0;
    uint64_t aliasLo = 0, aliasHi = 0;
    std::array<uint16_t, 128> aliasWords{};
    uint64_t firstMacReady = ~0ull;
    uint32_t generatedSticky = 0;
    uint32_t clipInputMask = 0;
    unsigned clipWorkingShift = 0, clipCommittedShift = 0, clipHead = 0, clipCount = 0;
    struct ClipStamp {
        uint64_t ready = 0;
        unsigned shift = 0;
    };
    std::array<ClipStamp, 8> clips{};
    bool positiveClipGate = false;
    bool sanitizedStatus = false, statusSafe = true;
    bool readQ = false, readP = false, readI = false, qAssigned = false, pAssigned = false, iAssigned = false;
    bool qWritten = false, pWritten = false, iWritten = false, fmacWritten = false, statusWritten = false,
         clipWritten = false;
    bool needMac = false, backupBase = false, supported = true, complete = true, idle = true, startsReset = false,
         seenRun = false;
    void consumeAliases(uint64_t lo, uint64_t hi) {
        if (!((aliasLo & lo) | (aliasHi & hi)))
            return;
        for (unsigned half = 0; half < 2; ++half) {
            auto& aliases = half ? aliasHi : aliasLo;
            uint64_t reads = aliases & (half ? hi : lo);
            aliases &= ~reads;
            while (reads) {
                const unsigned lane = std::countr_zero(reads) + half * 64u, word = aliasWords[lane] - 1u;
                liveMemory[word / 64u] |= 1ull << (word & 63u);
                reads &= reads - 1;
            }
        }
    }
    void read(uint32_t address, uint8_t lanes) {
        for (unsigned c = 0; c < 4; ++c)
            if (lanes & (1u << c)) {
                const auto word = ((address & 16383u) / 4u + c) & 4095u;
                liveMemory[word / 64u] |= (1ull << (word & 63u)) & ~writtenMemory[word / 64u];
            }
    }
    void write(uint32_t address, uint8_t lanes) {
        for (unsigned c = 0; c < 4; ++c)
            if (lanes & (1u << c)) {
                const auto word = ((address & 16383u) / 4u + c) & 4095u;
                writtenMemory[word / 64u] |= 1ull << (word & 63u);
            }
    }
    void readLow(uint32_t address) {
        const auto w = (address & 16383u) / 4u;
        lowMemory[w / 64u] |= (1ull << (w & 63u)) & ~writtenMemory[w / 64u];
    }
    // CLIP shifts six new bits into a 24-bit history. Carry the provenance of
    // pending writes, so a query depends only on surviving original bits. FCSET
    // replaces the working history; a complete, idle result can repair any
    // surviving unqueried input bits without rerunning vector arithmetic.
    void advanceClip(uint64_t cycle) {
        while (clipCount && clips[clipHead].ready <= cycle) {
            clipCommittedShift = clips[clipHead].shift;
            clipHead = (clipHead + 1) & 7u;
            --clipCount;
        }
    }
    void queueClip(uint64_t issue, bool set) {
        advanceClip(issue);
        clipWorkingShift = set ? 24u : std::min(24u, clipWorkingShift + 6u);
        if (clipCount == 8) {
            supported = false;
            return;
        }
        clips[(clipHead + clipCount++) & 7u] = {issue + 4u, clipWorkingShift};
    }
    void requireClip(uint32_t mask) {
        if (clipCommittedShift < 24)
            clipInputMask |= (mask & 0xffffffu) >> clipCommittedShift;
    }
    void begin(bool resume, uint32_t nextPc, const uint8_t* data) {
        if (seenRun && lazyLoadsMode() == 2 && (aliasLo || aliasHi)) {
            uint32_t count, flags;
            std::memcpy(&count, data + 9 * 16 + 8, 4);
            std::memcpy(&flags, data + 7 * 16, 4);
            const bool deadResume = resume && nextPc == 515u * 8u && int16_t(count) > 0 && !(flags & 1u);
            if (!deadResume)
                consumeAliases(~0ull, ~0ull);
        }
        if (!seenRun) {
            startsReset = !resume;
            seenRun = true;
        }
        if (!resume)
            backupBase = false;
    }
};
bool dependencies() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_DEPENDENCIES");
        return !(e && e[0] == '0');
    }();
    return on;
}
struct MemoryState {
    Data data{};
    VIFRegisters vif{};
    uint32_t pending = 0;
    bool directHl = false, masked = false;
    explicit MemoryState(const PS2Memory& m)
        : vif(m.vif1_regs), pending(m.m_vif1PendingPath2ImageQwc), directHl(m.m_vif1PendingPath2DirectHl),
          masked(m.m_path3Masked) {
        std::memcpy(data.data(), m.getVU1Data(), data.size());
    }
    void restore(PS2Memory& m) const {
        std::memcpy(m.getVU1Data(), data.data(), data.size());
        m.vif1_regs = vif;
        m.m_vif1PendingPath2ImageQwc = pending;
        m.m_vif1PendingPath2DirectHl = directHl;
        m.m_path3Masked = masked;
    }
    bool controlsMatch(const PS2Memory& m) const {
        return !std::memcmp(&vif, &m.vif1_regs, sizeof vif) && pending == m.m_vif1PendingPath2ImageQwc &&
               directHl == m.m_vif1PendingPath2DirectHl && masked == m.m_path3Masked;
    }
    bool matches(const PS2Memory& m) const {
        return controlsMatch(m) && !std::memcmp(data.data(), m.getVU1Data(), data.size());
    }
    bool operator==(const MemoryState& q) const {
        return data == q.data && !std::memcmp(&vif, &q.vif, sizeof vif) && pending == q.pending &&
               directHl == q.directHl && masked == q.masked;
    }
};
// Restore inherited words a cache line at a time where possible. Mixed
// chunks copy contiguous unset-bit runs; each source word remains exact.
void inheritWords(uint8_t* dest, const uint8_t* source, const WordMask& written) {
    for (unsigned chunk = 0; chunk < written.size(); ++chunk) {
        uint64_t pending = ~written[chunk];
        const unsigned base = chunk * 256u;
        if (!pending)
            continue;
        if (pending == ~0ull) {
            std::memcpy(dest + base, source + base, 256);
            continue;
        }
        while (pending) {
            const unsigned first = std::countr_zero(pending), count = std::countr_one(pending >> first);
            std::memcpy(dest + base + first * 4u, source + base + first * 4u, count * 4u);
            pending &= ~(((1ull << count) - 1ull) << first);
        }
    }
}
struct Event {
    enum Kind { Gif, Upload, Motion, Drain, Forward, Mask } kind = Drain;
    std::vector<uint8_t> data;
    GifPathId path = GifPathId::Path1;
    bool drain = false, directHl = false;
    drakan_vu1::UploadHook hook = nullptr;
    uint32_t first = 0, count = 0;
    gsr::ObjectMotionScope motion{};
    WordMask cameraWrites{};
    std::vector<std::pair<uint32_t, uint32_t>> packetWords;
    bool operator==(const Event& e) const {
        return kind == e.kind && data == e.data && path == e.path && drain == e.drain && directHl == e.directHl &&
               hook == e.hook && first == e.first && count == e.count &&
               !std::memcmp(&motion, &e.motion, sizeof motion);
    }
};
struct Cut {
    uint32_t offset;
    std::unique_ptr<MemoryState> memory;
    drakan_object_motion::Cursor motion;
    uint64_t key;
    bool bypass;
    Cut(uint32_t offset, PS2Memory& m, uint64_t key, bool bypass)
        : offset(offset), memory(bypass ? nullptr : std::make_unique<MemoryState>(m)),
          motion(drakan_object_motion::captureCursor()), key(key), bypass(bypass) {}
};
struct Capture {
    PS2Memory* memory = nullptr;
    std::vector<Event>* events = nullptr;
    bool discard = false, speculative = false, mscal = false, unsupported = false;
    uint64_t pairs = 0;
    Trace* trace = nullptr;
    std::vector<Cut>* cuts = nullptr;
    std::function<void(uint32_t, uint32_t)> makeCut;
};
thread_local Capture* current = nullptr;
struct CaptureScope {
    Capture* old;
    explicit CaptureScope(Capture& c) : old(current) {
        current = &c;
        captureActive = true;
    }
    ~CaptureScope() {
        current = old;
        captureActive = old != nullptr;
    }
};
struct CursorScope {
    drakan_object_motion::Cursor old = drakan_object_motion::captureCursor();
    ~CursorScope() { drakan_object_motion::restoreCursor(old); }
};
struct Binding {
    PS2Memory* memory = nullptr;
    VU1Interpreter* vu = nullptr;
    std::function<void(uint64_t, uint64_t, uint64_t)> publish;
} binding;
struct Stats {
    std::atomic<uint64_t> batches{0}, groups{0}, accepted{0}, rejected{0}, verified{0}, bad{0}, unsupported{0},
        single{0}, exceptions{0};
    std::atomic<uint64_t> attempts{0}, small{0}, arbiter{0}, code{0}, abandoned{0};
    std::array<std::atomic<uint64_t>, 14> reasons{};
    std::array<std::atomic<uint64_t>, 1024> memoryRejects{};
    std::array<std::atomic<uint64_t>, 8> clippingConflicts{};
    std::atomic<uint64_t> packetConflicts{0};
    std::atomic<uint64_t> staticRegBlocks{0}, staticRegPairs{0}, staticRegChecks{0}, staticRegBad{0};
    std::atomic<uint64_t> reuseEligible{0}, reuseRepeated{0}, reusePairs{0}, reuseRepeatedPairs{0}, reuseSidecars{0},
        reuseMetadata{0}, reuseMetadataPairs{0};
    std::atomic<uint64_t> acceptedRuns{0}, rejectedRuns{0}, acceptedPairs{0}, rejectedPairs{0};
    std::atomic<uint64_t> predictions{0}, predictionAccepted{0}, historyMisses{0}, serialBypass{0}, ownerHeads{0},
        nativeUvPairs{0}, nativeTransformPairs{0}, nativePacketPairs{0}, nativePacketBulk{0}, positiveQueries{0},
        positiveAccepted{0}, deferredLoads{0}, repairedLanes{0};
} stats;
bool verify() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_VERIFY");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
bool predict() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_PREDICT");
        return !(e && e[0] == '0');
    }();
    return on;
}
bool adaptive() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_ADAPTIVE");
        return !(e && e[0] == '0');
    }();
    return on;
}
unsigned threads() {
    static const unsigned n = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH_THREADS");
        long n = e ? std::strtol(e, nullptr, 10) : 4;
        return unsigned(std::clamp(n, 1l, 8l));
    }();
    return n;
}
struct Group {
    size_t first, last;
    std::unique_ptr<MemoryState> input;
    Vu1Batch::Seed seed;
    std::unique_ptr<MemoryState> output;
    std::unique_ptr<Vu1Batch::Checkpoint> result;
    std::vector<Event> events;
    uint64_t calls = 0, pairs = 0, cycles = 0;
    std::mutex mutex;
    std::condition_variable doneCv;
    bool done = false, queued = false, collected = false;
    std::exception_ptr error;
    const Input* jobs = nullptr;
    const std::array<uint8_t, PS2_VU1_CODE_SIZE>* code = nullptr;
    Trace trace;
    drakan_object_motion::Cursor motionInput, motionOutput;
    uint64_t key = 0;
    bool predicted = false, ownerHead = false, bypass = false;
    Group(size_t f, std::unique_ptr<MemoryState> m, const Vu1Batch::Checkpoint& s)
        : first(f), last(f), input(std::move(m)), seed(s), motionInput(drakan_object_motion::captureCursor()) {}
};
struct History {
    uint64_t key, age;
    VU1State state;
    uint64_t cycle;
    std::array<uint8_t, 12> clipping;
    unsigned failures = 0, cooldown = 0;
    History(uint64_t key, uint64_t age, const PS2Memory& memory, const VU1Interpreter& vu)
        : key(key), age(age), state(vu.state()), cycle(vu.totalCycles()) {
        std::memcpy(clipping.data(), memory.getVU1Data() + 65u * 16u, clipping.size());
    }
};
struct Executor {
    std::unique_ptr<PS2Memory> memory = std::make_unique<PS2Memory>(PS2Memory::IsolatedVU::Yes);
    std::unique_ptr<VU1Interpreter> vu = std::make_unique<VU1Interpreter>();
    GS gs;
    const Input* active = nullptr;
    bool prepare = false;
    uint64_t calls = 0;
    Executor() {
        memory->setVu1MscalCallback([this](uint32_t pc, uint32_t top, uint32_t itop) { run(false, pc, top, itop); });
        memory->setVu1MscntCallback([this](uint32_t top, uint32_t itop) { run(true, 0, top, itop); });
    }
    void code(const std::array<uint8_t, PS2_VU1_CODE_SIZE>& data) {
        if (std::memcmp(memory->getVU1Code(), data.data(), data.size())) {
            std::memcpy(memory->getVU1Code(), data.data(), data.size());
            memory->markVU1CodeModified();
        }
    }
    void run(bool resume, uint32_t pc, uint32_t top, uint32_t itop) {
        if (prepare)
            return;
        if (current && current->trace)
            current->trace->begin(resume, resume ? vu->state().pc : pc, memory->getVU1Data());
        Trace* trace = current ? current->trace : nullptr;
        const bool isolateStatus = trace && kMonotoneStatus;
        const uint32_t inheritedSticky = vu->state().status & 0xfc0u;
        if (isolateStatus) {
            trace->sanitizedStatus = true;
            vu->state().status &= ~0xfc0u;
        }
        vu->state().dBitEnabled = active->meta.dEn;
        vu->state().tBitEnabled = active->meta.tEn;
        if (resume)
            vu->resume(memory->getVU1Code(), PS2_VU1_CODE_SIZE, memory->getVU1Data(), PS2_VU1_DATA_SIZE, gs,
                       memory.get(), top, itop);
        else
            vu->execute(memory->getVU1Code(), PS2_VU1_CODE_SIZE, memory->getVU1Data(), PS2_VU1_DATA_SIZE, gs,
                        memory.get(), pc, top, itop);
        if (trace)
            trace->advanceClip(vu->totalCycles());
        if (isolateStatus) {
            trace->generatedSticky |= vu->state().status & 0xfc0u;
            vu->state().status |= inheritedSticky;
        }
        ++calls;
    }
    void job(const Input& in) {
        active = &in;
        memory->runVif1Slice(in.data.data(), uint32_t(in.data.size()), in.offset, in.meta.objectMotion.get(), in.begin,
                             in.end);
        active = nullptr;
    }
    void execute(Group& g) {
        PhaseClock timer;
        code(*g.code);
        g.input->restore(*memory);
        g.seed.restore(*vu);
        calls = 0;
        drakan_object_motion::restoreCursor(g.motionInput);
        Capture c{memory.get(), &g.events, false, true};
        c.trace = dependencies() ? &g.trace : nullptr;
        CaptureScope scope(c);
        for (size_t i = g.first; i < g.last; ++i) {
            if (g.jobs[i].vif())
                job(g.jobs[i]);
            else {
                Event e;
                e.kind = Event::Forward;
                e.first = uint32_t(i);
                g.events.push_back(std::move(e));
            }
        }
        g.calls = calls;
        g.pairs = c.pairs;
        g.output = std::make_unique<MemoryState>(*memory);
        g.result = std::make_unique<Vu1Batch::Checkpoint>(*vu);
        g.cycles = g.result->cycle() - g.seed.cycle();
        g.motionOutput = drakan_object_motion::captureCursor();
        timer.mark(Phase::Worker);
    }
};
struct Pool {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Group*> queue;
    bool stop = false;
    std::vector<std::thread> workers;
    std::unique_ptr<Executor> preparer;
    static constexpr unsigned kHistorySets = 1024;
    std::array<std::unique_ptr<History>, kHistorySets * 4> history;
    struct ReuseProbe {
        uint64_t key = 0, signature = 0, metadata = 0;
    };
    std::array<ReuseProbe, 4096> reuseProbes{};
    uint64_t age = 0, previousSignature = 0;
    void profileReuse(const Group& g, const PS2Memory& memory, const VU1Interpreter& vu,
                      const std::vector<Input>& jobs) {
        if (!reuseProfileEnabled() || !g.key)
            return;
        if (g.motionInput.tracking() || g.motionOutput.tracking()) {
            ++stats.reuseSidecars;
            return;
        }
        for (size_t i = g.first; i < g.last; ++i)
            if (jobs[i].meta.objectMotion) {
                ++stats.reuseSidecars;
                return;
            }
        for (const auto& e : g.events)
            if (e.kind == Event::Motion) {
                ++stats.reuseSidecars;
                return;
            }
        ++stats.reuseEligible;
        stats.reusePairs += g.pairs;
        uint64_t hash = 1469598103934665603ull;
        auto bytes = [&](const void* p, size_t n) {
            const auto* b = static_cast<const uint8_t*>(p);
            for (size_t i = 0; i < n; ++i)
                hash = (hash ^ b[i]) * 1099511628211ull;
        };
        auto value = [&](const auto& v) { bytes(&v, sizeof v); };
        const auto& t = g.trace;
        const auto& s = vu.state();
        value(t.pathHash);
        value(t.controlHash);
        value(t.liveMemory);
        value(t.lowMemory);
        value(t.writtenMemory);
        value(t.liveLo);
        value(t.liveHi);
        value(t.liveVa);
        value(t.writtenLo);
        value(t.writtenHi);
        value(t.writtenVa);
        value(t.aliasLo);
        value(t.aliasHi);
        for (unsigned half = 0; half < 2; ++half) {
            auto bits = half ? t.aliasHi : t.aliasLo;
            while (bits) {
                const unsigned lane = half * 64u + std::countr_zero(bits);
                value(t.aliasWords[lane]);
                bits &= bits - 1;
            }
        }
        value(t.firstMacReady == ~0ull ? ~0ull : t.firstMacReady - g.seed.cycle());
        value(t.clipInputMask);
        value(t.clipWorkingShift);
        value(t.clipCommittedShift);
        value(t.readQ);
        value(t.readP);
        value(t.readI);
        value(t.qWritten);
        value(t.pWritten);
        value(t.iWritten);
        value(t.fmacWritten);
        value(t.statusWritten);
        value(t.clipWritten);
        value(t.needMac);
        value(t.backupBase);
        value(t.sanitizedStatus);
        value(t.statusSafe);
        value(t.supported);
        value(t.complete);
        value(t.idle);
        value(t.startsReset);
        value(t.positiveClipGate);
        value(g.calls);
        value(g.pairs);
        value(g.cycles);
        for (const auto& tag : t.tagInputs)
            value(tag);
        for (const auto& e : g.events) {
            value(e.kind);
            value(e.path);
            value(e.data.size());
            value(e.drain);
            value(e.directHl);
            if (e.kind == Event::Upload) {
                value(e.first);
                value(e.count);
                value(e.cameraWrites);
            }
        }
        const uint64_t metadata = hash;
        for (size_t i = g.first; i < g.last; ++i) {
            const auto& j = jobs[i];
            value(j.path3);
            value(bool(j.task));
            value(j.offset);
            value(j.begin);
            value(j.end);
            value(j.meta.dEn);
            value(j.meta.tEn);
            value(j.data.size());
            bytes(j.data.data(), j.data.size());
        }
        value(memory.vif1_regs);
        value(memory.m_vif1PendingPath2ImageQwc);
        value(memory.m_vif1PendingPath2DirectHl);
        value(memory.m_path3Masked);
        value(t.liveLo);
        value(t.liveHi);
        value(t.liveVa);
        value(t.clipInputMask);
        value(t.needMac);
        value(t.readQ);
        value(t.readP);
        value(t.readI);
        for (unsigned r = 0; r < 32; ++r)
            for (unsigned c = 0; c < 4; ++c)
                if (((r < 16 ? t.liveLo : t.liveHi) >> (4 * (r & 15) + c)) & 1u)
                    value(s.vf[r][c]);
        for (unsigned r = 0; r < 16; ++r)
            if (t.liveVa >> r & 1u)
                value(s.vi[r]);
        for (unsigned c = 0; c < 4; ++c)
            if (t.liveVa >> (16 + c) & 1u)
                value(s.acc[c]);
        if (t.readQ)
            value(s.q);
        if (t.readP)
            value(s.p);
        if (t.readI)
            value(s.i);
        value(s.clip & t.clipInputMask);
        if (t.needMac)
            value(s.mac);
        if (t.statusWritten && !t.sanitizedStatus)
            value(s.status & ~15u);
        const auto* data = memory.getVU1Data();
        value(t.liveMemory);
        value(t.lowMemory);
        value(t.positiveClipGate);
        for (unsigned chunk = 0; chunk < 64; ++chunk) {
            uint64_t pending =
                t.liveMemory[chunk] | t.lowMemory[chunk] | (chunk == 4 && t.positiveClipGate ? 1ull << 4 : 0ull);
            while (pending) {
                const unsigned bit = std::countr_zero(pending), word = chunk * 64u + bit;
                uint32_t x;
                std::memcpy(&x, data + word * 4u, 4);
                const uint32_t mask = t.liveMemory[chunk] >> bit & 1u ? ~0u : 0xffffu;
                value(x & mask);
                pending &= pending - 1;
            }
        }
        for (const auto [word, mask] : t.tagInputs) {
            uint32_t x;
            std::memcpy(&x, data + word * 4u, 4);
            value(word);
            value(mask);
            value(x & mask);
        }
        const auto hook = drakan_vu1::g_uploadHook.load(std::memory_order_acquire);
        value(hook);
        auto& probe = reuseProbes[g.key & (reuseProbes.size() - 1u)];
        if (probe.key == g.key && probe.signature == hash) {
            ++stats.reuseRepeated;
            stats.reuseRepeatedPairs += g.pairs;
        }
        if (probe.key == g.key && probe.metadata == metadata) {
            ++stats.reuseMetadata;
            stats.reuseMetadataPairs += g.pairs;
        }
        probe = {g.key, hash, metadata};
    }
    History* lookup(uint64_t key) {
        const unsigned base = unsigned(key & (kHistorySets - 1u)) * 4u;
        for (unsigned i = 0; i < 4; ++i)
            if (history[base + i] && history[base + i]->key == key)
                return history[base + i].get();
        return nullptr;
    }
    void remember(uint64_t key, const PS2Memory& memory, const VU1Interpreter& vu) {
        if (!key)
            return;
        const unsigned base = unsigned(key & (kHistorySets - 1u)) * 4u;
        unsigned slot = base;
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned at = base + i;
            if (!history[at] || history[at]->key == key) {
                slot = at;
                break;
            }
            if (history[at]->age < history[slot]->age)
                slot = at;
        }
        if (!history[slot])
            history[slot] = std::make_unique<History>(key, ++age, memory, vu);
        else {
            const bool same = history[slot]->key == key;
            const unsigned failures = same ? history[slot]->failures : 0, cooldown = same ? history[slot]->cooldown : 0;
            *history[slot] = History(key, ++age, memory, vu);
            history[slot]->failures = failures;
            history[slot]->cooldown = cooldown;
        }
    }
    void start() {
        if (!workers.empty())
            return;
        preparer = std::make_unique<Executor>();
        preparer->prepare = true;
        for (unsigned i = 0; i < threads(); ++i)
            workers.emplace_back([this] {
                std::unique_ptr<Executor> executor;
                std::exception_ptr startup;
                try {
                    executor = std::make_unique<Executor>();
                } catch (...) {
                    startup = std::current_exception();
                }
                for (;;) {
                    Group* g;
                    {
                        std::unique_lock lock(mutex);
                        cv.wait(lock, [&] { return stop || !queue.empty(); });
                        if (queue.empty() && stop)
                            return;
                        g = queue.front();
                        queue.pop_front();
                    }
                    try {
                        if (startup)
                            std::rethrow_exception(startup);
                        executor->execute(*g);
                    } catch (...) {
                        g->error = std::current_exception();
                    }
                    {
                        std::lock_guard lock(g->mutex);
                        g->done = true;
                        g->doneCv.notify_one();
                    }
                }
            });
        std::atexit(vu1batch::shutdown);
    }
};
Pool& pool() {
    static Pool* p = new Pool;
    return *p;
}
uint64_t seedSignature(uint32_t code, uint32_t offset, const PS2Memory& memory, const Input& in) {
    const auto& vif = memory.vif1_regs;
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { key = (key ^ x) * 1099511628211ull; };
    mix(code);
    mix(offset);
    mix(vif.base);
    mix(vif.ofst);
    mix(vif.tops);
    mix(vif.itops);
    uint32_t flags, parameters[4];
    const auto* data = memory.getVU1Data();
    std::memcpy(&flags, data + 7 * 16, 4);
    std::memcpy(parameters, data + 9 * 16, 16);
    mix(flags);
    for (auto v : parameters)
        mix(v);
    if (in.meta.objectMotion) {
        const drakan_object_motion::Marker* marker = nullptr;
        for (const auto& m : in.meta.objectMotion->markers) {
            if (m.byteOffset > offset)
                break;
            marker = &m;
        }
        if (marker) {
            mix(marker->scope.objectId);
            mix(marker->scope.partId);
            mix(marker->scope.generation);
            mix(marker->scope.passKey);
        }
    }
    // Addresses and DMA offsets are aligned. Avalanche their high bits before
    // selecting a cache set, rather than concentrating low-bit-aligned keys.
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33;
    return key ? key : 1u;
}
void emit(PS2Memory& memory, const std::vector<Event>& events, const std::vector<Input>& jobs) {
    for (const auto& e : events)
        switch (e.kind) {
            case Event::Gif:
                memory.submitGifPacket(e.path, e.data.data(), uint32_t(e.data.size()), e.drain, e.directHl);
                break;
            case Event::Upload:
                e.hook(e.first, e.count, e.data.data());
                break;
            case Event::Motion:
                drakan_object_motion::replayCaptured(e.motion);
                break;
            case Event::Drain:
                if (memory.m_gifArbiter)
                    memory.m_gifArbiter->drain();
                break;
            case Event::Forward: {
                const auto& in = jobs[e.first];
                if (in.task)
                    in.task();
                else
                    memory.submitGifPacket(GifPathId::Path3, in.data.data(), uint32_t(in.data.size()), true);
                break;
            }
            case Event::Mask: {
                const bool old = memory.m_path3Masked;
                memory.m_path3Masked = e.drain;
                if (old && !e.drain)
                    memory.flushMaskedPath3Packets();
                break;
            }
        }
}
} // namespace

bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VU_BATCH");
        if (!e || !e[0] || e[0] == '0')
            return false;
        // These diagnostics keep single-stream mutable scratch state.
        for (const char* key : {"PS2X_VU1_TRACE_FLAG", "PS2X_VU1_DUMP", "PS2X_VU1_CHECK", "PS2X_VU1_DEBUG_TAG",
                                "PS2X_VIF_LOG", "PS2X_VU1_STATS", "DRAKAN_VU_RECORD"})
            if (std::getenv(key))
                return false;
        if (const char* value = std::getenv("DRAKAN_VU_STATS"); value && value[0] && value[0] != '0')
            return false;
        const char* check = std::getenv("DRAKAN_VUCHECK");
        return !(check && check[0] && check[0] != '0') || verify();
    }();
    return on && !stats.bad.load(std::memory_order_relaxed);
}
void bind(PS2Memory& m, VU1Interpreter& vu, std::function<void(uint64_t, uint64_t, uint64_t)> publish) {
    binding = {&m, &vu, std::move(publish)};
}
bool healthy() {
    return !stats.bad.load(std::memory_order_relaxed);
}
void unbind(PS2Memory& m) {
    if (binding.memory == &m)
        binding = {};
}

bool captureGif(PS2Memory* m, GifPathId path, const uint8_t* data, uint32_t size, bool drain, bool directHl) {
    if (!current || current->memory != m)
        return false;
    if (!current->discard && data && size >= 16) {
        Event e;
        e.kind = Event::Gif;
        e.path = path;
        e.drain = drain;
        e.directHl = directHl;
        e.data.assign(data, data + size);
        if (path == GifPathId::Path1 && current->trace)
            e.packetWords.swap(current->trace->packetWords);
        current->events->push_back(std::move(e));
    }
    return true;
}
bool captureUpload(PS2Memory* m, drakan_vu1::UploadHook hook, uint32_t first, uint32_t count, const uint8_t* data) {
    if (!current || current->memory != m)
        return false;
    if (!current->discard) {
        Event e;
        e.kind = Event::Upload;
        e.hook = hook;
        e.first = first;
        e.count = count;
        e.data.assign(data, data + PS2_VU1_DATA_SIZE);
        if (current->trace)
            e.cameraWrites = current->trace->writtenMemory;
        current->events->push_back(std::move(e));
    }
    return true;
}
bool captureMotion(const gsr::ObjectMotionScope& motion) {
    if (!current)
        return false;
    if (!current->discard) {
        Event e;
        e.kind = Event::Motion;
        e.motion = motion;
        current->events->push_back(std::move(e));
    }
    return true;
}
void command(PS2Memory* m, uint32_t code, uint32_t offset) {
    if (!current || current->memory != m)
        return;
    if (current->trace && reuseProfileEnabled()) {
        auto& hash = current->trace->controlHash;
        hash = (hash ^ code) * 1099511628211ull;
        hash = (hash ^ offset) * 1099511628211ull;
    }
    const unsigned op = (code >> 24) & 127;
    if (op == 0x14 || op == 0x15) {
        current->mscal = true;
        if (current->cuts) {
            if (current->cuts->size() < 512)
                current->makeCut(code, offset);
            else
                current->unsupported = true;
        }
    }
    // Code changes remain ordered serial barriers. PATH3 mask transitions are
    // retained as collector records alongside the packets they govern.
    if (op == 0x4a)
        current->unsupported = true;
}
bool captureMask(PS2Memory* m, bool masked) {
    if (!current || current->memory != m)
        return false;
    if (!current->discard) {
        Event e;
        e.kind = Event::Mask;
        e.drain = masked;
        current->events->push_back(std::move(e));
    }
    return true;
}
bool captureDrain(PS2Memory* m) {
    if (!current || current->memory != m)
        return false;
    if (!current->discard)
        current->events->emplace_back();
    return true;
}
bool capturePairs(uint64_t pairs) {
    if (!current || !current->speculative)
        return false;
    current->pairs += pairs;
    return true;
}

bool tracking() {
    return current && current->trace;
}
bool trackVif(PS2Memory* m) {
    return tracking() && current->memory == m;
}
bool dispatch(VU1Interpreter& v, Vu1Step::Ctx& c) {
    return staticRegsEnabled() ? Vu1Batch::dispatch<true>(v, c, *current->trace)
                               : Vu1Batch::dispatch<false>(v, c, *current->trace);
}
void memoryRead(PS2Memory* m, uint32_t address, uint32_t bytes) {
    if (!trackVif(m))
        return;
    for (uint32_t i = 0; i < bytes; i += 4)
        current->trace->read((address + i) & 16383u, 1);
}
void packetRead(PS2Memory* m, uint32_t address, uint32_t bytes, uint32_t offset, bool tag) {
    if (!trackVif(m))
        return;
    auto& t = *current->trace;
    if (!offset)
        t.packetWords.clear();
    for (uint32_t i = 0; i < bytes; i += 4) {
        const uint32_t w = ((address + i) & 16383u) / 4u;
        const uint64_t bit = 1ull << (w & 63u);
        if (t.writtenMemory[w / 64u] & bit)
            continue;
        // A store in this block may overlap a lazily transferred packet. Keep
        // the original dependency unless its transfer-time provenance is known.
        if (t.blockWrites[w / 64u] & bit) {
            t.read(w * 4u, 1);
            if (profileEnabled()) {
                if (!t.packetInputs)
                    t.packetInputs = std::make_unique<WordMask>();
                (*t.packetInputs)[w / 64u] |= bit;
            }
            continue;
        }
        t.packetWords.emplace_back(offset + i, w);
        // Only these GIFtag bits determine VU transfer length and termination.
        // Other inherited packet bytes are restored by the ordered collector.
        if (tag && i < 8)
            t.tagInputs.emplace_back(w, i == 0 ? 0xffffu : 0xfc000000u);
    }
}
void memoryWrite(PS2Memory* m, uint32_t address, uint8_t lanes) {
    if (trackVif(m))
        current->trace->write(address, lanes);
}
void unsupportedTrace() {
    if (tracking()) {
        current->trace->supported = false;
        current->trace->statusSafe = false;
    }
}
void scalarCommitted(bool q) {
    if (!tracking())
        return;
    auto& t = *current->trace;
    if (q) {
        t.qAssigned = t.qWritten = t.statusWritten = true;
    } else
        t.pAssigned = t.pWritten = true;
}
void runFinished(bool complete, bool idle) {
    if (tracking()) {
        current->trace->complete &= complete;
        current->trace->idle &= idle;
    }
}

bool process(std::vector<Input>& original, const std::function<void(const Input&)>& serial) {
    if (!enabled())
        return false;
    PhaseClock timer;
    ++stats.attempts;
    if (original.size() < 2) {
        ++stats.small;
        return false;
    }
    if (original[0].memory != binding.memory || !binding.vu)
        return false;
    auto& memory = *binding.memory;
    auto& vu = *binding.vu;
    if (memory.m_gifArbiter && !memory.m_gifArbiter->empty()) {
        ++stats.arbiter;
        return false;
    }
    size_t bytes = 0;
    for (auto& j : original) {
        if (!j.task && j.memory != &memory)
            return false;
        bytes += j.data.size();
    }
    if (bytes > 2 * 1024 * 1024)
        return false;
    if (!Vu1Aot::imageMatches(memory.getVU1Code(), PS2_VU1_CODE_SIZE)) {
        ++stats.code;
        return false;
    }
    auto& p = pool();
    try {
        p.start();
    } catch (...) {
        ++stats.exceptions;
        return false;
    }
    std::array<uint8_t, PS2_VU1_CODE_SIZE> code;
    std::memcpy(code.data(), memory.getVU1Code(), code.size());
    const Vu1Batch::Checkpoint seed(vu);
    // Stable storage lets workers read completed slices while the preparer
    // appends later, distinct elements. The hard limit is checked before a
    // push can exhaust this reservation; workers never access vector metadata.
    std::vector<Input> jobs;
    jobs.reserve(514);
    std::vector<std::unique_ptr<Group>> groups;
    uint64_t previous = p.previousSignature;
    // This guard also covers a late MPG/limit/decoder rejection after earlier
    // isolated groups have started. No input, code or seed can leave scope
    // while a worker still refers to it, and no output is published on decline.
    struct WaitAll {
        Pool& pool;
        std::vector<std::unique_ptr<Group>>& groups;
        ~WaitAll() {
            pool.cv.notify_all();
            for (auto& g : groups)
                if (g->queued) {
                    std::unique_lock lock(g->mutex);
                    g->doneCv.wait(lock, [&] { return g->done; });
                    if (!g->collected)
                        ++stats.abandoned;
                }
        }
    } waitAll{p, groups};
    std::array<Group*, 8> pending{};
    unsigned pendingCount = 0;
    auto flush = [&] {
        if (!pendingCount)
            return;
        {
            std::lock_guard lock(p.mutex);
            for (unsigned i = 0; i < pendingCount; ++i) {
                p.queue.push_back(pending[i]);
                pending[i]->queued = true;
            }
        }
        pendingCount = 0;
        p.cv.notify_all();
    };
    auto submit = [&](Group& g) {
        if (g.ownerHead || g.bypass)
            return;
        if (predict()) {
            if (const auto* h = p.lookup(g.key)) {
                std::memcpy(g.input->data.data() + 65u * 16u, h->clipping.data(), h->clipping.size());
                g.seed.state = h->state;
                g.seed.cycle(h->cycle);
                g.predicted = true;
                ++stats.predictions;
            } else
                ++stats.historyMisses;
        }
        g.jobs = jobs.data();
        g.code = &code;
        if (streamingEnabled()) {
            pending[pendingCount++] = &g;
            if (pendingCount == threads())
                flush();
        }
    };
    // Preparation runs only VIF state/uploads in a private memory view. It has
    // no geometry, GS, camera, provenance or guest-visible publication effects.
    try {
        CursorScope cursorScope;
        auto& prep = *p.preparer;
        prep.code(code);
        MemoryState(memory).restore(*prep.memory);
        groups.push_back(std::make_unique<Group>(0, nullptr, seed));
        groups.back()->ownerHead = true;
        // Decode complete original jobs to locate real command boundaries, not
        // bit patterns inside upload data. Slices retain absolute sidecar
        // offsets and share one motion cursor across each original DMA job.
        for (const auto& in : original) {
            if (!in.vif()) {
                if (jobs.size() == 512)
                    return false;
                jobs.push_back(in);
                continue;
            }
            std::vector<Cut> cuts;
            Capture c{prep.memory.get(), nullptr, true, true};
            c.cuts = &cuts;
            c.makeCut = [&](uint32_t command, uint32_t offset) {
                uint64_t key = 0;
                if (predict() || adaptive()) {
                    const uint64_t signature = seedSignature(command, offset, *prep.memory, in);
                    key = signature ^ (previous * 0x9e3779b97f4a7c15ull);
                    if (!key)
                        key = 1;
                    previous = signature;
                }
                const auto* h = adaptive() ? p.lookup(key) : nullptr;
                cuts.emplace_back(offset, *prep.memory, key, h && h->cooldown);
            };
            {
                CaptureScope scope(c);
                prep.job(in);
            }
            if (c.unsupported) {
                ++stats.unsupported;
                return false;
            }
            uint32_t at = 0;
            auto slice = [&](uint32_t end) {
                if (end <= at)
                    return;
                Input out;
                out.memory = in.memory;
                out.meta = in.meta;
                out.offset = in.offset + at;
                out.begin = in.begin && at == 0;
                out.end = in.end && end == in.data.size();
                out.data.assign(in.data.begin() + at, in.data.begin() + end);
                jobs.push_back(std::move(out));
                at = end;
            };
            for (auto& cut : cuts) {
                slice(cut.offset - in.offset);
                if (jobs.size() > 512)
                    return false;
                const auto first = jobs.size();
                if (groups.back()->first == first)
                    groups.pop_back();
                else {
                    groups.back()->last = first;
                    submit(*groups.back());
                }
                groups.push_back(std::make_unique<Group>(first, std::move(cut.memory), seed));
                groups.back()->motionInput = cut.motion;
                groups.back()->key = cut.key;
                groups.back()->bypass = cut.bypass;
                groups.back()->ownerHead = groups.size() == 1;
            }
            slice(uint32_t(in.data.size()));
            if (jobs.size() > 512)
                return false;
        }
        if (jobs.empty())
            return false;
        groups.back()->last = jobs.size();
        submit(*groups.back());
    } catch (...) {
        ++stats.exceptions;
        return false;
    }
    if (groups.size() < 2) {
        ++stats.single;
        return false;
    }
    ++stats.batches;
    stats.groups.fetch_add(groups.size());
    // Remaining groups are dispatched only after the decoder accepted the
    // whole stream. Earlier closed groups may already have finished privately.
    timer.mark(Phase::Prepare);
    if (streamingEnabled())
        flush();
    else {
        {
            std::lock_guard lock(p.mutex);
            for (auto& g : groups)
                if (!g->ownerHead && !g->bypass) {
                    p.queue.push_back(g.get());
                    g->queued = true;
                }
        }
        p.cv.notify_all();
    }
    for (auto& g : groups)
        if (g->bypass && !g->ownerHead)
            if (auto* h = p.lookup(g->key); h && h->cooldown)
                --h->cooldown;
    timer.mark(Phase::Queue);
    for (auto& ptr : groups) {
        auto& g = *ptr;
        if (!g.queued) {
            if (g.ownerHead)
                ++stats.ownerHeads;
            else
                ++stats.serialBypass;
            if (predict() || adaptive())
                try {
                    p.remember(g.key, memory, vu);
                } catch (...) {
                    ++stats.exceptions;
                }
            timer.mark(Phase::History);
            for (size_t i = g.first; i < g.last; ++i)
                serial(jobs[i]);
            timer.mark(Phase::Serial);
            continue;
        }
        {
            std::unique_lock lock(g.mutex);
            g.doneCv.wait(lock, [&] { return g.done; });
        }
        timer.mark(Phase::Wait);
        stats.nativeUvPairs += g.trace.nativeUvPairs;
        stats.nativeTransformPairs += g.trace.nativeTransformPairs;
        stats.nativePacketPairs += g.trace.nativePacketPairs;
        stats.nativePacketBulk += g.trace.nativePacketBulk;
        stats.deferredLoads += g.trace.deferredLoads;
        stats.staticRegBlocks += g.trace.staticRegBlocks;
        stats.staticRegPairs += g.trace.staticRegPairs;
        stats.staticRegChecks += g.trace.staticRegChecks;
        stats.staticRegBad += g.trace.staticRegBad;
        if (g.trace.positiveClipGate)
            ++stats.positiveQueries;
        bool positiveDifference = false;
        const bool actualMasked = memory.m_path3Masked;
        const auto* actualData = memory.getVU1Data();
        if (predict() || adaptive())
            try {
                p.remember(g.key, memory, vu);
            } catch (...) {
                ++stats.exceptions;
            }
        const bool motionCompatible = jobs[g.first].begin || g.motionInput.same(drakan_object_motion::captureCursor());
        const bool safeStatus = !g.trace.sanitizedStatus || g.trace.statusSafe;
        bool valid = !g.error && safeStatus && motionCompatible && g.input->matches(memory) &&
                     g.seed == Vu1Batch::Checkpoint(vu);
        if (!valid && !g.error && safeStatus && motionCompatible && dependencies()) {
            const auto& s = *g.input;
            unsigned reason = 0;
            bool compatible = s.controlsMatch(memory);
            if (compatible)
                reason = 1;
            for (unsigned chunk = 0; compatible && chunk < 64; ++chunk) {
                uint64_t pending = g.trace.liveMemory[chunk] | g.trace.lowMemory[chunk] |
                                   (chunk == 4 && g.trace.positiveClipGate ? 1ull << 4 : 0ull);
                while (compatible && pending) {
                    const unsigned bitIndex = std::countr_zero(pending), w = chunk * 64u + bitIndex;
                    const uint32_t mask = (g.trace.liveMemory[chunk] >> bitIndex & 1u)  ? ~0u
                                          : (g.trace.lowMemory[chunk] >> bitIndex & 1u) ? 0xffffu
                                                                                        : 0u;
                    const bool positive = w == 260 && g.trace.positiveClipGate;
                    uint32_t x, y;
                    std::memcpy(&x, actualData + w * 4, 4);
                    std::memcpy(&y, s.data.data() + w * 4, 4);
                    if (((x ^ y) & mask) || (positive && int16_t(x) <= 0)) {
                        compatible = false;
                        ++stats.memoryRejects[w / 4];
                        if (profileEnabled()) {
                            if (w / 4 == 65)
                                ++stats.clippingConflicts[(w & 3u) + (mask == ~0u ? 4u : 0u)];
                            if (g.trace.packetInputs && ((*g.trace.packetInputs)[chunk] >> bitIndex & 1u))
                                ++stats.packetConflicts;
                        }
                    } else if (positive && ((x ^ y) & 0xffffu))
                        positiveDifference = true;
                    pending &= pending - 1;
                }
            }
            for (const auto [word, mask] : g.trace.tagInputs)
                if (compatible) {
                    uint32_t x, y;
                    std::memcpy(&x, actualData + word * 4, 4);
                    std::memcpy(&y, s.data.data() + word * 4, 4);
                    if ((x ^ y) & mask) {
                        compatible = false;
                        ++stats.memoryRejects[word / 4];
                    }
                }
            if (compatible && Vu1Batch::merge(g.seed, *g.result, vu, *p.preparer->vu, g.trace, reason, actualData)) {
                stats.repairedLanes += std::popcount(g.trace.aliasLo) + std::popcount(g.trace.aliasHi);
                g.result = std::make_unique<Vu1Batch::Checkpoint>(*p.preparer->vu);
                inheritWords(g.output->data.data(), actualData, g.trace.writtenMemory);
                for (auto& e : g.events)
                    if (e.kind == Event::Upload)
                        inheritWords(e.data.data(), actualData, e.cameraWrites);
                for (auto& e : g.events)
                    if (e.kind == Event::Gif)
                        for (const auto [offset, word] : e.packetWords)
                            std::memcpy(e.data.data() + offset, actualData + word * 4, 4);
                valid = true;
            }
            if (!valid)
                ++stats.reasons[reason];
        }
        timer.mark(Phase::Validate);
        if (adaptive())
            if (auto* h = p.lookup(g.key)) {
                if (valid) {
                    h->failures = 0;
                    h->cooldown = 0;
                } else {
                    h->failures = std::min(h->failures + 1, 5u);
                    h->cooldown = (32u << h->failures) - 1u;
                }
            }
        if (!valid) {
            ++stats.rejected;
            if (g.error)
                ++stats.exceptions;
            stats.rejectedRuns += g.calls;
            stats.rejectedPairs += g.pairs;
            for (size_t i = g.first; i < g.last; ++i)
                serial(jobs[i]);
            g.collected = true;
            timer.mark(Phase::Serial);
            continue;
        }
        ++stats.accepted;
        if (positiveDifference)
            ++stats.positiveAccepted;
        if (g.predicted)
            ++stats.predictionAccepted;
        stats.acceptedRuns += g.calls;
        stats.acceptedPairs += g.pairs;
        p.profileReuse(g, memory, vu, jobs);
        if (verify()) {
            std::vector<Event> actual;
            Capture c{&memory, &actual, false, false};
            {
                CaptureScope scope(c);
                for (size_t i = g.first; i < g.last; ++i) {
                    if (jobs[i].vif())
                        serial(jobs[i]);
                    else {
                        Event e;
                        e.kind = Event::Forward;
                        e.first = uint32_t(i);
                        actual.push_back(std::move(e));
                    }
                }
            }
            ++stats.verified;
            if (!(MemoryState(memory) == *g.output) || !(Vu1Batch::Checkpoint(vu) == *g.result) || actual != g.events ||
                !g.motionOutput.same(drakan_object_motion::captureCursor())) {
                if (stats.bad.fetch_add(1) == 0) {
                    const auto actualState = Vu1Batch::Checkpoint(vu);
                    std::fprintf(stderr,
                                 "[vu-batch] MISMATCH: memory=%u state=%u scheduler=%u tail=%u kick=%u events=%u "
                                 "(%zu/%zu); original output retained\n",
                                 unsigned(!(MemoryState(memory) == *g.output)),
                                 unsigned(std::memcmp(&actualState.state, &g.result->state, sizeof(VU1State)) != 0),
                                 unsigned(actualState.scheduler != g.result->scheduler),
                                 unsigned(actualState.tail != g.result->tail),
                                 unsigned(actualState.kick != g.result->kick), unsigned(actual != g.events),
                                 actual.size(), g.events.size());
                    for (size_t i = 0; i < actualState.scheduler.size(); ++i)
                        if (actualState.scheduler[i] != g.result->scheduler[i]) {
                            std::fprintf(stderr, "[vu-batch] scheduler first byte %zu: %u / %u\n", i,
                                         unsigned(actualState.scheduler[i]), unsigned(g.result->scheduler[i]));
                            break;
                        }
                    for (size_t i = 0; i < actualState.tail.size(); ++i)
                        if (actualState.tail[i] != g.result->tail[i]) {
                            std::fprintf(stderr, "[vu-batch] tail first byte %zu: %u / %u\n", i,
                                         unsigned(actualState.tail[i]), unsigned(g.result->tail[i]));
                            break;
                        }
                    const auto* sa = reinterpret_cast<const uint8_t*>(&actualState.state);
                    const auto* sb = reinterpret_cast<const uint8_t*>(&g.result->state);
                    for (size_t i = 0; i < sizeof(VU1State); ++i)
                        if (sa[i] != sb[i]) {
                            std::fprintf(stderr, "[vu-batch] state first byte %zu: %u / %u\n", i, unsigned(sa[i]),
                                         unsigned(sb[i]));
                            break;
                        }
                    for (size_t i = 0; i < std::min(actual.size(), g.events.size()); ++i)
                        if (!(actual[i] == g.events[i])) {
                            std::fprintf(stderr, "[vu-batch] event %zu kind %u/%u bytes %zu/%zu\n", i,
                                         unsigned(actual[i].kind), unsigned(g.events[i].kind), actual[i].data.size(),
                                         g.events[i].data.size());
                            break;
                        }
                }
            }
            memory.m_path3Masked = actualMasked;
            emit(memory, actual, jobs);
        } else {
            g.output->restore(memory);
            g.result->restore(vu);
            drakan_object_motion::restoreCursor(g.motionOutput);
            binding.publish(g.cycles, g.calls, g.pairs);
            memory.m_path3Masked = actualMasked;
            emit(memory, g.events, jobs);
        }
        g.collected = true;
        timer.mark(Phase::Collect);
    }
    if (predict() || adaptive())
        p.previousSignature = previous;
    return true;
}
void shutdown() {
    auto& p = pool();
    {
        std::lock_guard lock(p.mutex);
        p.stop = true;
    }
    p.cv.notify_all();
    for (auto& t : p.workers)
        if (t.joinable())
            t.join();
    p.workers.clear();
}
void printStats() {
    if (!std::getenv("DRAKAN_VU_BATCH"))
        return;
    if (staticRegsEnabled())
        std::fprintf(stderr, "[vu-static-regs] blocks=%llu pairs=%llu checked=%llu mismatches=%llu\n",
                     (unsigned long long)stats.staticRegBlocks.load(), (unsigned long long)stats.staticRegPairs.load(),
                     (unsigned long long)stats.staticRegChecks.load(), (unsigned long long)stats.staticRegBad.load());
    if (reuseProfileEnabled())
        std::fprintf(stderr,
                     "[vu-reuse-profile] eligible=%llu repeated=%llu pairs=%llu repeated-pairs=%llu sidecars=%llu "
                     "metadata=%llu metadata-pairs=%llu\n",
                     (unsigned long long)stats.reuseEligible.load(), (unsigned long long)stats.reuseRepeated.load(),
                     (unsigned long long)stats.reusePairs.load(), (unsigned long long)stats.reuseRepeatedPairs.load(),
                     (unsigned long long)stats.reuseSidecars.load(), (unsigned long long)stats.reuseMetadata.load(),
                     (unsigned long long)stats.reuseMetadataPairs.load());
    std::fprintf(stderr,
                 "[vu-batch] %llu batches, %llu groups: %llu accepted, %llu rejected; %llu verified, %llu mismatches; "
                 "%llu unsupported, %llu single-group, %llu exceptions\n",
                 (unsigned long long)stats.batches.load(), (unsigned long long)stats.groups.load(),
                 (unsigned long long)stats.accepted.load(), (unsigned long long)stats.rejected.load(),
                 (unsigned long long)stats.verified.load(), (unsigned long long)stats.bad.load(),
                 (unsigned long long)stats.unsupported.load(), (unsigned long long)stats.single.load(),
                 (unsigned long long)stats.exceptions.load());
    std::fprintf(stderr,
                 "[vu-batch] attempts=%llu short=%llu arbiter=%llu code=%llu workers=%u dependencies=%u verify=%u\n",
                 (unsigned long long)stats.attempts.load(), (unsigned long long)stats.small.load(),
                 (unsigned long long)stats.arbiter.load(), (unsigned long long)stats.code.load(), threads(),
                 unsigned(dependencies()), unsigned(verify()));
    std::fprintf(stderr, "[vu-batch] dependency rejects "
                         "(VIF,memory,trace,reset,complete,idle,constant,VF,VI,ACC,scalar,CLIP,MAC,status):");
    for (const auto& n : stats.reasons)
        std::fprintf(stderr, " %llu", (unsigned long long)n.load());
    std::fprintf(stderr, "\n");
    std::array<std::pair<uint64_t, unsigned>, 1024> words;
    for (unsigned i = 0; i < 1024; ++i)
        words[i] = {stats.memoryRejects[i].load(), i};
    std::partial_sort(words.begin(), words.begin() + 8, words.end(), std::greater<>());
    std::fprintf(stderr, "[vu-batch] first conflicting qwords:");
    for (unsigned i = 0; i < 8 && words[i].first; ++i)
        std::fprintf(stderr, " %u:%llu", words[i].second, (unsigned long long)words[i].first);
    std::fprintf(stderr, "; monotone-status=%u\n", unsigned(kMonotoneStatus));
    std::fprintf(stderr,
                 "[vu-batch] validated geometry calls=%llu rejected geometry calls=%llu; validated pairs=%llu rejected "
                 "speculative pairs=%llu\n",
                 (unsigned long long)stats.acceptedRuns.load(), (unsigned long long)stats.rejectedRuns.load(),
                 (unsigned long long)stats.acceptedPairs.load(), (unsigned long long)stats.rejectedPairs.load());
    std::fprintf(stderr, "[vu-batch] native transform traced pairs=%llu\n",
                 (unsigned long long)stats.nativeTransformPairs.load());
    std::fprintf(stderr, "[vu-batch] native packet traced pairs=%llu bulk calls=%llu enabled=%u\n",
                 (unsigned long long)stats.nativePacketPairs.load(), (unsigned long long)stats.nativePacketBulk.load(),
                 packetKernelMode());
    std::fprintf(stderr, "[vu-batch] deferred load lanes=%llu final alias repairs=%llu enabled=%u\n",
                 (unsigned long long)stats.deferredLoads.load(), (unsigned long long)stats.repairedLanes.load(),
                 lazyLoadsMode());
    std::fprintf(stderr, "[vu-batch] streaming preparation=%u\n", unsigned(streamingEnabled()));
    std::fprintf(stderr, "[vu-batch] positive clipping gates=%llu relaxed accepted=%llu enabled=%u\n",
                 (unsigned long long)stats.positiveQueries.load(), (unsigned long long)stats.positiveAccepted.load(),
                 unsigned(predicateEnabled()));
    std::fprintf(stderr, "[vu-batch] abandoned worker results=%llu\n", (unsigned long long)stats.abandoned.load());
    if (profileEnabled()) {
        std::fprintf(stderr, "[vu-batch-profile] q65 low/full lane conflicts:");
        for (const auto& n : stats.clippingConflicts)
            std::fprintf(stderr, " %llu", (unsigned long long)n.load());
        std::fprintf(stderr, "; possible packet-overlap conflicts=%llu\n",
                     (unsigned long long)stats.packetConflicts.load());
        static const char* names[] = {"prepare", "queue",    "history", "serial",
                                      "wait",    "validate", "collect", "worker-sum"};
        std::fprintf(stderr, "[vu-batch-profile] milliseconds:");
        for (size_t i = 0; i < std::size(names); ++i)
            std::fprintf(stderr, " %s=%.3f", names[i], double(phaseNs[i].load()) / 1e6);
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "[vu-batch] owner heads=%llu native UV traced pairs=%llu\n",
                 (unsigned long long)stats.ownerHeads.load(), (unsigned long long)stats.nativeUvPairs.load());
    std::fprintf(stderr, "[vu-batch] adaptive serial bypass=%llu enabled=%u\n",
                 (unsigned long long)stats.serialBypass.load(), unsigned(adaptive()));
    std::fprintf(stderr, "[vu-batch] history predictions=%llu accepted=%llu misses=%llu enabled=%u\n",
                 (unsigned long long)stats.predictions.load(), (unsigned long long)stats.predictionAccepted.load(),
                 (unsigned long long)stats.historyMisses.load(), unsigned(predict()));
}
} // namespace vu1batch
