#include "ps2_vu1_gpu_live.h"

#include "../ps2_vu1_batch.h"
#include "gsr/vertex_path.h"
#include "ps2_vu1_check.h"
#include "ps2_vu1_gpu_batch.h"
#include "ps2_vu1_gpu_program.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vu1gpu::live {
std::atomic<bool> g_following{false};

namespace {
namespace vpath = gsr::vpath;

struct Live {
    batch::Tracker tracker;
    const uint8_t* data = nullptr; // the VU data memory the tracker follows
    std::vector<uint8_t> packet;
    bool shadow = false;
    uint64_t staleDrawsSeen = 0;
    uint64_t engaged = 0, rested = 0;        // times the tracker started and stopped following
    uint64_t directAccesses = 0;             // EE accesses to VU1 data memory through its address space
    uint64_t unknownLanes = 0;               // lanes written with a value made from their old content
    uint64_t observedRuns = 0, liveRuns = 0; // runs followed under the microprogram, and under records
    uint64_t materialisedOnLeave = 0;        // slots brought up to date because the mode left `records`
    uint64_t unfollowedRuns = 0;             // runs under a word without a batch stage while the tracker rested
    // followed UNPACKs: count, and processor ticks of the unpack and of the report (slow path: both in unpackTicks)
    uint64_t unpacks = 0, slowUnpacks = 0, unpackTicks = 0, reportTicks = 0, slowTicks = 0;
    uint64_t ticksAtStart = 0;
    std::chrono::steady_clock::time_point timeAtStart{};
};
Live g_live; // owned by the thread that runs the VIF (the VU1 worker); the EE touches it only while that is idle
Live& state() {
    return g_live;
}

vpath::Fallback fallbackOf(batch::Reason why) {
    using batch::Reason;
    switch (why) {
        case Reason::Disabled:
            return vpath::Fallback::StageOff;
        case Reason::EngineState:
            return vpath::Fallback::EnginePending;
        case Reason::ClipCodes:
        case Reason::Clipper:
            return vpath::Fallback::Clipper;
        case Reason::StaleInput:
            return vpath::Fallback::StaleInput;
        default:
            return vpath::Fallback::NotRecognised;
    }
}

bool dispatcherPc(uint32_t pc) {
    return pc == batch::kPcAfterSetup || pc == batch::kPcAfterVertex || pc == batch::kPcAfterList ||
           pc == batch::kPcAfterClipList;
}

// Lanes one vector of an UNPACK writes at write-cycle position `cycle` (0..3): `known` take the EE's data, ROW or
// COL; `unknown` take the lane's old value plus ROW (a field the format does not supply, in an adding mode).
void unpackLanes(uint32_t vn, bool masked, uint32_t mask, uint32_t mode, uint32_t cycle, uint32_t& known,
                 uint32_t& unknown) {
    const uint32_t sourced = vn == 0u ? 15u : (1u << (vn + 1u)) - 1u;
    const bool adds = mode == 1u || mode == 2u;
    known = unknown = 0;
    for (uint32_t field = 0; field < 4u; ++field) {
        const uint32_t spec = masked ? (mask >> ((cycle * 4u + field) * 2u)) & 3u : 0u;
        if (spec == 0u) {
            if ((sourced >> field) & 1u)
                known |= 1u << field;
            else if (adds)
                unknown |= 1u << field;
        } else if (spec != 3u) {
            known |= 1u << field;
        }
    }
}
} // namespace

uint32_t familiesOfStages(uint32_t stages) {
    using namespace batch;
    uint32_t f = 0;
    f |= (stages & vpath::kStrips) ? FamStrips : 0u;
    f |= (stages & vpath::kDepthFade) ? FamFade : 0u;
    f |= (stages & vpath::kTextureMatrix) ? FamTexMatrix : 0u;
    f |= (stages & vpath::kGridLights) ? FamGrid : 0u;
    f |= (stages & vpath::kPointLights) ? FamPointLight : 0u;
    f |= (stages & vpath::kTextureGen) ? FamTexGen : 0u;
    f |= (stages & vpath::kLightTransform) ? FamLightTransform : 0u;
    f |= (stages & vpath::kPrimitives) ? FamPrimitive : 0u;
    f |= (stages & vpath::kClipInside) ? FamClipInside : 0u;
    return f;
}
uint32_t stagesOfFamilies(uint32_t families) {
    using namespace batch;
    uint32_t s = 0;
    s |= (families & FamStrips) ? vpath::kStrips : 0u;
    s |= (families & FamFade) ? vpath::kDepthFade : 0u;
    s |= (families & FamTexMatrix) ? vpath::kTextureMatrix : 0u;
    s |= (families & FamGrid) ? vpath::kGridLights : 0u;
    s |= (families & FamPointLight) ? vpath::kPointLights : 0u;
    s |= (families & FamTexGen) ? vpath::kTextureGen : 0u;
    s |= (families & FamLightTransform) ? vpath::kLightTransform : 0u;
    s |= (families & FamPrimitive) ? vpath::kPrimitives : 0u;
    s |= (families & FamClipInside) ? vpath::kClipInside : 0u;
    return s;
}

Before beforeRun(const Run& run) {
    Live& l = state();
    static const bool shadow = vu1check::shadowRequested();
    if (shadow) {
        // The microprogram runs and draws; the shadow tracker decides on a memory of its own.
        if (vu1check::enabled() || vu1check::recording() || vu1batch::enabled())
            return Before::Execute;
        if (!l.shadow) {
            l.shadow = true;
            l.data = run.data;
            g_following.store(true, std::memory_order_relaxed);
        }
        vu1check::shadowBefore(run.resume, run.startPc, run.vu, run.code, run.data, run.gs, &run.memory, run.top,
                               run.itop, familiesOfStages(vpath::stagesOf(vpath::controlWord())));
        return Before::Shadow;
    }
    const uint32_t word = vpath::controlWord();
    const bool records = vpath::evalOf(word) != vpath::Eval::Microprogram;
    // Whether the word lets a batch stage leave the microprogram. Without one the tracker can take nothing, so a
    // clean tracker (every lane the microprogram's) stays clean whatever runs: it need not follow, and the strip stage
    // is handed over on its own, as before the tracker existed.
    const bool batchStages = records && (familiesOfStages(vpath::stagesOf(word)) & ~batch::FamStrips) != 0u;
    if (!l.data && !batchStages && records && !vu1check::enabled() && !vu1check::recording() && !vu1batch::enabled()) {
        ++l.unfollowedRuns;
        return run.resume && intercept(run.vu, run.data, run.dataSize, run.memory) ? Before::Skipped : Before::Execute;
    }
    const uint32_t pc = run.resume ? run.vu.state().pc : run.startPc;

    // The tracker needs every write into VU memory and every run. The parallel batch executor runs groups on private
    // memories and copies results back, the differential check executes runs itself and a recording must hold the
    // microprogram's memory: with any of them on, batch runs stay in the microprogram and only strips are handed over.
    if (vu1check::enabled() || vu1check::recording() || vu1batch::enabled()) {
        if (!records) {
            vpath::vuRest();
            return Before::Execute;
        }
        if (run.resume && dispatcherPc(pc))
            vpath::countFallback(vpath::Fallback::EnginePending);
        if (run.resume && !vu1check::enabled() && intercept(run.vu, run.data, run.dataSize, run.memory))
            return Before::Skipped;
        return Before::Execute;
    }

    if (!l.data) {
        if (!records) {
            vpath::vuRest();
            return Before::Execute;
        }
        // Start following (a batch stage was switched on): the tracker is in its initial state, or in the clean state
        // it stopped in. Either is true of memory at any run boundary, because nothing was deferred since: the
        // tracker stops only when clean, and while it rests no run leaves the microprogram but the strip stage, which
        // writes nothing a batch stage reads before its own uploads.
        l.data = run.data;
        g_following.store(true, std::memory_order_relaxed);
        if (++l.engaged == 1u) {
            l.ticksAtStart = ticks();
            l.timeAtStart = std::chrono::steady_clock::now();
        }
    }
    if (l.data != run.data) // another VU memory than the one followed: not ours to decide
        return Before::Execute;

    const bool image = Vu1Fast::spare(run.vu).aotValid != 0u && Vu1Func::enabled();
    const bool engineOk = image && (!run.resume || Vu1Func::canEnter(run.vu));
    const uint32_t families = records ? familiesOfStages(vpath::stagesOf(word)) : 0u;
    if (!batchStages) {
        // Leaving `records`, or the batch stages switched off: what was deferred is written before anything
        // executes, also before a new call, which would otherwise drop it and leave the lanes behind as unknown.
        const uint64_t before = l.tracker.counters.slotsMaterialised;
        l.tracker.materialiseAll(run.data);
        l.materialisedOnLeave += l.tracker.counters.slotsMaterialised - before;
        ++(records ? l.liveRuns : l.observedRuns);
    } else {
        ++l.liveRuns;
    }

    batch::Reason why = batch::Reason::None;
    batch::Action action;
    uint32_t next = pc;
    if (image) {
        action = l.tracker.step(run.resume, next, run.vu.state().vf, run.data, families, engineOk, l.packet, &why);
    } else {
        // Micro memory is not the image the stages were read from: the run is not one the tracker knows. It executes
        // on memory brought up to date; the marks stay as they are.
        uint32_t other = 0xFFFFFFFFu;
        action = l.tracker.step(true, other, run.vu.state().vf, run.data, 0u, false, l.packet, nullptr);
        if (records && run.resume)
            why = batch::Reason::EngineState;
    }
    if (l.tracker.counters.staleDraws != l.staleDrawsSeen) {
        vpath::state().counters.staleDraws.fetch_add(l.tracker.counters.staleDraws - l.staleDrawsSeen,
                                                     std::memory_order_relaxed);
        l.staleDrawsSeen = l.tracker.counters.staleDraws;
    }
    if (records && why != batch::Reason::None)
        vpath::countFallback(fallbackOf(why));
    if (action == batch::Action::Skip) {
        run.vu.state().pc = next;
        uint32_t indices = 0;
        if (!l.packet.empty()) {
            run.memory.submitGifPacket(GifPathId::Path1, l.packet.data(), static_cast<uint32_t>(l.packet.size()));
            std::memcpy(&indices, l.packet.data() + 16 + 12, 4); // BatchHeader::indexCount
        }
        vpath::countTakenStages(stagesOfFamilies(l.tracker.lastFamilies()), indices);
        return Before::Skipped;
    }
    // The run executes. The strip stage may still leave as a record of its own.
    if (records && run.resume && image && intercept(run.vu, run.data, run.dataSize, run.memory, &l.tracker))
        return Before::Skipped;
    if (!batchStages && !run.resume && l.tracker.clean()) {
        // Every lane of VU memory is the microprogram's: nothing left to follow. Under a word with strips alone the
        // hook stays called (vuRest keeps the flag) and hands strips over without the tracker.
        l.data = nullptr;
        g_following.store(false, std::memory_order_relaxed);
        ++l.rested;
        vpath::vuRest();
    }
    return Before::Execute;
}

void afterRun(const Run& run) {
    vu1check::shadowAfter(run.vu, run.data);
}

void noteUnpack(const uint8_t* memory, uint32_t address, uint32_t count, uint32_t cl, uint32_t wl, uint32_t vn,
                bool masked, uint32_t mask, uint32_t mode) {
    Live& l = state();
    if (memory != l.data || count == 0u)
        return;
    address &= 1023u;
    mode &= 3u;
    if (!l.shadow) {
        // Most uploads are constants below the three arrays, or index lists above them: nothing to mark. `span`
        // is how far from `address` the unpack reaches (cl >= wl on this path: wl vectors written per cl quadwords).
        const uint32_t span = ((count + wl - 1u) / wl) * cl;
        if (address + span <= batch::kPos || (address >= batch::kSt + batch::kSlots && address + span <= 1024u))
            return;
        if (!masked && (mode == 0u || mode == 3u) && cl == wl && address + count <= 1024u) {
            // the usual upload: consecutive quadwords, the format's lanes in each
            l.tracker.noteWrites(address, count, vn == 0u ? 15u : (1u << (vn + 1u)) - 1u);
            return;
        }
    }
    uint32_t known[4], unknown[4];
    for (uint32_t c = 0; c < 4u; ++c)
        unpackLanes(vn, masked, mask, mode, c, known[c], unknown[c]);
    uint32_t cycle = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t c = cycle > 3u ? 3u : cycle;
        if (l.shadow) {
            vu1check::shadowWrite(address, known[c] | unknown[c]);
        } else {
            l.tracker.noteWrite(address, known[c]);
            if (unknown[c]) {
                l.tracker.noteUnknown(address, unknown[c]);
                ++l.unknownLanes;
            }
        }
        address = (address + 1u) & 1023u;
        if (++cycle == wl) {
            address = (address + cl - wl) & 1023u;
            cycle = 0;
        }
    }
}

void noteWrite(const uint8_t* memory, uint32_t qw, uint32_t lanes, uint32_t unknown) {
    Live& l = state();
    if (memory != l.data)
        return;
    if (l.shadow) {
        vu1check::shadowWrite(qw, lanes | unknown);
        return;
    }
    l.tracker.noteWrite(qw, lanes);
    if (unknown) {
        l.tracker.noteUnknown(qw, unknown);
        ++l.unknownLanes;
    }
}

void unpackTimes(uint64_t unpackTicks, uint64_t reportTicks, bool slow) {
    Live& l = state();
    if (slow) {
        ++l.slowUnpacks;
        l.slowTicks += unpackTicks;
    } else {
        ++l.unpacks;
        l.unpackTicks += unpackTicks;
        l.reportTicks += reportTicks;
    }
}

void directAccess(uint8_t* memory) {
    Live& l = state();
    if (memory != l.data || l.shadow)
        return;
    // The EE is about to read or write VU memory itself: it has to hold what was deferred.
    ++l.directAccesses;
    l.tracker.materialiseAll(memory);
}

void directWrite(const uint8_t* memory, uint32_t offset, uint32_t bytes) {
    Live& l = state();
    if (memory != l.data || bytes == 0u)
        return;
    for (uint32_t lane = offset / 4u; lane <= (offset + bytes - 1u) / 4u; ++lane) {
        const bool whole = lane * 4u >= offset && lane * 4u + 4u <= offset + bytes;
        if (l.shadow)
            vu1check::shadowWrite(lane / 4u, 1u << (lane & 3u));
        else if (whole) // part of a lane leaves the lane what it was: real stays real, unknown stays unknown
            l.tracker.noteWrite(lane / 4u, 1u << (lane & 3u));
    }
}

void countersText(char* text, size_t size) {
    const Live& l = state();
    const batch::Counters& c = l.tracker.counters;
    if (size == 0u)
        return;
    text[0] = 0;
    if (l.engaged == 0u && l.unfollowedRuns == 0u)
        return;
    using U = unsigned long long;
    const auto at = [](batch::Stage s) { return static_cast<uint32_t>(s); };
    // processor ticks to milliseconds, from the two clocks since the tracker first followed
    const uint64_t elapsedTicks = ticks() - l.ticksAtStart;
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l.timeAtStart).count();
    const double msPerTick = elapsedTicks ? elapsedMs / double(elapsedTicks) : 0.0;
    std::snprintf(text, size,
                  "batch tracker: runs followed %llu under records, %llu under the microprogram, %llu not followed "
                  "(no batch stage on); vertex runs %llu "
                  "(deferred %llu), index lists %llu (taken %llu), with clip codes %llu (taken %llu); records %llu "
                  "with %llu vertices; slots written back %llu (%llu when leaving records); lanes left unknown at "
                  "set-ups %llu, runs that drew one %llu; started following %llu times, stopped %llu; EE direct "
                  "accesses %llu, lanes of unknown content written %llu; followed UNPACKs %llu fast (%.3f ms the "
                  "unpack, %.3f ms the report) and %llu slow (%.3f ms)",
                  U(l.liveRuns), U(l.observedRuns), U(l.unfollowedRuns), U(c.runs[at(batch::Stage::Vertex)]),
                  U(c.taken[at(batch::Stage::Vertex)]), U(c.runs[at(batch::Stage::List)]),
                  U(c.taken[at(batch::Stage::List)]), U(c.runs[at(batch::Stage::ClipList)]),
                  U(c.taken[at(batch::Stage::ClipList)]), U(c.records), U(c.recordVertices), U(c.slotsMaterialised),
                  U(l.materialisedOnLeave), U(c.staleLanes), U(c.staleDraws), U(l.engaged), U(l.rested),
                  U(l.directAccesses), U(l.unknownLanes), U(l.unpacks), double(l.unpackTicks) * msPerTick,
                  double(l.reportTicks) * msPerTick, U(l.slowUnpacks), double(l.slowTicks) * msPerTick);
}

} // namespace vu1gpu::live
