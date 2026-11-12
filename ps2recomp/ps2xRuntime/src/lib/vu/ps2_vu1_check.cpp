// Differential test of the VU1 interpreter against the unoptimised reference copy (vu/ref).
// DRAKAN_VUCHECK=1: every microprogram run (MSCAL / MSCNT) is executed by both interpreters from the same state;
// VU data memory, the register state and the PATH1 packets are compared; mismatches are reported on stderr.
#include "ps2_vu1_check.h"

#include "ref/ps2_vu1_ref.h"
#include "ps2_vu1_fast.h"
#include "aot/ps2_vu1_aot.h"
#include "ps2_vu1_func.h"
#include "ps2_vu1_gpu_program.h"
#include "gsr/vertex_program.h"
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

// DRAKAN_VU_RING=<file>,<N>: keep the last N runs in memory and write them to <file> (same format as DRAKAN_VU_RECORD)
// once a trigger file <file>.go appears (the GS creates it when DRAKAN_VU_TRIGGER=1 and a draw has a vertex behind
// the camera). Used to capture the VU1 input of a rare bad frame.
struct Ring {
    bool on = false;
    std::string path;
    uint32_t cap = 0, head = 0, filled = 0, tick = 0;
    struct Slot {
        RecEntry e;
        std::vector<uint8_t> data;
        VU1State state;
    };
    std::vector<Slot> slots;
    Ring() {
        const char* e = std::getenv("DRAKAN_VU_RING");
        if (!e || !*e)
            return;
        std::string spec = e;
        const size_t c = spec.find(',');
        path = spec.substr(0, c);
        cap = c == std::string::npos ? 4096u : static_cast<uint32_t>(std::atoi(spec.c_str() + c + 1));
        if (cap < 16)
            cap = 16;
        slots.resize(cap);
        on = true;
        std::remove((path + ".go").c_str());
        std::fprintf(stderr, "[vuring] keeping the last %u VU1 runs, trigger file %s.go\n", cap, path.c_str());
    }
};
Ring& ring() {
    static Ring r;
    return r;
}
} // namespace

namespace vu1check {
bool recording() {
    return rec().on || ring().on;
}

void record(bool resume, uint32_t startPC, uint32_t top, uint32_t itop, bool dEn, bool tEn, const VU1Interpreter& vu,
            const uint8_t* code, uint32_t codeSize, const uint8_t* data, uint32_t dataSize) {
    if (Ring& g = ring(); g.on && dataSize == kDataSize) {
        Ring::Slot& sl = g.slots[g.head];
        sl.e = RecEntry{resume ? 1u : 0u, resume ? vu.state().pc : startPC, top, itop, dEn ? 1u : 0u, tEn ? 1u : 0u};
        sl.state = vu.state();
        sl.data.assign(data, data + dataSize);
        g.head = (g.head + 1) % g.cap;
        if (g.filled < g.cap)
            ++g.filled;
        if ((++g.tick & 63u) == 0) {
            if (FILE* t = std::fopen((g.path + ".go").c_str(), "rb")) {
                std::fclose(t);
                std::remove((g.path + ".go").c_str());
                if (FILE* f = std::fopen(g.path.c_str(), "wb")) {
                    RecHeader h{kRecMagic, g.filled, codeSize, static_cast<uint32_t>(sizeof(VU1State))};
                    std::fwrite(&h, sizeof(h), 1, f);
                    std::fwrite(code, 1, codeSize, f);
                    std::fwrite(&vu.state(), sizeof(VU1State), 1, f);
                    const uint32_t first = g.filled < g.cap ? 0 : g.head;
                    for (uint32_t i = 0; i < g.filled; ++i) {
                        const Ring::Slot& q = g.slots[(first + i) % g.cap];
                        std::fwrite(&q.e, sizeof(q.e), 1, f);
                        std::fwrite(q.data.data(), 1, kDataSize, f);
                    }
                    std::fclose(f);
                    // entry register state of every run (VU1State), same order, for the offline scanner
                    if (FILE* fs = std::fopen((g.path + ".states").c_str(), "wb")) {
                        for (uint32_t i = 0; i < g.filled; ++i)
                            std::fwrite(&g.slots[(first + i) % g.cap].state, sizeof(VU1State), 1, fs);
                        std::fclose(fs);
                    }
                    std::fprintf(stderr, "[vuring] wrote %u runs to %s\n", g.filled, g.path.c_str());
                }
                g.on = false;
            }
        }
    }
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

bool gpuProgramsEnabled() {
    return vu1gpu::enabled();
}
bool gpuProgramIntercept(VU1Interpreter& vu, const uint8_t* data, uint32_t dataSize, PS2Memory& memory) {
    return vu1gpu::intercept(vu, data, dataSize, memory);
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

    // DRAKAN_VU_BENCH_RUNS=<csv>: one line per recorded run (entry and exit pc, the flag word and batch header of
    // the image's data layout, packets and vertices sent, pairs and best time), for scripts/perf/vu_run_classes.py.
    struct RunFacts {
        uint32_t entryPc = 0, exitPc = 0, flags = 0, packets = 0, vertices = 0, fanVertices = 0;
        uint32_t header[4] = {};
        uint64_t pairs = 0;
        double ns = 1e30;
    };
    const char* runsCsv = std::getenv("DRAKAN_VU_BENCH_RUNS");
    std::vector<RunFacts> facts(runsCsv && *runsCsv ? runs.size() : 0);

    // DRAKAN_VU_BENCH_GPUPROG=1: for every run the vertex-program recogniser accepts (vu/ps2_vu1_gpu_program.h),
    // evaluate its record with the CPU mirror of the shader (gsr/vertex_program.h) and compare every field a GS reads
    // with the packets the engine sent; then replay once more without executing those runs and compare the packets of
    // all other runs (the registers a skipped run would have left must not matter).
    const bool gpuProg = std::getenv("DRAKAN_VU_BENCH_GPUPROG") != nullptr;
    struct GpuProgStats {
        uint64_t runs = 0, vertices = 0, badVertices = 0, badPosition = 0, badDepth = 0, badFog = 0, badColour = 0,
                 badSt = 0, badShape = 0, skipBits = 0, unsafeFog = 0, flatRuns = 0, litRuns = 0;
        uint64_t declined[6] = {}; // by vu1gpu::Decline (0: pipelines pending)
    } gp;
    const char* gpDumpPath = std::getenv("DRAKAN_VU_BENCH_GPUPROG_DUMP");
    FILE* gpDump = gpuProg && gpDumpPath && *gpDumpPath ? std::fopen(gpDumpPath, "wb") : nullptr;
    std::vector<uint64_t> packetHash(gpuProg ? runs.size() : 0);
    std::vector<uint8_t> taken(gpuProg ? runs.size() : 0);
    const auto hashPackets = [](const std::vector<std::vector<uint8_t>>& packets) {
        uint64_t h = 1469598103934665603ull;
        for (const auto& pk : packets) {
            for (uint8_t b : pk)
                h = (h ^ b) * 1099511628211ull;
            h = (h ^ 0xFFu) * 1099511628211ull;
        }
        return h;
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
            if (!facts.empty()) {
                RunFacts& ft = facts[i];
                ft.entryPc = r.e.resume ? nv.state().pc : r.e.startPC;
                std::memcpy(&ft.flags, r.data.data() + 7 * 16, 4);
                std::memcpy(ft.header, r.data.data() + 9 * 16, 16);
            }
            std::vector<uint8_t> gpRecord;
            uint32_t gpExit = 0;
            vu1gpu::Decline gpWhy = vu1gpu::Decline::None;
            const bool gpStage = gpuProg && r.e.resume && nv.state().pc == 0x2A60u;
            const bool gpTaken = gpStage && Vu1Func::canEnter(nv) &&
                                 vu1gpu::buildStrips(nv.state(), vdata, kDataSize, gpRecord, gpExit, &gpWhy);
            if (gpStage && !gpTaken)
                ++gp.declined[static_cast<uint32_t>(gpWhy) < 6u ? static_cast<uint32_t>(gpWhy) : 0u];
            runOne(nv, r, vdata);
            nv.setPacketTap(nullptr);
            if (gpuProg)
                packetHash[i] = hashPackets(pn);
            if (gpTaken) {
                taken[i] = 1;
                ++gp.runs;
                if (gpDump) { // records for runtime/gs/tests/test_vertex_program.cpp (GSR_VP_RECORDS): size, record
                    const uint32_t size = static_cast<uint32_t>(gpRecord.size() - 16);
                    std::fwrite(&size, 4, 1, gpDump);
                    std::fwrite(gpRecord.data() + 16, 1, size, gpDump);
                }
                gsr::vp::View rec;
                if (!gsr::vp::parse(gpRecord.data() + 16, static_cast<uint32_t>(gpRecord.size() - 16), rec) ||
                    rec.header.stripCount != pn.size() || nv.state().pc != gpExit) {
                    ++gp.badShape;
                } else {
                    (rec.header.program == gsr::vp::StripsFlat ? gp.flatRuns : gp.litRuns)++;
                    if (!gsr::vp::neverSkipsVertices(*rec.constants))
                        ++gp.unsafeFog;
                    const gsr::vp::Vertex* in = rec.vertices;
                    for (uint32_t st = 0; st < rec.header.stripCount; ++st) {
                        const std::vector<uint8_t>& pk = pn[st];
                        const uint32_t n = rec.strips[st];
                        uint64_t lo = 0;
                        if (pk.size() >= 8)
                            std::memcpy(&lo, pk.data(), 8);
                        if (pk.size() != 16 + size_t(n) * 64 || (lo & 0x7FFFu) != n ||
                            ((lo >> 47) & 0x7FFu) != rec.header.prim) {
                            ++gp.badShape;
                            in += n;
                            continue;
                        }
                        for (uint32_t k = 0; k < n; ++k, ++in) {
                            const gsr::vp::Output o = gsr::vp::evaluate(rec.header.program, *rec.constants, *in);
                            uint32_t w[16];
                            std::memcpy(w, pk.data() + 16 + size_t(k) * 64, 64);
                            bool ok = true;
                            if (w[0] != o.s || w[1] != o.t || w[2] != o.q) {
                                ++gp.badSt;
                                ok = false;
                            }
                            if ((w[4] & 0xFF) != o.r || (w[5] & 0xFF) != o.g || (w[6] & 0xFF) != o.b ||
                                (w[7] & 0xFF) != o.a) {
                                ++gp.badColour;
                                ok = false;
                            }
                            if (((w[11] >> 4) & 0xFF) != o.fog) {
                                ++gp.badFog;
                                ok = false;
                            }
                            if ((w[12] & 0xFFFF) != o.x || (w[13] & 0xFFFF) != o.y) {
                                ++gp.badPosition;
                                ok = false;
                            }
                            if (w[14] != o.z) {
                                ++gp.badDepth;
                                ok = false;
                            }
                            if (((w[15] >> 15) & 1u) != (o.adc ? 1u : 0u))
                                ok = false;
                            if ((w[15] >> 15) & 1u)
                                ++gp.skipBits;
                            ++gp.vertices;
                            if (!ok)
                                ++gp.badVertices;
                        }
                    }
                }
            }
            if (!facts.empty()) {
                RunFacts& ft = facts[i];
                ft.exitPc = nv.state().pc;
                ft.packets = static_cast<uint32_t>(pn.size());
                for (const auto& pk : pn) {
                    // packed-mode tags only (the image sends nothing else): NLOOP in bits 0..14, PRIM in 47..57
                    for (size_t at = 0; at + 16 <= pk.size();) {
                        uint64_t lo;
                        std::memcpy(&lo, pk.data() + at, 8);
                        const uint32_t nloop = static_cast<uint32_t>(lo & 0x7FFFu);
                        const uint32_t nreg =
                            static_cast<uint32_t>((lo >> 60) & 15u) ? static_cast<uint32_t>((lo >> 60) & 15u) : 16u;
                        ft.vertices += nloop;
                        if (((lo >> 47) & 7u) == 5u)
                            ft.fanVertices += nloop;
                        at += 16 + size_t(nloop) * nreg * 16;
                        if ((lo >> 15) & 1u)
                            break;
                    }
                }
            }
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

    if (gpDump)
        std::fclose(gpDump);
    if (gpuProg) {
        std::fprintf(stderr,
                     "[vubench] vertex programs: %llu of %zu runs taken (%llu constant colour, %llu lit), %llu "
                     "vertices, %llu differ from the engine's packets (position %llu, depth %llu, fog %llu, colour "
                     "%llu, texture coordinate %llu), %llu runs or strips of unexpected shape, %llu vertices with the "
                     "skip bit, %llu runs the constants cannot clear of it\n",
                     static_cast<unsigned long long>(gp.runs), runs.size(),
                     static_cast<unsigned long long>(gp.flatRuns), static_cast<unsigned long long>(gp.litRuns),
                     static_cast<unsigned long long>(gp.vertices), static_cast<unsigned long long>(gp.badVertices),
                     static_cast<unsigned long long>(gp.badPosition), static_cast<unsigned long long>(gp.badDepth),
                     static_cast<unsigned long long>(gp.badFog), static_cast<unsigned long long>(gp.badColour),
                     static_cast<unsigned long long>(gp.badSt), static_cast<unsigned long long>(gp.badShape),
                     static_cast<unsigned long long>(gp.skipBits), static_cast<unsigned long long>(gp.unsafeFog));
        std::fprintf(stderr,
                     "[vubench] vertex programs, strip runs left on the CPU: %llu engine state, %llu tag template, "
                     "%llu first length, %llu strip longer than a packet buffer, %llu outside the vertex window\n",
                     static_cast<unsigned long long>(gp.declined[0]), static_cast<unsigned long long>(gp.declined[2]),
                     static_cast<unsigned long long>(gp.declined[3]), static_cast<unsigned long long>(gp.declined[4]),
                     static_cast<unsigned long long>(gp.declined[5]));
        // second replay: taken runs are not executed
        nv.reset();
        nv.state() = init;
        uint64_t skipped = 0, others = 0, othersBad = 0, lost = 0;
        double nsOthers = 0;
        for (size_t i = 0; i < runs.size(); ++i) {
            const Run& r = runs[i];
            std::memcpy(vdata, r.data.data(), kDataSize);
            std::vector<uint8_t> record;
            uint32_t exitPc = 0;
            if (r.e.resume && Vu1Func::canEnter(nv) &&
                vu1gpu::buildStrips(nv.state(), vdata, kDataSize, record, exitPc)) {
                nv.state().pc = exitPc;
                ++skipped;
                if (!taken[i])
                    ++lost;
                continue;
            }
            if (taken[i])
                ++lost;
            std::vector<std::vector<uint8_t>> pk;
            nv.setPacketTap(&pk);
            const auto t0 = std::chrono::steady_clock::now();
            runOne(nv, r, vdata);
            nsOthers += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
            nv.setPacketTap(nullptr);
            ++others;
            if (hashPackets(pk) != packetHash[i])
                ++othersBad;
        }
        std::fprintf(stderr,
                     "[vubench] vertex programs, replay without the taken runs: %llu skipped, %llu other runs, %llu "
                     "of them sent different packets, %llu runs changed sides\n",
                     static_cast<unsigned long long>(skipped), static_cast<unsigned long long>(others),
                     static_cast<unsigned long long>(othersBad), static_cast<unsigned long long>(lost));
        (void)nsOthers;
    }

    const auto profileStart = Vu1Fast::spare(nv);

    // 2. speed
    if (std::getenv("PS2X_HOSTPROF"))
        ps2perf::start(nullptr, nullptr, nullptr); // host sampler on the benchmark loop
    Vu1Fast::spare(nv).sinkNone = 1;
    const char* gpuProgEnv = std::getenv("DRAKAN_VU_BENCH_GPUPROG");
    const bool gpuSkip = gpuProg && gpuProgEnv[0] == 's';
    if (gpuSkip)
        std::fprintf(stderr, "[vubench] the timed replay builds records for the taken runs instead of running them\n");
    double best = 1e30;
    uint64_t cycles = 0, pairs = 0;
    for (int rep = 0; rep < reps; ++rep) {
        nv.reset();
        nv.state() = init;
        const uint64_t pairs0 = ps2perf::g_vu1Pairs.load();
        const uint64_t cyc0 = nv.totalCycles();
        double ns = 0;
        for (size_t i = 0; i < runs.size(); ++i) {
            const Run& r = runs[i];
            std::memcpy(vdata, r.data.data(), kDataSize);
            const uint64_t runPairs0 = facts.empty() ? 0 : ps2perf::g_vu1Pairs.load();
            const auto t0 = std::chrono::steady_clock::now();
            if (gpuSkip && taken[i]) {
                // what the hook costs instead of the run: building the record
                thread_local std::vector<uint8_t> record;
                uint32_t exitPc = 0;
                if (vu1gpu::buildStrips(nv.state(), vdata, kDataSize, record, exitPc))
                    nv.state().pc = exitPc;
                else
                    runOne(nv, r, vdata);
            } else
                runOne(nv, r, vdata);
            const double one = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
            ns += one;
            if (!facts.empty()) {
                facts[i].ns = std::min(facts[i].ns, one);
                facts[i].pairs = ps2perf::g_vu1Pairs.load() - runPairs0;
            }
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
    if (!facts.empty()) {
        if (FILE* out = std::fopen(runsCsv, "w")) {
            std::fprintf(out, "run,resume,entry_pc,exit_pc,flags,h0,h1,h2,h3,packets,vertices,fan_vertices,pairs,ns\n");
            for (size_t i = 0; i < facts.size(); ++i) {
                const RunFacts& ft = facts[i];
                std::fprintf(out, "%zu,%u,0x%x,0x%x,0x%x,%u,%u,%u,%u,%u,%u,%u,%llu,%.0f\n", i, runs[i].e.resume,
                             ft.entryPc, ft.exitPc, ft.flags, ft.header[0], ft.header[1], ft.header[2], ft.header[3],
                             ft.packets, ft.vertices, ft.fanVertices, static_cast<unsigned long long>(ft.pairs), ft.ns);
            }
            std::fclose(out);
            std::fprintf(stderr, "[vubench] per-run facts written to %s\n", runsCsv);
        }
    }
    std::fflush(stderr);
    ps2perf::stop();
    drakan_log::flush();
    std::exit(bad ? 1 : 0);
}
} // namespace vu1check
