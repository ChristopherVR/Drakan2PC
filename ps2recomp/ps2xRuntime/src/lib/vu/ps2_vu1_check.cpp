// Differential test of the VU1 interpreter against the unoptimised reference copy (vu/ref).
// DRAKAN_VUCHECK=1: every microprogram run (MSCAL / MSCNT) is executed by both interpreters from the same state;
// VU data memory, the register state and the PATH1 packets are compared; mismatches are reported on stderr.
#include "ps2_vu1_check.h"

#include "ref/ps2_vu1_ref.h"
#include "ps2_vu1_fast.h"
#include "aot/ps2_vu1_aot.h"
#include "ps2_vu1_func.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "ps2_perf.h"
#include "../drakan_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

namespace {
VU1InterpreterRef& ref() {
    static VU1InterpreterRef r(VU1InterpreterRef::Unit::VU1);
    return r;
}

uint64_t g_runs = 0, g_bad = 0;

bool compareState(const VU1State& a, const VU1State& b, char* why, size_t whySize) {
#define CMP(field)                                                                                                     \
    if (std::memcmp(&a.field, &b.field, sizeof(a.field)) != 0) {                                                       \
        std::snprintf(why, whySize, "state." #field " ref=%llx new=%llx",                                              \
                      (unsigned long long)*reinterpret_cast<const uint64_t*>(&a.field),                                \
                      (unsigned long long)*reinterpret_cast<const uint64_t*>(&b.field));                               \
        return false;                                                                                                  \
    }
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
    // clang-format on
    if (!Vu1Func::enabled()) { // the functional engine keeps neither cycle-exact timing nor status flags
        // clang-format off
        CMP(status)
        CMP(cycles)
        // clang-format on
    }
    // clang-format off
    CMP(ebit)
    CMP(haltAfterDelaySlot)
    CMP(stoppedByD)
    CMP(stoppedByT)
    CMP(branchPending)
    CMP(branchTarget)
    CMP(branchDelay)
    // clang-format on
#undef CMP
    return true;
}
} // namespace

namespace vu1check {
bool enabled() {
    static const bool on = []() {
        const char* e = std::getenv("DRAKAN_VUCHECK");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}

void run(bool resume, VU1Interpreter& vu, uint8_t* code, uint32_t codeSize, uint8_t* data, uint32_t dataSize, GS& gs,
         PS2Memory* memory, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t maxCycles) {
    static std::vector<uint8_t> refData;
    refData.assign(data, data + dataSize);

    VU1InterpreterRef& r = ref();
    r.state() = vu.state();
    std::vector<std::vector<uint8_t>> refPackets, newPackets;
    r.setPacketTap(&refPackets);
    vu.setPacketTap(&newPackets);

    if (resume) {
        r.resume(code, codeSize, refData.data(), dataSize, gs, nullptr, top, itop, maxCycles);
        vu.resume(code, codeSize, data, dataSize, gs, memory, top, itop, maxCycles);
    } else {
        r.execute(code, codeSize, refData.data(), dataSize, gs, nullptr, startPC, top, itop, maxCycles);
        vu.execute(code, codeSize, data, dataSize, gs, memory, startPC, top, itop, maxCycles);
    }
    vu.setPacketTap(nullptr);

    ++g_runs;
    if (g_runs <= 40 && std::getenv("DRAKAN_VUCHECK_VERBOSE"))
        std::fprintf(stderr,
                     "[vucheck] run %llu %s start=0x%x: cycles ref=%llu new=%llu pc ref=%x new=%x D=%d/%d T=%d/%d\n",
                     static_cast<unsigned long long>(g_runs), resume ? "mscnt" : "mscal", startPC,
                     static_cast<unsigned long long>(r.state().cycles),
                     static_cast<unsigned long long>(vu.state().cycles), r.state().pc, vu.state().pc,
                     r.state().stoppedByD, vu.state().stoppedByD, r.state().stoppedByT, vu.state().stoppedByT);
    char why[128] = {};
    bool ok = true;
    if (!compareState(r.state(), vu.state(), why, sizeof(why)))
        ok = false;
    else if (std::memcmp(refData.data(), data, dataSize) != 0) {
        size_t at = 0;
        while (at < dataSize && refData[at] == data[at])
            ++at;
        std::snprintf(why, sizeof(why), "vu data memory @0x%zx", at);
        ok = false;
    } else if (refPackets.size() != newPackets.size()) {
        std::snprintf(why, sizeof(why), "packet count %zu vs %zu", refPackets.size(), newPackets.size());
        ok = false;
    } else {
        for (size_t i = 0; i < refPackets.size(); ++i)
            if (refPackets[i] != newPackets[i]) {
                std::snprintf(why, sizeof(why), "packet %zu differs", i);
                ok = false;
                break;
            }
    }
    if (!ok) {
        ++g_bad;
        if (g_bad <= 20)
            std::fprintf(stderr, "[vucheck] MISMATCH run %llu (%s start=0x%x): %s\n",
                         static_cast<unsigned long long>(g_runs), resume ? "mscnt" : "mscal", startPC, why);
        // resynchronise the reference with the real interpreter so one bug is not reported thousands of times
        r.state() = vu.state();
    } else if ((g_runs & 0x3FFF) == 0)
        std::fprintf(stderr, "[vucheck] %llu runs checked, %llu mismatches\n", static_cast<unsigned long long>(g_runs),
                     static_cast<unsigned long long>(g_bad));
}

void report() {
    std::fprintf(stderr, "[vucheck] total: %llu runs checked, %llu mismatches\n",
                 static_cast<unsigned long long>(g_runs), static_cast<unsigned long long>(g_bad));
}
} // namespace vu1check

// ---------------------------------------------------------------------------------------------------------------------------
// Recording and offline benchmark of VU1 runs
// ---------------------------------------------------------------------------------------------------------------------------
namespace {
constexpr uint32_t kRecMagic = 0x31525556u; // "VUR1"
constexpr uint32_t kDataSize = 16384u;

struct RecHeader {
    uint32_t magic, count, codeSize, stateSize;
};
struct RecEntry {
    uint32_t resume, startPC, top, itop, dEn, tEn;
};

struct Recorder {
    bool on = false;
    FILE* f = nullptr;
    uint32_t skip = 0, count = 0, seen = 0, written = 0;
    bool headerDone = false;
    Recorder() {
        const char* e = std::getenv("DRAKAN_VU_RECORD");
        if (!e || !*e)
            return;
        std::string spec = e;
        std::string path = spec;
        skip = 2000;
        count = 3000;
        const size_t c = spec.find(',');
        if (c != std::string::npos) {
            path = spec.substr(0, c);
            skip = static_cast<uint32_t>(std::atoi(spec.c_str() + c + 1));
            const size_t c2 = spec.find(',', c + 1);
            if (c2 != std::string::npos)
                count = static_cast<uint32_t>(std::atoi(spec.c_str() + c2 + 1));
        }
        f = std::fopen(path.c_str(), "wb");
        on = f != nullptr;
        if (on)
            std::fprintf(stderr, "[vurec] recording %u VU1 runs after %u into %s\n", count, skip, path.c_str());
    }
};
Recorder& rec() {
    static Recorder r;
    return r;
}
} // namespace

namespace vu1check {
bool recording() {
    return rec().on;
}

void record(bool resume, uint32_t startPC, uint32_t top, uint32_t itop, bool dEn, bool tEn, const VU1Interpreter& vu,
            const uint8_t* code, uint32_t codeSize, const uint8_t* data, uint32_t dataSize) {
    Recorder& r = rec();
    if (!r.on || dataSize != kDataSize)
        return;
    if (r.seen++ < r.skip)
        return;
    if (!r.headerDone) {
        RecHeader h{kRecMagic, r.count, codeSize, static_cast<uint32_t>(sizeof(VU1State))};
        std::fwrite(&h, sizeof(h), 1, r.f);
        std::fwrite(code, 1, codeSize, r.f);
        std::fwrite(&vu.state(), sizeof(VU1State), 1, r.f);
        r.headerDone = true;
    }
    RecEntry e{resume ? 1u : 0u, startPC, top, itop, dEn ? 1u : 0u, tEn ? 1u : 0u};
    std::fwrite(&e, sizeof(e), 1, r.f);
    std::fwrite(data, 1, dataSize, r.f);
    if (++r.written >= r.count) {
        std::fclose(r.f);
        r.f = nullptr;
        r.on = false;
        std::fprintf(stderr, "[vurec] done (%u runs)\n", r.written);
    }
}

bool benchRequested() {
    const char* e = std::getenv("DRAKAN_VU_BENCH");
    return e && *e;
}

void bench(PS2Memory& memory, GS& gs) {
    const char* spec = std::getenv("DRAKAN_VU_BENCH");
    if (!spec || !*spec)
        return;
    std::string path = spec;
    int reps = 5;
    if (const size_t c = path.find(','); c != std::string::npos) {
        reps = std::atoi(path.c_str() + c + 1);
        path = path.substr(0, c);
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "[vubench] cannot open %s\n", path.c_str());
        std::exit(2);
    }
    RecHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1 || h.magic != kRecMagic || h.stateSize != sizeof(VU1State)) {
        std::fprintf(stderr, "[vubench] bad recording (state size %u vs %zu)\n", h.stateSize, sizeof(VU1State));
        std::exit(2);
    }
    std::vector<uint8_t> code(h.codeSize);
    VU1State init{};
    if (std::fread(code.data(), 1, h.codeSize, f) != h.codeSize || std::fread(&init, sizeof(init), 1, f) != 1)
        std::exit(2);
    struct Run {
        RecEntry e;
        std::vector<uint8_t> data;
    };
    std::vector<Run> runs;
    for (uint32_t i = 0; i < h.count; ++i) {
        Run r;
        r.data.resize(kDataSize);
        if (std::fread(&r.e, sizeof(r.e), 1, f) != 1 || std::fread(r.data.data(), 1, kDataSize, f) != kDataSize)
            break;
        runs.push_back(std::move(r));
    }
    std::fclose(f);
    std::fprintf(stderr, "[vubench] %zu runs loaded\n", runs.size());

    // install the microcode where the interpreter expects it (the decoded-pair cache is keyed on the PS2Memory's VU1
    // code)
    for (uint32_t off = 0; off + 4 <= h.codeSize; off += 4) {
        uint32_t w;
        std::memcpy(&w, code.data() + off, 4);
        memory.write32(PS2_VU1_CODE_BASE + off, w);
    }
    uint8_t* vcode = memory.getVU1Code();
    uint8_t* vdata = memory.getVU1Data();

    static VU1Interpreter nv(VU1Interpreter::Unit::VU1);
    VU1InterpreterRef& rv = ref();
    Vu1Fast::spare(nv).sinkNone = 0;
    std::vector<uint8_t> refData(kDataSize);

    auto runOne = [&](VU1Interpreter& v, const Run& r, uint8_t* d) {
        v.state().dBitEnabled = r.e.dEn != 0;
        v.state().tBitEnabled = r.e.tEn != 0;
        if (r.e.resume)
            v.resume(vcode, h.codeSize, d, kDataSize, gs, &memory, r.e.top, r.e.itop, 65536);
        else
            v.execute(vcode, h.codeSize, d, kDataSize, gs, &memory, r.e.startPC, r.e.top, r.e.itop, 65536);
    };

    // 1. correctness: new vs reference, all runs (state, VU data memory, XGKICK packets)
    uint64_t bad = 0;
    if (!std::getenv("DRAKAN_VU_BENCH_NOCHECK")) {
        nv.reset();
        nv.state() = init;
        rv.reset();
        rv.state() = init;
        for (size_t i = 0; i < runs.size(); ++i) {
            const Run& r = runs[i];
            std::memcpy(vdata, r.data.data(), kDataSize);
            std::memcpy(refData.data(), r.data.data(), kDataSize);
            std::vector<std::vector<uint8_t>> pn, pr;
            nv.setPacketTap(&pn);
            rv.setPacketTap(&pr);
            rv.state().dBitEnabled = r.e.dEn != 0;
            rv.state().tBitEnabled = r.e.tEn != 0;
            if (r.e.resume)
                rv.resume(vcode, h.codeSize, refData.data(), kDataSize, gs, nullptr, r.e.top, r.e.itop, 65536);
            else
                rv.execute(vcode, h.codeSize, refData.data(), kDataSize, gs, nullptr, r.e.startPC, r.e.top, r.e.itop,
                           65536);
            runOne(nv, r, vdata);
            nv.setPacketTap(nullptr);
            char why[128] = {};
            bool ok = compareState(rv.state(), nv.state(), why, sizeof(why));
            if (ok && std::memcmp(refData.data(), vdata, kDataSize) != 0) {
                size_t at = 0;
                while (refData[at] == vdata[at])
                    ++at;
                std::snprintf(why, sizeof(why), "vu data @0x%zx", at);
                ok = false;
            }
            if (ok && pn != pr) {
                std::snprintf(why, sizeof(why), "packets differ (%zu vs %zu)", pn.size(), pr.size());
                ok = false;
            }
            if (!ok) {
                if (++bad <= 10)
                    std::fprintf(stderr, "[vubench] MISMATCH run %zu (%s pc=0x%x): %s\n", i,
                                 r.e.resume ? "mscnt" : "mscal", r.e.startPC, why);
                rv.state() = nv.state();
            }
        }
        std::fprintf(stderr, "[vubench] check: %zu runs, %llu mismatches\n", runs.size(),
                     static_cast<unsigned long long>(bad));
    }

    const auto profileStart = Vu1Fast::spare(nv);

    // 2. speed
    if (std::getenv("PS2X_HOSTPROF"))
        ps2perf::start(nullptr, nullptr, nullptr); // host sampler on the benchmark loop
    Vu1Fast::spare(nv).sinkNone = 1;
    double best = 1e30;
    uint64_t cycles = 0, pairs = 0;
    for (int rep = 0; rep < reps; ++rep) {
        nv.reset();
        nv.state() = init;
        const uint64_t pairs0 = ps2perf::g_vu1Pairs.load();
        const uint64_t cyc0 = nv.totalCycles();
        double ns = 0;
        for (const Run& r : runs) {
            std::memcpy(vdata, r.data.data(), kDataSize);
            const auto t0 = std::chrono::steady_clock::now();
            runOne(nv, r, vdata);
            ns += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        }
        cycles = nv.totalCycles() - cyc0;
        pairs = ps2perf::g_vu1Pairs.load() - pairs0;
        best = std::min(best, ns);
    }
    if (Vu1Aot::g_blockStats) {
        const Vu1Fast::Spare& sp = Vu1Fast::spare(nv);
        const double r0 = std::max(1, reps);
        std::fprintf(stderr,
                     "[vubench] block fallbacks per pass: xgkick %.0f, xgkick+other %.0f, Q %.0f, other-pending %.0f, "
                     "state %.0f, budget %.0f, read-conflict %.0f, write-conflict %.0f, coarse-false-positive %.0f\n",
                     (sp.why[0] - profileStart.why[0]) / r0, (sp.why[1] - profileStart.why[1]) / r0,
                     (sp.why[2] - profileStart.why[2]) / r0, (sp.why[3] - profileStart.why[3]) / r0,
                     (sp.why[4] - profileStart.why[4]) / r0, (sp.why[5] - profileStart.why[5]) / r0,
                     (sp.why[6] - profileStart.why[6]) / r0, (sp.why[7] - profileStart.why[7]) / r0,
                     (sp.why[8] - profileStart.why[8]) / r0);
    }
    if (Vu1Aot::g_blockStats) {
        const Vu1Fast::Spare& sp = Vu1Fast::spare(nv);
        const double r = std::max(1, reps);
        std::fprintf(stderr,
                     "[vubench] blocks: %.0f fast, %.0f exact-fallback per pass; %.0f of %llu pairs in fast blocks\n",
                     (sp.blkFast - profileStart.blkFast) / r, (sp.blkSlow - profileStart.blkSlow) / r,
                     (sp.blkPairs - profileStart.blkPairs) / r, static_cast<unsigned long long>(pairs));
    }
    std::fprintf(stderr,
                 "[vubench] %zu runs: %.2f ms (best of %d), %.1f us/run, %llu cycles (%.1f Mcyc/s), %llu pairs (%.1f "
                 "ns/pair, %.1f Mpairs/s)\n",
                 runs.size(), best * 1e-6, reps, best * 1e-3 / runs.size(), static_cast<unsigned long long>(cycles),
                 cycles * 1e3 / best, static_cast<unsigned long long>(pairs), best / std::max<uint64_t>(pairs, 1),
                 pairs * 1e3 / best);
    std::fflush(stderr);
    ps2perf::stop();
    drakan_log::flush();
    std::exit(bad ? 1 : 0);
}
} // namespace vu1check
