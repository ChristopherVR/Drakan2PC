// Diagnostic only: replay immutable VUR1 inputs on isolated VU instances.
// Checkpoints are produced by a SERIAL oracle. Speedup here is a compute
// ceiling, not live-game speedup: the live producer still needs a
// dependency-safe split.
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cfenv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>
// Keep private-state inspection confined to this standalone diagnostic TU.
#define private public
#include "runtime/ps2_vu1.h"
#undef private
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "vu/aot/ps2_vu1_aot.h"
#include "vu/ps2_vu1_decode.h"
#include "vu/ps2_vu1_fast.h"
#include "vu/ps2_vu1_step.h"
#ifdef VU_SELECTED_PROBE
unsigned installSelectedInputProbe();
#endif

namespace {
constexpr size_t dataSize = 16384;
using Data = std::array<uint8_t, dataSize>;
using Packets = std::vector<std::vector<uint8_t>>;
struct Header {
    uint32_t magic, count, codeSize, stateSize;
};
struct Entry {
    uint32_t resume, startPC, top, itop, dEn, tEn;
};
struct Input {
    Entry e;
    Data data;
};

// Do not copy the large decoded-code cache, cache keys, host destinations or
// unused 64 KiB PATH1 capacity. Preserve ALL scheduler/branch/flag state, even
// across an unfinished run. All instances have the same Unit and code image.
struct Checkpoint {
    static constexpr size_t a = offsetof(VU1Interpreter, m_flagMask);
    static constexpr size_t b = offsetof(VU1Interpreter, m_xgkick);
    static constexpr size_t c = offsetof(VU1Interpreter, m_vfReady);
    VU1State state;
    std::array<uint8_t, b - a> scheduler;
    std::array<uint8_t, sizeof(VU1Interpreter) - c> tail;
    using Kick = VU1Interpreter::XgkickPipeline;
    static constexpr size_t k = offsetof(Kick, sourceAddress);
    std::array<uint8_t, sizeof(Kick) - k> kick;
    std::vector<uint8_t> packet;
    explicit Checkpoint(const VU1Interpreter& v) : state(v.state()) {
        static_assert(std::is_standard_layout_v<VU1Interpreter>);
        static_assert(std::is_trivially_copyable_v<VU1Interpreter>);
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        std::memcpy(scheduler.data(), p + a, scheduler.size());
        std::memcpy(tail.data(), p + c, tail.size());
        std::memcpy(kick.data(), reinterpret_cast<const uint8_t*>(&v.m_xgkick) + k, kick.size());
        if (v.m_xgkick.active) {
            if (v.m_xgkick.copiedBytes > v.m_xgkick.packet.size())
                std::abort();
            packet.assign(v.m_xgkick.packet.begin(), v.m_xgkick.packet.begin() + v.m_xgkick.copiedBytes);
        }
    }
    void restore(VU1Interpreter& v) const {
        auto* p = reinterpret_cast<uint8_t*>(&v);
        v.state() = state;
        std::memcpy(p + a, scheduler.data(), scheduler.size());
        std::memcpy(p + c, tail.data(), tail.size());
        std::memcpy(reinterpret_cast<uint8_t*>(&v.m_xgkick) + k, kick.data(), kick.size());
        std::copy(packet.begin(), packet.end(), v.m_xgkick.packet.begin());
        // These are host call-frame bindings, never portable checkpoint state.
        // run() rebinds active memory/GS; each pair rebinds upper destinations.
        v.m_packetTap = nullptr;
        v.m_upperDst = v.m_upperAccDst = nullptr;
        v.m_activeVuData = nullptr;
        v.m_activeVuDataSize = 0;
        v.m_activeGs = nullptr;
        v.m_activeMemory = nullptr;
    }
};
bool same(const VU1State& a, const VU1State& b) {
#define CMP(x)                                                                                                         \
    if (std::memcmp(&a.x, &b.x, sizeof a.x))                                                                           \
        return false;
    // clang-format off
    CMP(vf)
    CMP(vi)
    CMP(acc)
    CMP(q)
    CMP(p)
    CMP(i)
    CMP(r)
    CMP(pc)
    CMP(mac)
    CMP(clip)
    CMP(status)
    CMP(cycles)
    CMP(ebit)
    CMP(haltAfterDelaySlot)
    CMP(dBitEnabled)
    CMP(tBitEnabled)
    CMP(stoppedByD)
    CMP(stoppedByT)
    CMP(top)
    CMP(itop)
    CMP(branchPending)
    CMP(branchTarget)
    CMP(branchDelay)
    // clang-format on
#undef CMP
    return true;
}
struct Output {
    VU1State state;
    Data data;
    Packets packets;
};
struct RegisterReads {
    // Architectural constants are available without any predecessor.
    uint64_t assignedLo = 15, assignedHi = 0, liveLo = 0, liveHi = 0;
    uint32_t assignedVa = 1, liveVa = 0;
};
thread_local RegisterReads* traceReads = nullptr;
bool tracePair(VU1Interpreter& v, Vu1Step::Ctx& c) {
    const uint32_t pc = v.state().pc / 8;
    const auto& d = v.m_decodedCodeCache[pc];
    auto& r = *traceReads;
    r.liveLo |= d.rdLo & ~r.assignedLo;
    r.liveHi |= d.rdHi & ~r.assignedHi;
    r.liveVa |= d.rdVa & ~r.assignedVa;
    const auto before = c.pairs;
    const bool result = Vu1Aot::g_table[pc](v, c);
    if (c.pairs != before)
        for (unsigned i = 0; i < d.grpCount; ++i) {
            r.assignedLo |= d.grpLo[i];
            r.assignedHi |= d.grpHi[i];
            r.assignedVa |= d.grpVa[i];
        }
    return result;
}
struct Worker {
    std::unique_ptr<VU1Interpreter> vu = std::make_unique<VU1Interpreter>();
    GS gs;
    Data data{};
    Packets packets;
    void run(const Input& in, PS2Memory& mem, uint32_t codeSize, uint32_t budget = 65536) {
        data = in.data;
        packets.clear();
        vu->setPacketTap(&packets);
        Vu1Fast::spare(*vu).sinkNone = 1;
        vu->state().dBitEnabled = in.e.dEn != 0;
        vu->state().tBitEnabled = in.e.tEn != 0;
        if (in.e.resume)
            vu->resume(mem.getVU1Code(), codeSize, data.data(), dataSize, gs, &mem, in.e.top, in.e.itop, budget);
        else
            vu->execute(mem.getVU1Code(), codeSize, data.data(), dataSize, gs, &mem, in.e.startPC, in.e.top, in.e.itop,
                        budget);
    }
};
double ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: vu_parallel_replay recording.vur\n");
        return 2;
    }
    // The production debug ring/statistics are single-stream facilities.
    const char* unsupported[] = {"PS2X_VU1_TRACE_FLAG", "PS2X_VU1_DUMP", "PS2X_VU1_CHECK", "PS2X_VU1_DEBUG_TAG",
                                 "PS2X_VU1_STATS"};
    for (auto name : unsupported)
        if (std::getenv(name)) {
            std::fprintf(stderr, "unset %s\n", name);
            return 2;
        }
    FILE* f = std::fopen(argv[1], "rb");
    Header h{};
    if (!f || std::fread(&h, sizeof h, 1, f) != 1 || h.magic != 0x31525556 || h.stateSize != sizeof(VU1State) ||
        h.codeSize > 16384 || h.count > 100000)
        return 2;
    std::vector<uint8_t> code(h.codeSize);
    VU1State initial{};
    if (std::fread(code.data(), 1, code.size(), f) != code.size() || std::fread(&initial, sizeof initial, 1, f) != 1)
        return 2;
    std::vector<Input> inputs(h.count);
    for (auto& in : inputs)
        if (std::fread(&in.e, sizeof in.e, 1, f) != 1 || std::fread(in.data.data(), 1, dataSize, f) != dataSize)
            return 2;
    std::fclose(f);
    if (inputs.empty())
        return 2;
    auto mem = std::make_unique<PS2Memory>();
    if (!mem->initialize())
        return 2;
    std::memcpy(mem->getVU1Code(), code.data(), code.size());
    mem->markVU1CodeModified();
    if (!Vu1Aot::imageMatches(mem->getVU1Code(), h.codeSize))
        return 2;
    auto serial = std::make_unique<Worker>();
    serial->vu->state() = initial;
    std::vector<Checkpoint> checkpoints;
    std::vector<Output> expected(inputs.size());
    checkpoints.reserve(inputs.size());
    size_t resumes = 0, active = 0, packetBytes = 0;
    const auto prep = std::chrono::steady_clock::now();
    for (size_t i = 0; i < inputs.size(); ++i) {
        checkpoints.emplace_back(*serial->vu);
        resumes += inputs[i].e.resume != 0;
        active += serial->vu->m_xgkick.active;
        serial->run(inputs[i], *mem, h.codeSize);
        expected[i] = {serial->vu->state(), serial->data, serial->packets};
        for (const auto& p : serial->packets)
            packetBytes += p.size();
    }
    const double preparationMs = ms(prep);
    double serialBest = 1e30;
    for (int rep = 0; rep < 7; ++rep) {
        serial->vu->reset();
        serial->vu->state() = initial;
        auto start = std::chrono::steady_clock::now();
        for (auto& in : inputs)
            serial->run(in, *mem, h.codeSize);
        serialBest = std::min(serialBest, ms(start));
    }
#ifdef VU_SELECTED_PROBE
    const std::vector<Vu1Aot::Fn> originalDispatch(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    const unsigned changedBlocks = installSelectedInputProbe();
    double selectedBest = 1e30;
    for (int rep = 0; rep < 7; ++rep) {
        serial->vu->reset();
        serial->vu->state() = initial;
        auto start = std::chrono::steady_clock::now();
        for (auto& in : inputs)
            serial->run(in, *mem, h.codeSize);
        selectedBest = std::min(selectedBest, ms(start));
    }
    const std::vector<Vu1Aot::Fn> selectedDispatch(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    auto measure = [&] {
        double best = 1e30;
        for (int rep = 0; rep < 7; ++rep) {
            serial->vu->reset();
            serial->vu->state() = initial;
            auto start = std::chrono::steady_clock::now();
            for (auto& in : inputs)
                serial->run(in, *mem, h.codeSize);
            best = std::min(best, ms(start));
        }
        return best;
    };
    const double selectedSecond = measure();
    std::copy(originalDispatch.begin(), originalDispatch.end(), Vu1Aot::g_dispatch);
    const double originalSecond = measure();
    std::copy(selectedDispatch.begin(), selectedDispatch.end(), Vu1Aot::g_dispatch);
    std::fprintf(stderr,
                 "[selected-input-probe] %u changed blocks; ABBA_ms %.6f %.6f "
                 "%.6f %.6f; speedup %.6f\n",
                 changedBlocks, serialBest, selectedBest, selectedSecond, originalSecond,
                 (serialBest + originalSecond) / (selectedBest + selectedSecond));
#endif
    std::printf("{\"runs\":%zu,\"resumes\":%zu,\"active_path1_checkpoints\":%zu,"
                "\"checkpoint_bytes\":%zu,\"interpreter_bytes\":%zu,\"packet_bytes\":%zu,"
                "\"serial_seed_ms\":%.6f,\"serial_best_ms\":%.6f,\"workers\":[\n",
                inputs.size(), resumes, active, sizeof(Checkpoint), sizeof(VU1Interpreter), packetBytes, preparationMs,
                serialBest);
    for (unsigned n : {1u, 2u, 4u, 8u}) {
        std::atomic<size_t> next{0}, bad{0};
        std::barrier ready(n + 1), done(n + 1);
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < n; ++t)
            threads.emplace_back([&] {
                auto w = std::make_unique<Worker>();
                // Warm the local decoded image before starting the measured rounds.
                checkpoints[0].restore(*w->vu);
                w->run(inputs[0], *mem, h.codeSize);
                for (int rep = 0; rep < 8; ++rep) {
                    ready.arrive_and_wait();
                    for (;;) {
                        const size_t first = next.fetch_add(32, std::memory_order_relaxed);
                        if (first >= inputs.size())
                            break;
                        for (size_t i = first; i < std::min(first + 32, inputs.size()); ++i) {
                            checkpoints[i].restore(*w->vu);
                            w->run(inputs[i], *mem, h.codeSize);
                            if (rep == 0 && (!same(w->vu->state(), expected[i].state) || w->data != expected[i].data ||
                                             w->packets != expected[i].packets)) {
                                if (bad.fetch_add(1) == 0)
                                    std::fprintf(stderr, "checkpoint mismatch run %zu (%u workers)\n", i, n);
                            }
                        }
                    }
                    done.arrive_and_wait();
                }
            });
        double best = 1e30;
        for (int rep = 0; rep < 8; ++rep) {
            next.store(0);
            auto start = std::chrono::steady_clock::now();
            ready.arrive_and_wait();
            done.arrive_and_wait();
            if (rep)
                best = std::min(best, ms(start));
        }
        for (auto& t : threads)
            t.join();
        std::printf("%s{\"count\":%u,\"best_ms\":%.6f,\"speedup_vs_serial\":%.6f,"
                    "\"mismatches\":%zu}",
                    n == 1 ? "" : ",\n", n, best, serialBest / best, bad.load());
        std::fflush(stdout);
        if (bad)
            return 1;
    }
    // A realistic scheduling unit preserves each MSCAL / MSCNT continuation
    // chain on one worker. Also test cold architectural state at MSCAL: geometry
    // equality is a dependency probe, NOT permission to discard final registers.
    std::vector<size_t> groups{0};
    for (size_t i = 1; i < inputs.size(); ++i)
        if (!inputs[i].e.resume)
            groups.push_back(i);
    groups.push_back(inputs.size());
    auto cold = std::make_unique<Worker>();
    size_t geometryBad = 0, stateBad = 0;
    uint32_t differingVf = 0, differingVi = 0;
    for (size_t g = 0; g + 1 < groups.size(); ++g) {
        const size_t first = groups[g];
        cold->vu->reset();
        if (inputs[first].e.resume)
            checkpoints[first].restore(*cold->vu);
        for (size_t i = first; i < groups[g + 1]; ++i) {
            cold->run(inputs[i], *mem, h.codeSize);
            geometryBad += cold->data != expected[i].data || cold->packets != expected[i].packets;
            VU1State normalized = cold->vu->state();
            normalized.cycles = expected[i].state.cycles;
            stateBad += !same(normalized, expected[i].state);
            for (unsigned r = 0; r < 32; ++r)
                if (std::memcmp(normalized.vf[r], expected[i].state.vf[r], 16))
                    differingVf |= 1u << r;
            for (unsigned r = 0; r < 16; ++r)
                if (normalized.vi[r] != expected[i].state.vi[r])
                    differingVi |= 1u << r;
        }
    }
    std::printf("\n],\"groups\":%zu,\"cold_group_probe\":{\"geometry_mismatches\":%zu,"
                "\"state_mismatches_ignoring_absolute_clock\":%zu,\"differing_vf_mask\":%"
                "u,\"differing_vi_mask\":%u},\"grouped_workers\":[\n",
                groups.size() - 1, geometryBad, stateBad, differingVf, differingVi);
    for (unsigned n : {1u, 2u, 4u, 8u}) {
        std::atomic<size_t> next{0}, bad{0};
        std::barrier ready(n + 1), done(n + 1);
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < n; ++t)
            threads.emplace_back([&] {
                auto w = std::make_unique<Worker>();
                checkpoints[0].restore(*w->vu);
                w->run(inputs[0], *mem, h.codeSize);
                for (int rep = 0; rep < 8; ++rep) {
                    ready.arrive_and_wait();
                    for (;;) {
                        const size_t g = next.fetch_add(1, std::memory_order_relaxed);
                        if (g + 1 >= groups.size())
                            break;
                        checkpoints[groups[g]].restore(*w->vu);
                        for (size_t i = groups[g]; i < groups[g + 1]; ++i) {
                            w->run(inputs[i], *mem, h.codeSize);
                            if (!rep && (!same(w->vu->state(), expected[i].state) || w->data != expected[i].data ||
                                         w->packets != expected[i].packets))
                                ++bad;
                        }
                    }
                    done.arrive_and_wait();
                }
            });
        double best = 1e30;
        for (int rep = 0; rep < 8; ++rep) {
            next.store(0);
            auto start = std::chrono::steady_clock::now();
            ready.arrive_and_wait();
            done.arrive_and_wait();
            if (rep)
                best = std::min(best, ms(start));
        }
        for (auto& t : threads)
            t.join();
        std::printf("%s{\"count\":%u,\"best_ms\":%.6f,\"speedup_vs_serial\":%.6f,"
                    "\"mismatches\":%zu}",
                    n == 1 ? "" : ",\n", n, best, serialBest / best, bad.load());
        if (bad)
            return 1;
    }
    std::vector<Vu1Aot::Fn> dispatch(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs);
    std::fill(Vu1Aot::g_dispatch, Vu1Aot::g_dispatch + Vu1Aot::kPairs, &tracePair);
    RegisterReads unionReads;
    size_t groupsWithLiveRegisters = 0, tracingBad = 0;
    for (size_t g = 0; g + 1 < groups.size(); ++g) {
        RegisterReads reads;
        traceReads = &reads;
        checkpoints[groups[g]].restore(*serial->vu);
        for (size_t i = groups[g]; i < groups[g + 1]; ++i) {
            serial->run(inputs[i], *mem, h.codeSize);
            tracingBad += !same(serial->vu->state(), expected[i].state) || serial->data != expected[i].data ||
                          serial->packets != expected[i].packets;
        }
        // An initial partial continuation is deliberately excluded: it needs
        // its recorded predecessor. Count complete MSCAL-led groups only.
        if (!inputs[groups[g]].e.resume) {
            groupsWithLiveRegisters += (reads.liveLo | reads.liveHi | reads.liveVa) != 0;
            unionReads.liveLo |= reads.liveLo;
            unionReads.liveHi |= reads.liveHi;
            unionReads.liveVa |= reads.liveVa;
        }
    }
    std::copy(dispatch.begin(), dispatch.end(), Vu1Aot::g_dispatch);
    size_t suspendedChecks = 0, suspendedBad = 0, activeCheckpoints = 0;
    auto cloned = std::make_unique<Worker>();
    const int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
    const uint32_t budgets[] = {1, 2, 3, 4, 7, 16, 31, 64, 128, 256, 512, 1024};
    for (size_t i = 0; i < std::min(size_t(256), inputs.size()); ++i)
        for (auto budget : budgets) {
            const int rounding = modes[suspendedChecks % 4];
            std::fesetround(rounding);
            checkpoints[i].restore(*serial->vu);
            serial->run(inputs[i], *mem, h.codeSize, budget);
            Checkpoint mid(*serial->vu);
            activeCheckpoints += serial->vu->m_xgkick.active;
            mid.restore(*cloned->vu);
            cloned->data = serial->data;
            serial->packets.clear();
            cloned->packets.clear();
            serial->vu->setPacketTap(&serial->packets);
            cloned->vu->setPacketTap(&cloned->packets);
            // Continue each private scratch image; do not replace it with the
            // recording's pre-run image after the interrupted prefix.
            for (auto* w : {serial.get(), cloned.get()})
                w->vu->resume(mem->getVU1Code(), h.codeSize, w->data.data(), dataSize, w->gs, mem.get(),
                              inputs[i].e.top, inputs[i].e.itop);
            suspendedBad += !same(serial->vu->state(), cloned->vu->state()) || serial->data != cloned->data ||
                            serial->packets != cloned->packets || std::fegetround() != rounding;
            ++suspendedChecks;
        }
    std::fesetround(FE_TONEAREST);
    std::printf("\n],\"register_dependency_trace\":{\"groups_with_live_register_"
                "reads\":%zu,\"vf_lane_mask_lo\":\"%016llx\",\"vf_lane_mask_hi\":"
                "\"%016llx\",\"vi_acc_mask\":\"%08x\",\"instrumented_output_"
                "mismatches\":%zu,\"scope\":\"dynamic VF/VI/ACC reads before "
                "writes; excludes scalar/flag and scratch-memory dependency "
                "proof\"},\"suspended_checkpoint_checks\":%zu,\"suspended_active_"
                "path1_checks\":%zu,\"suspended_checkpoint_mismatches\":%zu,"
                "\"scope\":\"offline compute ceiling; serial oracle seed cost "
                "excluded from worker timings; no live FPS claim\"}\n",
                groupsWithLiveRegisters, (unsigned long long)unionReads.liveLo, (unsigned long long)unionReads.liveHi,
                unionReads.liveVa, tracingBad, suspendedChecks, activeCheckpoints, suspendedBad);
    if (tracingBad || suspendedBad)
        return 1;
}
