// Differential test of the VU1 interpreter against the unoptimised reference copy (vu/ref).
// DRAKAN_VUCHECK=1: every microprogram run (MSCAL / MSCNT) is executed by both interpreters from the same state;
// VU data memory, the register state and the PATH1 packets are compared; mismatches are reported on stderr.
#include "ps2_vu1_check.h"

#include "ref/ps2_vu1_ref.h"
#include "ps2_vu1_fast.h"
#include "aot/ps2_vu1_aot.h"
#include "ps2_vu1_func.h"
#include "ps2_vu1_gpu_program.h"
#include "ps2_vu1_gpu_batch.h"
#include "gsr/vertex_program.h"
#include "gsr/vertex_batch.h"
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

// Lanes the VIF wrote since the previous run (quadword | lanes << 16), see noteVifWrite.
std::vector<uint32_t> g_vifWrites;

struct Recorder {
    bool on = false;
    FILE* f = nullptr;
    FILE* writes = nullptr;
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
            writes = std::fopen((path + ".writes").c_str(), "wb");
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
        std::vector<uint32_t> writes;
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

// ---------------------------------------------------------------------------------------------------------------------------
// DRAKAN_VU_BENCH_BATCH: proof of the batch path's vertex-program records (vu/ps2_vu1_gpu_batch.h) in the replay
// ---------------------------------------------------------------------------------------------------------------------------
// The replay restores the recorded VU memory before every run, which hides what a run that is not executed leaves
// undone. This check therefore keeps a memory of its own ("mine") that only receives what the EE wrote between two
// runs (the lanes in which a recorded snapshot differs from the memory the previous run left) and whatever the
// tracker and the runs it lets through write, and a third interpreter that executes those runs on it. Proven per
// recording:
//   1. every vertex stage: the CPU evaluation applied to the snapshot writes the memory the microprogram writes;
//   2. every run the tracker takes: the record's CPU evaluation equals the packets the microprogram sends;
//   3. every run it lets through: executed on "mine", it sends the packets it sends from the recorded snapshot, and
//      "mine" equals the snapshot in every lane below the packet buffer that is not marked stale.
namespace {
using BatchPackets = std::vector<std::vector<uint8_t>>;

// The fields a GS reads from the packets of a run, in order. Packed tags of the microprogram's four registers per
// vertex are reduced to those fields; anything else is kept byte for byte.
void canonicalPackets(const BatchPackets& packets, std::vector<uint32_t>& out) {
    out.clear();
    for (const auto& pk : packets) {
        size_t at = 0;
        while (at + 16 <= pk.size()) {
            uint64_t lo, hi;
            std::memcpy(&lo, pk.data() + at, 8);
            std::memcpy(&hi, pk.data() + at + 8, 8);
            const uint32_t nloop = static_cast<uint32_t>(lo & 0x7FFFu);
            const uint32_t nreg = static_cast<uint32_t>((lo >> 60) & 15u);
            const bool known = ((lo >> 58) & 3u) == 0u && nreg == 4u && (hi & 0xFFFFu) == 0x5A12u &&
                               at + 16 + size_t(nloop) * 64 <= pk.size();
            if (!known) {
                out.push_back(0xFFFFFFFFu);
                for (size_t b = at; b < pk.size(); ++b)
                    out.push_back(pk[b]);
                break;
            }
            out.push_back(0xAAAA0000u | nloop);
            out.push_back(static_cast<uint32_t>((lo >> 15) & 1u));     // EOP
            out.push_back(static_cast<uint32_t>((lo >> 46) & 0xFFFu)); // PRE and PRIM
            for (uint32_t v = 0; v < nloop; ++v) {
                uint32_t w[16];
                std::memcpy(w, pk.data() + at + 16 + size_t(v) * 64, 64);
                const uint32_t fields[12] = {w[0],
                                             w[1],
                                             w[2],
                                             w[4] & 0xFFu,
                                             w[5] & 0xFFu,
                                             w[6] & 0xFFu,
                                             w[7] & 0xFFu,
                                             (w[11] >> 4) & 0xFFu,
                                             w[12] & 0xFFFFu,
                                             w[13] & 0xFFFFu,
                                             w[14],
                                             (w[15] >> 15) & 1u};
                out.insert(out.end(), fields, fields + 12);
            }
            at += 16 + size_t(nloop) * 64;
            if ((lo >> 15) & 1u)
                break;
        }
        out.push_back(0xEEEEEEEEu);
    }
}

struct BatchBench {
    using Tracker = vu1gpu::batch::Tracker;
    using Reason = vu1gpu::batch::Reason;
    using Stage = vu1gpu::batch::Stage;
    using Action = vu1gpu::batch::Action;

    bool on = false, timedSkip = false;
    bool exactWrites = false;                     // the recording has the VIF's writes (<file>.writes)
    std::vector<std::vector<uint32_t>> vifWrites; // per run: quadword | lanes << 16
    uint64_t unreportedLanes = 0;                 // lanes that changed between two runs without a reported write
    uint32_t families = vu1gpu::batch::FamAll;
    Tracker tracker;
    std::vector<uint8_t> mine, postPrev, evalMem, packet;
    bool evalValid = false;
    vu1gpu::batch::VertexRun evalRun;
    Action action = Action::Run;
    Reason why = Reason::None;
    BatchPackets minePackets;
    std::vector<uint32_t> canonA, canonB;
    std::vector<std::vector<uint32_t>> writes; // per run: quadword | lanes << 16, then the four words
    std::vector<uint8_t> runStage, runTaken, runReason;
    FILE* dump = nullptr;

    uint64_t stageProofRuns = 0, stageProofBadRuns = 0, stageProofBadLanes = 0;
    uint64_t stageProofByKind[11] = {}, clipVertexRuns = 0;
    uint64_t recordRuns = 0, emptyTakenBad = 0, recVertices = 0, recBadVertices = 0, recBadFields[5] = {};
    uint64_t recBadShape = 0, recSkipBits = 0, recPerCorner = 0;
    uint64_t recSkipPossible = 0, recProcessed = 0, recFlat = 0, recNotStrip = 0, recBytes = 0;
    uint32_t recMaxPasses = 0;
    uint64_t execRuns = 0, execBadPackets = 0, memBadLanes = 0, memBadRuns = 0, staleLanesExcluded = 0;
    uint64_t eeLanes = 0;
    uint32_t staleShown = 0;
    bool started = false, waiting = true;
    size_t firstRun = 0;
    size_t unreportedRun = ~size_t(0);
    uint32_t unreportedRuns = 0;

    void init(size_t runCount) {
        const char* e = std::getenv("DRAKAN_VU_BENCH_BATCH");
        on = e && *e && e[0] != '0';
        if (!on)
            return;
        timedSkip = e[0] == 's';
        if (const char* f = std::getenv("DRAKAN_VU_BENCH_FAMILIES"); f && *f)
            families = static_cast<uint32_t>(std::strtoul(f, nullptr, 0));
        if (const char* d = std::getenv("DRAKAN_VU_BENCH_BATCH_DUMP"); d && *d)
            dump = std::fopen(d, "wb");
        mine.resize(kDataSize);
        postPrev.resize(kDataSize);
        evalMem.resize(kDataSize);
        writes.resize(runCount);
        runStage.assign(runCount, 0);
        runTaken.assign(runCount, 0);
        runReason.assign(runCount, 0);
    }

    // What the EE wrote between the previous run and this one, applied to `to` and reported to `t`.
    static void applyWrites(const std::vector<uint32_t>& list, uint8_t* to, Tracker& t) {
        for (size_t at = 0; at + 5 <= list.size(); at += 5) {
            const uint32_t qw = list[at] & 0xFFFFu, lanes = list[at] >> 16;
            for (uint32_t l = 0; l < 4u; ++l)
                if ((lanes >> l) & 1u)
                    std::memcpy(to + size_t(qw) * 16u + l * 4u, &list[at + 1 + l], 4);
            t.noteWrite(qw, lanes);
        }
    }

    template <typename RunOne>
    void before(size_t i, bool resume, uint32_t startPc, const uint8_t* snapshot, const VU1Interpreter& nv,
                VU1Interpreter& cv, RunOne&& runMine) {
        using namespace vu1gpu::batch;
        // The registers of the runs before the first microprogram call of the recording are not the recorded ones
        // (the file holds the state at its end), so the proof starts at that call.
        evalValid = false;
        started = started || !resume;
        if (!started) {
            waiting = true;
            return;
        }
        if (waiting) {
            waiting = false;
            firstRun = i;
            std::memcpy(mine.data(), snapshot, kDataSize);
            cv.state() = nv.state();
        } else {
            std::vector<uint32_t>& list = writes[i];
            if (exactWrites) {
                // the lanes the VIF wrote, with the values the snapshot holds
                uint8_t written[kMemory] = {};
                for (uint32_t entry : vifWrites[i])
                    if ((entry & 0xFFFFu) < kMemory)
                        written[entry & 0xFFFFu] |= static_cast<uint8_t>(entry >> 16);
                for (uint32_t qw = 0; qw < kMemory; ++qw) {
                    uint32_t a[4], b[4];
                    std::memcpy(a, snapshot + size_t(qw) * 16u, 16);
                    std::memcpy(b, postPrev.data() + size_t(qw) * 16u, 16);
                    for (uint32_t l = 0; l < 4u; ++l)
                        if (a[l] != b[l] && ((written[qw] >> l) & 1u) == 0u) {
                            if (unreportedRun != i && unreportedRuns++ < 40 && (unreportedRun = i, true))
                                std::fprintf(stderr,
                                             "[vubench] batch WRITE run %zu quadword %u lane %u changed "
                                             "without a reported write\n",
                                             i, qw, l);
                            ++unreportedLanes;
                        }
                    if (written[qw] == 0u)
                        continue;
                    for (uint32_t l = 0; l < 4u; ++l)
                        eeLanes += (written[qw] >> l) & 1u;
                    list.push_back(qw | (uint32_t(written[qw]) << 16));
                    list.insert(list.end(), a, a + 4);
                }
            }
            for (uint32_t qw = 0; qw < kMemory && !exactWrites; ++qw) {
                if (std::memcmp(snapshot + size_t(qw) * 16u, postPrev.data() + size_t(qw) * 16u, 16) == 0)
                    continue;
                uint32_t a[4], b[4], lanes = 0;
                std::memcpy(a, snapshot + size_t(qw) * 16u, 16);
                std::memcpy(b, postPrev.data() + size_t(qw) * 16u, 16);
                for (uint32_t l = 0; l < 4u; ++l)
                    if (a[l] != b[l])
                        lanes |= 1u << l;
                // The recording does not hold the EE's writes, only their effect, and a written lane that happens
                // to keep its value cannot be seen. The EE uploads positions and colours or normals as whole
                // quadwords and texture coordinates as pairs, so a changed lane stands for its group.
                if (qw >= kPos && qw < kSt)
                    lanes = 15u;
                else if (qw >= kSt && qw < kSt + kSlots && (lanes & 3u) != 0u)
                    lanes |= 3u;
                for (uint32_t l = 0; l < 4u; ++l)
                    eeLanes += (lanes >> l) & 1u;
                list.push_back(qw | (lanes << 16));
                list.insert(list.end(), a, a + 4);
            }
            applyWrites(list, mine.data(), tracker);
        }
        // 1. the stages of a vertex run, evaluated on the snapshot
        evalValid = false;
        const uint32_t pcRef = resume ? nv.state().pc : startPc;
        if (resume &&
            (pcRef == kPcAfterSetup || pcRef == kPcAfterVertex || pcRef == kPcAfterList || pcRef == kPcAfterClipList) &&
            word16(snapshot, kQwHeader, 2) <= 0) {
            evalRun = classifyVertexRun(snapshot);
            if (evalRun.why == Reason::ClipCodes)
                ++clipVertexRuns;
            if (evalRun.why == Reason::None) {
                std::memcpy(evalMem.data(), snapshot, kDataSize);
                for (uint32_t k = 0; k < evalRun.count; ++k) {
                    const uint32_t kind = evalRun.kinds[k];
                    const gsr::vp::Pass pass = makePass(kind, evalRun, snapshot, nv.state().vf);
                    const bool grid = isGrid(kind);
                    static GridAxes axes;
                    if (grid)
                        axes.set(pass);
                    const uint16_t wr = writeLanes(kind);
                    ++stageProofByKind[kind];
                    if (kind == gsr::vp::PassGridFlat) {
                        // the flat grid also stores the zero quadword it reads its heights from (a live game's first
                        // such run finds something else there; a recording usually starts after it)
                        const uint32_t zero[4] = {0u, 0u, 0u, gsr::vp::vu::kOne};
                        std::memcpy(evalMem.data() + size_t(47u) * 16u, zero, 16);
                    }
                    for (uint32_t sl = pass.first; sl < pass.first + pass.count; ++sl) {
                        gsr::vp::Row row;
                        loadRow(evalMem.data(), sl, row);
                        const uint32_t cell = sl - pass.first;
                        gsr::vp::applyPass(pass, row, grid ? axes.x[cell % pass.param] : 0u,
                                           grid ? axes.z[cell / pass.param] : 0u);
                        const uint32_t* words[3] = {row.pos, row.col, row.st};
                        const uint32_t base[3] = {kPos, kCol, kSt};
                        for (uint32_t l = 0; l < 12u; ++l)
                            if ((wr >> l) & 1u)
                                std::memcpy(evalMem.data() + size_t(base[l / 4u] + sl) * 16u + size_t(l & 3u) * 4u,
                                            &words[l / 4u][l & 3u], 4);
                    }
                }
                evalValid = true;
            }
        }
        // 2. and 3. the tracker's decision on its own memory
        uint32_t pc = resume ? cv.state().pc : startPc;
        // Not compared below: lanes that are stale before the decision (the run may be about to overwrite them, which
        // the tracker notes at once), and lanes that are pending when a new call starts (they become stale).
        uint16_t pendingAtCall[kSlots] = {};
        for (uint32_t sl = 0; sl < kSlots; ++sl)
            pendingAtCall[sl] = tracker.staleLanes(sl) | (resume ? 0u : tracker.virtualLanes(sl));
        action = tracker.step(resume, pc, cv.state().vf, mine.data(), families, !resume || Vu1Func::canEnter(cv),
                              packet, &why);
        runStage[i] = static_cast<uint8_t>(tracker.lastStage());
        runTaken[i] = action == Action::Skip ? 1 : 0;
        runReason[i] = static_cast<uint8_t>(why);
        minePackets.clear();
        if (why == Reason::StaleInput && staleShown < 6) {
            ++staleShown;
            const IndexRun list = walkIndexList(mine.data());
            std::fprintf(stderr, "[vubench] batch STALE run %zu flags %04x header %d %d %d %d:", i,
                         static_cast<uint16_t>(word16(mine.data(), kQwFlags, 0)), word16(mine.data(), kQwHeader, 0),
                         word16(mine.data(), kQwHeader, 1), word16(mine.data(), kQwHeader, 2),
                         word16(mine.data(), kQwHeader, 3));
            for (uint32_t c = 0; c < list.cornerCount; ++c) {
                const Corner& k = list.corners[c];
                if (tracker.staleLanes(k.slot) | (tracker.staleLanes(k.stSlot) & kLaneStXy) |
                    (tracker.staleLanes(k.colSlot) & kLaneCol))
                    std::fprintf(stderr, " [%u:%03x st %u:%03x col %u:%03x]", k.slot, tracker.staleLanes(k.slot),
                                 k.stSlot, tracker.staleLanes(k.stSlot), k.colSlot, tracker.staleLanes(k.colSlot));
            }
            std::fprintf(stderr, "\n");
        }
        if (action == Action::Skip) {
            cv.state().pc = pc;
            return;
        }
        // the run is executed: "mine" must be the recorded memory wherever the microprogram's content is known
        uint64_t badLanes = 0;
        for (uint32_t qw = 0; qw < kBuffer; ++qw) {
            if (std::memcmp(snapshot + size_t(qw) * 16u, mine.data() + size_t(qw) * 16u, 16) == 0)
                continue;
            uint32_t a[4], b[4], stale = 0;
            std::memcpy(a, snapshot + size_t(qw) * 16u, 16);
            std::memcpy(b, mine.data() + size_t(qw) * 16u, 16);
            if (qw >= kPos && qw < kSt + kSlots)
                stale = ((tracker.staleLanes((qw - kPos) % kSlots) | pendingAtCall[(qw - kPos) % kSlots]) >>
                         (((qw - kPos) / kSlots) * 4u)) &
                        15u;
            for (uint32_t l = 0; l < 4u; ++l) {
                if (a[l] == b[l])
                    continue;
                if ((stale >> l) & 1u)
                    ++staleLanesExcluded;
                else {
                    if (memBadLanes + badLanes < 12)
                        std::fprintf(stderr,
                                     "[vubench] batch MEMORY run %zu quadword %u lane %u: recorded %08x mine %08x\n", i,
                                     qw, l, a[l], b[l]);
                    ++badLanes;
                }
            }
        }
        memBadLanes += badLanes;
        if (badLanes)
            ++memBadRuns;
        cv.setPacketTap(&minePackets);
        runMine(cv, mine.data());
        cv.setPacketTap(nullptr);
        ++execRuns;
    }

    void after(size_t i, const BatchPackets& ref, const uint8_t* post) {
        using namespace vu1gpu::batch;
        if (!started)
            return;
        if (evalValid) {
            ++stageProofRuns;
            uint64_t bad = 0;
            for (uint32_t w = 0; w < kBuffer * 4u; ++w) {
                uint32_t a, b;
                std::memcpy(&a, post + size_t(w) * 4u, 4);
                std::memcpy(&b, evalMem.data() + size_t(w) * 4u, 4);
                if (a != b) {
                    if (stageProofBadLanes + bad < 12)
                        std::fprintf(stderr,
                                     "[vubench] batch STAGE run %zu (first kind %u) quadword %u lane %u: microprogram "
                                     "%08x evaluation %08x\n",
                                     i, evalRun.kinds[0], w / 4u, w & 3u, a, b);
                    ++bad;
                }
            }
            stageProofBadLanes += bad;
            if (bad)
                ++stageProofBadRuns;
        }
        if (action == Action::Skip) {
            if (packet.empty()) {
                if (!ref.empty()) {
                    if (emptyTakenBad < 6)
                        std::fprintf(stderr,
                                     "[vubench] batch RECORD run %zu: taken without a record, the "
                                     "microprogram sent %zu packets\n",
                                     i, ref.size());
                    ++emptyTakenBad;
                }
            } else {
                ++recordRuns;
                gsr::vp::BatchView rec;
                if (!gsr::vp::parseBatch(packet.data() + 16, static_cast<uint32_t>(packet.size() - 16), rec)) {
                    ++recBadShape;
                } else {
                    if (dump) {
                        const uint32_t size = static_cast<uint32_t>(packet.size() - 16);
                        std::fwrite(&size, 4, 1, dump);
                        std::fwrite(packet.data() + 16, 1, size, dump);
                    }
                    if (rec.header.vertexCount == rec.header.indexCount && rec.header.indexCount > 4)
                        ++recPerCorner;
                    // what a backend has to cope with: the skip bit not ruled out by the constants, flat shading,
                    // a primitive other than a triangle strip, the longest chain of passes
                    bool skipPossible = false, projects = false;
                    for (uint32_t k = 0; k < rec.header.passCount; ++k)
                        if (rec.passes[k].kind == gsr::vp::PassTransform ||
                            rec.passes[k].kind == gsr::vp::PassProject) {
                            projects = true;
                            skipPossible = skipPossible || !gsr::vp::passNeverSkips(rec.passes[k]);
                        }
                    recSkipPossible += skipPossible;
                    recProcessed += !projects && rec.header.indexCount != 0u;
                    recFlat += (rec.header.prim & 8u) == 0u;
                    recNotStrip += (rec.header.prim & 7u) != 4u;
                    if (rec.header.passCount > recMaxPasses)
                        recMaxPasses = rec.header.passCount;
                    recBytes += packet.size();
                    BatchPackets made;
                    gsr::vp::batchToGif(rec, made);
                    canonicalPackets(made, canonA);
                    canonicalPackets(ref, canonB);
                    recVertices += rec.header.indexCount;
                    if (canonA != canonB) {
                        // count by vertex and field where the shapes agree
                        bool sameShape = made.size() == ref.size();
                        for (size_t p = 0; sameShape && p < made.size(); ++p)
                            sameShape = made[p].size() == ref[p].size();
                        if (!sameShape) {
                            ++recBadShape;
                            if (recBadShape < 6)
                                std::fprintf(stderr, "[vubench] batch RECORD run %zu: %zu packets against %zu\n", i,
                                             made.size(), ref.size());
                        } else {
                            for (size_t at = 0; at + 12 <= canonA.size() && at + 12 <= canonB.size();) {
                                if ((canonA[at] & 0xFFFF0000u) == 0xAAAA0000u || canonA[at] == 0xEEEEEEEEu) {
                                    const size_t head = canonA[at] == 0xEEEEEEEEu ? 1 : 3;
                                    for (size_t h = 0; h < head; ++h)
                                        if (canonA[at + h] != canonB[at + h])
                                            ++recBadShape;
                                    at += head;
                                    continue;
                                }
                                const uint32_t* a = &canonA[at];
                                const uint32_t* b = &canonB[at];
                                const bool st = a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
                                const bool colour = a[3] != b[3] || a[4] != b[4] || a[5] != b[5] || a[6] != b[6];
                                const bool fog = a[7] != b[7] || a[11] != b[11];
                                const bool position = a[8] != b[8] || a[9] != b[9];
                                const bool depth = a[10] != b[10];
                                if (st || colour || fog || position || depth) {
                                    if (recBadVertices < 8)
                                        std::fprintf(stderr,
                                                     "[vubench] batch RECORD run %zu vertex differs:%s%s%s%s%s made "
                                                     "%08x %08x %08x rgba %u %u %u %u fog %u xy %u %u z %08x adc %u, "
                                                     "microprogram %08x %08x %08x rgba %u %u %u %u fog %u xy %u %u z "
                                                     "%08x adc %u\n",
                                                     i, st ? " st" : "", colour ? " colour" : "", fog ? " fog" : "",
                                                     position ? " position" : "", depth ? " depth" : "", a[0], a[1],
                                                     a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], b[0],
                                                     b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
                                                     b[11]);
                                    ++recBadVertices;
                                    recBadFields[0] += st;
                                    recBadFields[1] += colour;
                                    recBadFields[2] += fog;
                                    recBadFields[3] += position;
                                    recBadFields[4] += depth;
                                }
                                recSkipBits += b[11];
                                at += 12;
                            }
                        }
                    } else {
                        for (size_t at = 0; at < canonB.size();) {
                            if ((canonB[at] & 0xFFFF0000u) == 0xAAAA0000u) {
                                at += 3;
                                continue;
                            }
                            if (canonB[at] == 0xEEEEEEEEu) {
                                ++at;
                                continue;
                            }
                            recSkipBits += canonB[at + 11];
                            at += 12;
                        }
                    }
                }
            }
        } else {
            canonicalPackets(minePackets, canonA);
            canonicalPackets(ref, canonB);
            if (canonA != canonB) {
                if (execBadPackets < 8)
                    std::fprintf(stderr,
                                 "[vubench] batch EXECUTED run %zu: packets differ from the recording's "
                                 "(%zu against %zu)\n",
                                 i, minePackets.size(), ref.size());
                ++execBadPackets;
            }
        }
        std::memcpy(postPrev.data(), post, kDataSize);
    }

    void report(size_t runCount) {
        using namespace vu1gpu::batch;
        const Counters& c = tracker.counters;
        std::fprintf(stderr,
                     "[vubench] batch stages: %llu vertex runs evaluated against the microprogram's memory, %llu "
                     "differ (%llu lanes); stages: grid %llu, flat grid %llu, point light %llu, texgen integers %llu, "
                     "texgen positions %llu, light %llu, transform %llu, fade %llu, texture matrix %llu; %llu vertex "
                     "runs with clip codes not evaluated\n",
                     (unsigned long long)stageProofRuns, (unsigned long long)stageProofBadRuns,
                     (unsigned long long)stageProofBadLanes, (unsigned long long)stageProofByKind[1],
                     (unsigned long long)stageProofByKind[10], (unsigned long long)stageProofByKind[2],
                     (unsigned long long)stageProofByKind[3], (unsigned long long)stageProofByKind[4],
                     (unsigned long long)stageProofByKind[5], (unsigned long long)stageProofByKind[6],
                     (unsigned long long)stageProofByKind[7], (unsigned long long)stageProofByKind[8],
                     (unsigned long long)clipVertexRuns);
        std::fprintf(stderr, "[vubench] batch: proof from run %zu (the first microprogram call) to %zu\n", firstRun,
                     runCount);
        static const char* const stageNames[] = {"set-up", "vertex", "index list", "index list with clip codes",
                                                 "other"};
        for (uint32_t st = 0; st < static_cast<uint32_t>(Stage::Count); ++st)
            std::fprintf(stderr, "[vubench] batch runs, %s: %llu, taken %llu\n", stageNames[st],
                         (unsigned long long)c.runs[st], (unsigned long long)c.taken[st]);
        for (uint32_t r = 1; r < static_cast<uint32_t>(Reason::Count); ++r)
            if (c.declined[r])
                std::fprintf(stderr, "[vubench] batch declined, %s: %llu\n", reasonName(static_cast<Reason>(r)),
                             (unsigned long long)c.declined[r]);
        std::fprintf(stderr,
                     "[vubench] batch records: %llu (%llu with one vertex per index), %llu vertices compared, %llu "
                     "differ (texture coordinate %llu, colour %llu, fog or skip bit %llu, position %llu, depth "
                     "%llu), %llu records or tags of another shape, %llu runs taken without a record although the "
                     "microprogram sent packets, %llu vertices with the skip bit\n",
                     (unsigned long long)recordRuns, (unsigned long long)recPerCorner, (unsigned long long)recVertices,
                     (unsigned long long)recBadVertices, (unsigned long long)recBadFields[0],
                     (unsigned long long)recBadFields[1], (unsigned long long)recBadFields[2],
                     (unsigned long long)recBadFields[3], (unsigned long long)recBadFields[4],
                     (unsigned long long)recBadShape, (unsigned long long)emptyTakenBad,
                     (unsigned long long)recSkipBits);
        std::fprintf(stderr,
                     "[vubench] batch records, for a backend: %llu whose constants do not rule out the skip bit, "
                     "%llu of processed rows only (no transform in the record), %llu flat shaded, %llu not a triangle "
                     "strip, at most %u passes in a record, %llu bytes of records\n",
                     (unsigned long long)recSkipPossible, (unsigned long long)recProcessed, (unsigned long long)recFlat,
                     (unsigned long long)recNotStrip, recMaxPasses, (unsigned long long)recBytes);
        std::fprintf(stderr,
                     "[vubench] batch, runs executed on the tracker's memory: %llu of %zu, %llu sent packets other "
                     "than the recording's; memory before them: %llu runs with a lane other than recorded (%llu "
                     "lanes), %llu stale lanes left out\n",
                     (unsigned long long)execRuns, runCount, (unsigned long long)execBadPackets,
                     (unsigned long long)memBadRuns, (unsigned long long)memBadLanes,
                     (unsigned long long)staleLanesExcluded);
        std::fprintf(stderr,
                     "[vubench] batch tracker: %llu stages deferred (%llu slot stages), %llu slots materialised (%llu "
                     "slot stages), %llu slot stages dropped by EE writes, %llu lanes left stale at set-ups, %llu "
                     "executed runs drawing a stale lane; records carry %llu vertices for %llu indices, %.2f passes "
                     "per record; %llu lanes written by the EE (%s), %llu lanes changed without a reported write\n",
                     (unsigned long long)c.passesDeferred, (unsigned long long)c.slotPassesDeferred,
                     (unsigned long long)c.slotsMaterialised, (unsigned long long)c.slotPassesMaterialised,
                     (unsigned long long)c.passesDropped, (unsigned long long)c.staleLanes,
                     (unsigned long long)c.staleDraws, (unsigned long long)c.recordVertices,
                     (unsigned long long)c.recordIndices, c.records ? double(c.recordPasses) / double(c.records) : 0.0,
                     (unsigned long long)eeLanes,
                     exactWrites ? "as the VIF reported them" : "inferred from the snapshots",
                     (unsigned long long)unreportedLanes);
        if (dump)
            std::fclose(dump);
        dump = nullptr;
    }

    bool failed() const {
        return unreportedLanes || stageProofBadLanes || recBadVertices || recBadShape || emptyTakenBad ||
               execBadPackets || memBadLanes || tracker.counters.staleDraws;
    }
};
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
        sl.writes.swap(g_vifWrites);
        g_vifWrites.clear();
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
                    if (FILE* fw = std::fopen((g.path + ".writes").c_str(), "wb")) {
                        for (uint32_t i = 0; i < g.filled; ++i) {
                            const Ring::Slot& q = g.slots[(first + i) % g.cap];
                            const uint32_t n = static_cast<uint32_t>(q.writes.size());
                            std::fwrite(&n, 4, 1, fw);
                            std::fwrite(q.writes.data(), 4, n, fw);
                        }
                        std::fclose(fw);
                    }
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
    if (!r.on || dataSize != kDataSize) {
        g_vifWrites.clear();
        return;
    }
    if (r.seen++ < r.skip) {
        g_vifWrites.clear();
        return;
    }
    if (r.writes) {
        const uint32_t n = static_cast<uint32_t>(g_vifWrites.size());
        std::fwrite(&n, 4, 1, r.writes);
        std::fwrite(g_vifWrites.data(), 4, n, r.writes);
    }
    g_vifWrites.clear();
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
        if (r.writes)
            std::fclose(r.writes);
        r.writes = nullptr;
        std::fclose(r.f);
        r.f = nullptr;
        r.on = false;
        std::fprintf(stderr, "[vurec] done (%u runs)\n", r.written);
    }
}

void noteVifWrite(uint32_t qw, uint32_t lanes) {
    if ((lanes & 15u) != 0u)
        g_vifWrites.push_back((qw & 0xFFFFu) | ((lanes & 15u) << 16));
}

// ---- DRAKAN_VERTEX_SHADOW: the batch proof of the replay (BatchBench) on the runs of a live game ----
// The game's own VU1 executes every run and draws as always. A second tracker decides, as it would under `records`,
// on a memory of its own that receives only what the VIF reports as written and what the tracker and the runs it lets
// through write; a second engine executes those runs on that memory, with its packets dropped. Checked per run, with
// the counters of BatchBench: every record against the packets the game's VU1 sent for the run, every executed run's
// packets against them, the tracker's memory against the game's before an executed run, the stages of every vertex
// run against the memory the microprogram leaves, and that no lane of the game's VU memory changed between two runs
// without a reported write (which proves the VIF's reports, fast path included, on real play).
namespace {
struct Shadow {
    BatchBench bench;
    VU1Interpreter engine{VU1Interpreter::Unit::VU1};
    std::vector<uint32_t> writes; // since the last run: quadword | lanes << 16
    BatchPackets sent;
    uint64_t runs = 0, lastReport = 0;
    bool ready = false;
};
Shadow& shadow() {
    static Shadow* s = new Shadow; // never destroyed: the VU1 worker may outlive static destruction
    return *s;
}
void shadowReport(Shadow& s) {
    const BatchBench& b = s.bench;
    const vu1gpu::batch::Counters& c = b.tracker.counters;
    using U = unsigned long long;
    std::fprintf(stderr,
                 "[vertex-shadow] runs %llu; records %llu, vertices compared %llu, differing %llu, records of another "
                 "shape %llu, taken without a record but the microprogram sent packets %llu; vertex runs evaluated "
                 "%llu, differing %llu (%llu lanes); runs executed on the shadow memory %llu, with other packets "
                 "%llu, memory before them other than the game's in %llu runs (%llu lanes); lanes changed without a "
                 "reported write %llu; runs drawing an unknown lane %llu; declined:",
                 U(s.runs), U(b.recordRuns), U(b.recVertices), U(b.recBadVertices), U(b.recBadShape),
                 U(b.emptyTakenBad), U(b.stageProofRuns), U(b.stageProofBadRuns), U(b.stageProofBadLanes),
                 U(b.execRuns), U(b.execBadPackets), U(b.memBadRuns), U(b.memBadLanes), U(b.unreportedLanes),
                 U(c.staleDraws));
    for (uint32_t r = 1; r < static_cast<uint32_t>(vu1gpu::batch::Reason::Count); ++r)
        if (c.declined[r])
            std::fprintf(stderr, " %s %llu", vu1gpu::batch::reasonName(static_cast<vu1gpu::batch::Reason>(r)),
                         U(c.declined[r]));
    std::fprintf(stderr, "; taken: vertex runs %llu of %llu, index lists %llu of %llu, with clip codes %llu of %llu\n",
                 U(c.taken[1]), U(c.runs[1]), U(c.taken[2]), U(c.runs[2]), U(c.taken[3]), U(c.runs[3]));
}
} // namespace

bool shadowRequested() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_VERTEX_SHADOW");
        return e && e[0] && e[0] != '0';
    }();
    return on;
}
void shadowWrite(uint32_t qw, uint32_t lanes) {
    if ((lanes & 15u) != 0u)
        shadow().writes.push_back((qw & 0xFFFFu) | ((lanes & 15u) << 16));
}
void shadowBefore(bool resume, uint32_t startPc, VU1Interpreter& vu, uint8_t* code, uint8_t* data, GS& gs,
                  PS2Memory* memory, uint32_t top, uint32_t itop, uint32_t families) {
    Shadow& s = shadow();
    BatchBench& b = s.bench;
    if (!s.ready) {
        s.ready = true;
        b.on = true;
        b.exactWrites = true;
        b.mine.resize(kDataSize);
        b.postPrev.resize(kDataSize);
        b.evalMem.resize(kDataSize);
        b.writes.resize(2);
        b.vifWrites.resize(2);
        b.runStage.assign(2, 0);
        b.runTaken.assign(2, 0);
        b.runReason.assign(2, 0);
        s.engine.reset();
        Vu1Fast::spare(s.engine).sinkNone = 1; // its packets are compared, never drawn
        std::fprintf(stderr, "[vertex-shadow] on: the microprogram draws, records are built and compared beside it\n");
    }
    b.families = families;
    b.vifWrites[1].swap(s.writes);
    s.writes.clear();
    b.writes[1].clear();
    const bool dEn = vu.state().dBitEnabled, tEn = vu.state().tBitEnabled;
    b.before(1, resume, startPc, data, vu, s.engine, [&](VU1Interpreter& v, uint8_t* d) {
        v.state().dBitEnabled = dEn;
        v.state().tBitEnabled = tEn;
        if (resume)
            v.resume(code, PS2_VU1_CODE_SIZE, d, kDataSize, gs, memory, top, itop, 65536);
        else
            v.execute(code, PS2_VU1_CODE_SIZE, d, kDataSize, gs, memory, startPc, top, itop, 65536);
    });
    s.sent.clear();
    vu.setPacketTap(&s.sent);
}
void shadowAfter(VU1Interpreter& vu, const uint8_t* data) {
    Shadow& s = shadow();
    vu.setPacketTap(nullptr);
    s.bench.after(1, s.sent, data);
    ++s.runs;
    if (s.runs - s.lastReport >= 250000u) {
        s.lastReport = s.runs;
        shadowReport(s);
    }
}
void shadowFinish() {
    if (shadowRequested() && shadow().ready)
        shadowReport(shadow());
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
    static VU1Interpreter cv(VU1Interpreter::Unit::VU1); // executes the runs the batch tracker lets through
    Vu1Fast::spare(cv).sinkNone = 0;
    static BatchBench batch;
    batch.init(runs.size());
    if (batch.on) {
        if (FILE* fw = std::fopen((path + ".writes").c_str(), "rb")) {
            batch.vifWrites.resize(runs.size());
            size_t got = 0;
            for (; got < runs.size(); ++got) {
                uint32_t n = 0;
                if (std::fread(&n, 4, 1, fw) != 1 || n > (1u << 20))
                    break;
                batch.vifWrites[got].resize(n);
                if (n && std::fread(batch.vifWrites[got].data(), 4, n, fw) != n)
                    break;
            }
            std::fclose(fw);
            batch.exactWrites = got == runs.size();
            std::fprintf(stderr, "[vubench] batch: VIF writes of %zu runs read%s\n", got,
                         batch.exactWrites ? "" : " (incomplete, not used)");
        }
    }

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
        cv.reset();
        cv.state() = init;
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
            if (batch.on)
                batch.before(i, r.e.resume != 0, r.e.startPC, r.data.data(), nv, cv,
                             [&](VU1Interpreter& v, uint8_t* d) { runOne(v, r, d); });
            runOne(nv, r, vdata);
            nv.setPacketTap(nullptr);
            if (batch.on)
                batch.after(i, pn, vdata);
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
        if (batch.on)
            batch.report(runs.size());
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
            if (batch.on && batch.timedSkip) {
                // the batch tracker decides on a memory of its own; the EE's writes are applied outside the timing
                static BatchBench::Tracker timedTracker;
                static std::vector<uint8_t> timedMemory(kDataSize);
                static std::vector<uint8_t> timedPacket;
                if (i == 0) {
                    timedTracker = BatchBench::Tracker();
                    std::memcpy(timedMemory.data(), r.data.data(), kDataSize);
                } else {
                    BatchBench::applyWrites(batch.writes[i], timedMemory.data(), timedTracker);
                }
                const auto t1 = std::chrono::steady_clock::now();
                uint32_t pc = r.e.resume ? nv.state().pc : r.e.startPC;
                thread_local std::vector<uint8_t> record;
                uint32_t exitPc = 0;
                if (gpuSkip && taken[i] &&
                    vu1gpu::buildStrips(nv.state(), timedMemory.data(), kDataSize, record, exitPc))
                    nv.state().pc = exitPc;
                else if (timedTracker.step(r.e.resume != 0, pc, nv.state().vf, timedMemory.data(), batch.families,
                                           !r.e.resume || Vu1Func::canEnter(nv),
                                           timedPacket) == vu1gpu::batch::Action::Skip)
                    nv.state().pc = pc;
                else
                    runOne(nv, r, timedMemory.data());
                const double one =
                    std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t1).count();
                ns += one;
                if (!facts.empty()) {
                    facts[i].ns = std::min(facts[i].ns, one);
                    facts[i].pairs = ps2perf::g_vu1Pairs.load() - runPairs0;
                }
                continue;
            }
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
            std::fprintf(out, "run,resume,entry_pc,exit_pc,flags,h0,h1,h2,h3,packets,vertices,fan_vertices,pairs,ns,"
                              "strip_taken,batch_taken,batch_reason\n");
            for (size_t i = 0; i < facts.size(); ++i) {
                const RunFacts& ft = facts[i];
                std::fprintf(
                    out, "%zu,%u,0x%x,0x%x,0x%x,%u,%u,%u,%u,%u,%u,%u,%llu,%.0f,%u,%u,%s\n", i, runs[i].e.resume,
                    ft.entryPc, ft.exitPc, ft.flags, ft.header[0], ft.header[1], ft.header[2], ft.header[3], ft.packets,
                    ft.vertices, ft.fanVertices, static_cast<unsigned long long>(ft.pairs), ft.ns,
                    taken.empty() ? 0u : taken[i], batch.on ? batch.runTaken[i] : 0u,
                    batch.on ? vu1gpu::batch::reasonName(static_cast<vu1gpu::batch::Reason>(batch.runReason[i])) : "");
            }
            std::fclose(out);
            std::fprintf(stderr, "[vubench] per-run facts written to %s\n", runsCsv);
        }
    }
    std::fflush(stderr);
    ps2perf::stop();
    drakan_log::flush();
    std::exit(bad || (batch.on && batch.failed()) ? 1 : 0);
}
} // namespace vu1check
