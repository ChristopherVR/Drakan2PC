#include "drakan_object_motion.h"
#include "ps2_vu1_batch_capture.h"
#include "drakan_scene_models.h"
#include "runtime/object_motion_capture.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <array>

namespace drakan_object_motion {
namespace {
Tracker tracker;
PS2Runtime* installed = nullptr;
Sink output = nullptr;
void* outputUser = nullptr;
std::atomic<bool> enabled{false}, needReset{true};
std::map<int, Scope> active;
std::map<int, bool> finalSend;
thread_local Cursor cursor;
PS2Runtime::RecompiledFunction origFrame = nullptr, origRigid = nullptr, origCore = nullptr, origIndexed = nullptr,
                               origSequential = nullptr, origSend = nullptr, origFree = nullptr, origEnd = nullptr;
PS2Runtime::RecompiledFunction origConstruct = nullptr, origDestroy = nullptr;
PS2Runtime::RecompiledFunction origBuffer = nullptr;
PS2Runtime::RecompiledFunction origInline = nullptr;
PS2Runtime::RecompiledFunction origCpu = nullptr;
enum Diag {
    DOwner,
    DRa,
    DSp,
    DRange,
    DVtable,
    DPartCall,
    DNoOwner,
    DMesh,
    DState,
    DScratch,
    DBuffer,
    DPart,
    DPartStable,
    DPartEligible,
    DPartRejected,
    DAnimated,
    DInlineCall,
    DInlineGroup,
    DCpu,
    DChainOwner,
    DChainMissing,
    DJobs,
    DSidecars,
    DMarkers,
    DReplay,
    DValid,
    DStable,
    DEligible,
    DReset,
    DClipped,
    DAmbiguous,
    DRejected,
    DNoObject,
    DForward,
    DCount
};
std::array<std::atomic<uint64_t>, DCount> diagnostics{};
std::array<std::atomic<unsigned>, 6> diagnosticSamples{};
bool diagOn() {
    static const bool value = [] {
        const char* s = std::getenv("DRAKAN_OBJECT_MOTION_DIAG");
        return s && *s && std::strcmp(s, "0");
    }();
    return value;
}
void bump(Diag d, uint64_t n = 1) {
    if (diagOn())
        diagnostics[d].fetch_add(n, std::memory_order_relaxed);
}
bool sample(unsigned bucket) {
    return diagOn() && diagnosticSamples[bucket].fetch_add(1, std::memory_order_relaxed) < 4;
}
bool eligible(const Scope& p) {
    const uint32_t required = Scope::Valid | Scope::TopologyStable;
    return (p.flags & required) == required &&
           !(p.flags & (Scope::Reset | Scope::Clipped | Scope::Ambiguous | Scope::Rejected));
}
void diagPacket(const Scope& p, bool replay) {
    if (!sample((replay ? 2u : 0u) + (eligible(p) ? 0u : 1u)))
        return;
    std::fprintf(stderr,
                 "[object-motion-diag-sample] stage=%s epoch=%llu scene=%llu job=%llu owner=%08x gen=%llu mesh=%08x "
                 "part=%08x pc=%x flags=%x eligible=%u hash=%llx vertices=%u indices=%u\n",
                 replay ? "replay" : "capture", (unsigned long long)p.frameEpoch, (unsigned long long)p.sceneSerial,
                 (unsigned long long)p.submissionSerial, p.objectAddress, (unsigned long long)p.generation,
                 p.meshAddress, p.partAddress, p.sourcePc, p.flags, unsigned(eligible(p)),
                 (unsigned long long)p.topologyHash, p.vertexCount, p.indexCount);
}
void diagPart(const Scope& p) {
    bump(DPart);
    if (p.flags & Scope::TopologyStable)
        bump(DPartStable);
    if (p.flags & Scope::Rejected)
        bump(DPartRejected);
    if (eligible(p))
        bump(DPartEligible);
    if (p.flags & Scope::Animated)
        bump(DAnimated);
    diagPacket(p, false);
}
void diagSummary() {
    if (!diagOn())
        return;
    static unsigned lines = 0, populated = 0;
    static uint64_t previousActivity = 0;
    if (lines >= 20)
        return;
    const uint64_t activity = diagnostics[DOwner].load() + diagnostics[DPartCall].load() + diagnostics[DCpu].load();
    const bool changed = activity != previousActivity;
    previousActivity = activity;
    if (changed)
        ++populated;
    if (!(changed && populated <= 16) && tracker.frameEpoch() > 2 && tracker.frameEpoch() % 120)
        return;
    ++lines;
    static const char* names[] = {
        "owner",           "raReject",       "spReject",       "rangeReject",    "vtableReject", "partCall",
        "noOwner",         "meshReject",     "stateReject",    "scratchState",   "bufferReject", "parts",
        "partStable",      "partEligible",   "partRejected",   "animated",       "inlineCall",   "inlineGroup",
        "cpuUnsupported",  "chainOwner",     "chainMissing",   "jobs",           "sidecars",     "markers",
        "replayBegin",     "replayValid",    "replayStable",   "replayEligible", "replayReset",  "replayClipped",
        "replayAmbiguous", "replayRejected", "replayNoObject", "forwardBegin"};
    static_assert(sizeof(names) / sizeof(names[0]) == DCount);
    // Cumulative atomics: includes worker progress; no unbounded per-object log.
    char line[2048];
    size_t used = std::snprintf(line, sizeof line, "[object-motion-diag] epoch=%llu totals",
                                (unsigned long long)tracker.frameEpoch());
    for (unsigned i = 0; i < DCount && used < sizeof line; ++i) {
        const int n = std::snprintf(line + used, sizeof line - used, " %s=%llu", names[i],
                                    (unsigned long long)diagnostics[i].load(std::memory_order_relaxed));
        if (n < 0 || size_t(n) >= sizeof line - used)
            break;
        used += size_t(n);
    }
    std::fprintf(stderr, "%s\n", line);
}
bool valid(uint32_t a, uint32_t bytes) {
    return a >= 0x100000u && bytes <= PS2_RAM_SIZE && a <= PS2_RAM_SIZE - bytes;
}
uint32_t phys(uint32_t a) {
    return a & 0x1fffffffu;
}
uint32_t u32(const uint8_t* ram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, ram + phys(a), 4);
    return v;
}
int tid(PS2Runtime* rt) {
    return rt->eeScheduler().currentThreadId();
}
bool on() {
    if (!enabled.load(std::memory_order_acquire))
        return false;
    if (needReset.exchange(false)) {
        tracker.reset();
        active.clear();
        finalSend.clear();
    }
    return true;
}
void trace(const Scope& p, const char* where) {
    static const bool log = std::getenv("DRAKAN_OBJECT_MOTION_LOG") != nullptr;
    if (log)
        std::fprintf(stderr,
                     "[object-motion] %s epoch=%llu scene=%llu job=%llu object=%08x generation=%llu mesh=%08x "
                     "part=%08x pass=%x hash=%llx flags=%x\n",
                     where, (unsigned long long)p.frameEpoch, (unsigned long long)p.sceneSerial,
                     (unsigned long long)p.submissionSerial, p.objectAddress, (unsigned long long)p.generation,
                     p.meshAddress, p.partAddress, p.passKey, (unsigned long long)p.topologyHash, p.flags);
}
void object(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t expectedRa, uint32_t stackOffset, uint32_t pc) {
    const int id = tid(rt);
    active.erase(id);
    finalSend.erase(id);
    if (GPR_U32(c, 31) != expectedRa) {
        bump(DRa);
        if (sample(4))
            std::fprintf(stderr, "[object-motion-diag-owner] pc=%x ra=%x expected=%x reason=caller\n", pc,
                         GPR_U32(c, 31), expectedRa);
        return;
    }
    const uint32_t sp = phys(GPR_U32(c, 29));
    if (!valid(sp, stackOffset + 4)) {
        bump(DSp);
        return;
    }
    const uint32_t obj = phys(u32(ram, sp + stackOffset));
    const uint32_t mesh = phys(GPR_U32(c, 5));
    // Proven call chain:189b38 s2 -> caller SP+38/10 -> wrapper object.
    // 178d98 constructs 0x120-byte instances, including slots in larger pools;
    // 178f28 destroys them. A malloc header immediately before obj is NOT valid.
    if (!valid(obj, 0x120) || !valid(mesh, 0x20)) {
        bump(DRange);
        return;
    }
    const uint32_t vt = phys(u32(ram, obj));
    if (vt != 0x51b8b0u) {
        bump(DVtable);
        if (sample(4))
            std::fprintf(stderr, "[object-motion-diag-owner] pc=%x ra=%x sp=%x owner=%x vt=%x mesh=%x\n", pc,
                         GPR_U32(c, 31), sp, obj, vt, mesh);
        return;
    }
    Scope p = tracker.object(obj, obj, 0x120, vt, mesh, pc);
    active[id] = p;
    bump(DOwner);
    trace(p, "engine-object");
}
void part(uint8_t* ram, R5900Context* c, PS2Runtime* rt, uint32_t pc) {
    bump(DPartCall);
    const auto it = active.find(tid(rt));
    if (it == active.end()) {
        bump(DNoOwner);
        return;
    }
    const uint32_t renderer = phys(GPR_U32(c, 4)), address = phys(GPR_U32(c, 5));
    if (!valid(renderer, 0x10) || !valid(address, 0x30) || phys(u32(ram, renderer + 8)) != it->second.meshAddress) {
        bump(DMesh);
        return;
    }
    const uint8_t* stateHost =
        resolveState(ram, PS2_RAM_SIZE, rt->memory().getScratchpad(), 0x4000, u32(ram, renderer), 0x80);
    if (!stateHost) {
        bump(DState);
        return;
    }
    if ((u32(ram, renderer) & 0xffffc000u) == 0x70000000u)
        bump(DScratch);
    uint32_t rawTag = 0, rawBase = 0;
    std::memcpy(&rawTag, stateHost + 0x54 + 8, 4);
    std::memcpy(&rawBase, stateHost + 0x54 + 0x1c, 4);
    const uint32_t tag = phys(rawTag), base = phys(rawBase);
    if (!valid(base, 0x50000) || tag < base || tag + 16 > base + 0x50000) {
        bump(DBuffer);
        if (sample(5))
            std::fprintf(stderr, "[object-motion-diag-buffer] pc=%x owner=%x state=%x rawTag=%x rawBase=%x\n", pc,
                         it->second.objectAddress, u32(ram, renderer), rawTag, rawBase);
        return;
    }
    auto topology = inspectInputTopology(ram, PS2_RAM_SIZE, it->second.meshAddress, address, pc == 0x1ec160u);
    Scope owner = it->second;
    const auto pose = provenPoseSource(ram, PS2_RAM_SIZE, owner.objectAddress);
    const uint32_t source = phys(u32(ram, renderer + 0xc));
    if (pose.address && source == pose.address) {
        if (topology.maxPositionIndex > pose.count)
            topology.verified = false;
        else
            owner.flags |= Scope::Animated;
        static const bool log = std::getenv("DRAKAN_OBJECT_MOTION_LOG") != nullptr;
        if (log)
            std::fprintf(
                stderr,
                "[object-motion-pose] epoch=%llu object=%08x controller=%08x positions=%08x count=%u positionHash=%llx "
                "pass=%x\n",
                (unsigned long long)owner.frameEpoch, owner.objectAddress, pose.controller, pose.address, pose.count,
                (unsigned long long)captureHash(14695981039346656037ull, ram + pose.address, pose.count * 12), pc);
    }
    Scope p = tracker.part(owner, address, pc, topology.hash, topology.vertices, topology.indices, topology.verified);
    p.sourcePc = pc;
    tracker.mark(tag, base, 0x50000, p);
    diagPart(p);
    trace(p, "engine-part");
}
void frameHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x152330u && GPR_U32(c, 31) == 0x1392ecu) {
        tracker.nextFrame();
        active.clear();
        finalSend.clear();
    }
    origFrame(r, c, rt);
}
void rigidHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x1485c8u)
        object(r, c, rt, 0x18a700u, 0x38, 0x1485c8);
    origRigid(r, c, rt);
}
void coreHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x1c7410u)
        object(r, c, rt, 0x18a854u, 0x10, 0x1c7410);
    origCore(r, c, rt);
}
void indexedHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x1ec160u)
        part(r, c, rt, 0x1ec160);
    origIndexed(r, c, rt);
}
void sequentialHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x1ec8f0u)
        part(r, c, rt, 0x1ec8f0);
    origSequential(r, c, rt);
}
void inlineHook(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x1ed8f0u && GPR_U32(c, 31) == 0x148710u) {
        bump(DInlineCall);
        const auto it = active.find(tid(rt));
        if (it != active.end()) {
            const Scope owner = it->second;
            static const bool log = std::getenv("DRAKAN_OBJECT_MOTION_LOG") != nullptr;
            captureInlineGroups(ram, PS2_RAM_SIZE, tracker, owner, phys(GPR_U32(c, 5)), log,
                                [&](const Scope& p, uint32_t positions, uint64_t positionHash) {
                                    bump(DInlineGroup);
                                    diagPart(p);
                                    trace(p, "engine-inline-group");
                                    // Changed source bytes are evidence, not proof of skeleton ownership.
                                    if (log)
                                        std::fprintf(stderr,
                                                     "[object-motion-source] epoch=%llu object=%08x group=%08x "
                                                     "positions=%08x positionHash=%llx topologyHash=%llx count=%u\n",
                                                     (unsigned long long)p.frameEpoch, p.objectAddress, p.partAddress,
                                                     positions, (unsigned long long)positionHash,
                                                     (unsigned long long)p.topologyHash, p.vertexCount);
                                });
        }
    }
    origInline(ram, c, rt);
}
void cpuHook(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    // Diagnostic only: this CPU-clipping/direct-GIF path has no immutable
    // per-face transport yet. Never certify its output using a whole-call key.
    if (on() && c->pc == 0x18bb90u) {
        bump(DCpu);
        static const bool log = std::getenv("DRAKAN_OBJECT_MOTION_LOG") != nullptr;
        const uint32_t object = phys(GPR_U32(c, 4));
        if (log && valid(object, 0x120) && u32(ram, object) == 0x51b8b0u) {
            const auto pose = provenPoseSource(ram, PS2_RAM_SIZE, object);
            std::fprintf(stderr,
                         "[object-motion-cpu-unsupported] epoch=%llu owner=%08x resource=%08x first=%u count=%u "
                         "order=%08x faces=%08x caller=%08x pose=%08x\n",
                         (unsigned long long)tracker.frameEpoch(), object, GPR_U32(c, 5), GPR_U32(c, 6), GPR_U32(c, 7),
                         GPR_U32(c, 9), GPR_U32(c, 10), GPR_U32(c, 31), pose.address);
        }
    }
    origCpu(ram, c, rt);
}
void sendHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x1f1420u && GPR_U32(c, 31) == 0x1ed76cu)
        finalSend[tid(rt)] = true;
    origSend(r, c, rt);
}
void freeHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (on() && c->pc == 0x4acc40u) {
        const uint32_t p = phys(GPR_U32(c, 4));
        if (valid(p, 4) && p >= 8) {
            const uint32_t n = u32(r, p - 4) & ~3u;
            tracker.freed(p - 8, valid(p - 8, n) ? n : 0);
        }
    }
    origFree(r, c, rt);
}
void constructHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x178d98u)
        drakan_scene_models::constructed(GPR_U32(c, 4));
    if (on() && c->pc == 0x178d98u)
        tracker.lifetimeBoundary(phys(GPR_U32(c, 4)));
    origConstruct(r, c, rt);
}
void destroyHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == 0x178f28u)
        drakan_scene_models::destroyed(GPR_U32(c, 4));
    if (on() && c->pc == 0x178f28u)
        tracker.lifetimeBoundary(phys(GPR_U32(c, 4)));
    origDestroy(r, c, rt);
}
void bufferHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    // Part builders call 159670 after rewinding the command buffer. Reject all
    // outstanding provenance rather than inherit a prior part in the same scene.
    if (on() && c->pc == 0x159670u)
        tracker.invalidateTags();
    origBuffer(r, c, rt);
}
void endHook(uint8_t* r, R5900Context* c, PS2Runtime* rt) {
    const bool worldEnd = on() && c->pc == 0x147690u && GPR_U32(c, 31) == 0x139344u;
    if (worldEnd) {
        active.clear();
        finalSend.clear();
    }
    origEnd(r, c, rt);
    if (worldEnd)
        diagSummary();
}
void emit(const Scope& p, void*) {
    if (vu1batch::captureActive && vu1batch::captureMotion(p))
        return;
    if (p.flags & Scope::Begin) {
        bump(DReplay);
        if (p.flags & Scope::Valid)
            bump(DValid);
        if (p.flags & Scope::TopologyStable)
            bump(DStable);
        if (eligible(p))
            bump(DEligible);
        if (p.flags & Scope::Reset)
            bump(DReset);
        if (p.flags & Scope::Clipped)
            bump(DClipped);
        if (p.flags & Scope::Ambiguous)
            bump(DAmbiguous);
        if (p.flags & Scope::Rejected)
            bump(DRejected);
        if (!p.objectId)
            bump(DNoObject);
        diagPacket(p, true);
    }
    trace(p, "vif-replay");
    if (output && enabled.load(std::memory_order_relaxed)) {
        if (p.flags & Scope::Begin)
            bump(DForward);
        output(p, outputUser);
    }
}
} // namespace
bool install(PS2Runtime& rt, Sink cb, void* opaque) {
    if (installed)
        return installed == &rt;
    const uint32_t pcs[] = {0x152330, 0x1485c8, 0x1c7410, 0x1ec160, 0x1ec8f0, 0x1f1420, 0x4acc40,
                            0x147690, 0x178d98, 0x178f28, 0x159670, 0x1ed8f0, 0x18bb90};
    for (auto pc : pcs)
        if (!rt.hasFunction(pc))
            return false;
    auto hook = [&](uint32_t pc, PS2Runtime::RecompiledFunction fn, PS2Runtime::RecompiledFunction& orig) {
        orig = rt.lookupFunction(pc);
        rt.replaceFunction(pc, fn);
    };
    hook(pcs[0], frameHook, origFrame);
    hook(pcs[1], rigidHook, origRigid);
    hook(pcs[2], coreHook, origCore);
    hook(pcs[3], indexedHook, origIndexed);
    hook(pcs[4], sequentialHook, origSequential);
    hook(pcs[5], sendHook, origSend);
    hook(pcs[6], freeHook, origFree);
    hook(pcs[7], endHook, origEnd);
    hook(pcs[8], constructHook, origConstruct);
    hook(pcs[9], destroyHook, origDestroy);
    hook(pcs[10], bufferHook, origBuffer);
    hook(pcs[11], inlineHook, origInline);
    hook(pcs[12], cpuHook, origCpu);
    installed = &rt;
    output = cb;
    outputUser = opaque;
    return true;
}
void setEnabled(bool yes) {
    if (enabled.exchange(yes) != yes)
        needReset.store(true);
}
Tracker::Chain beginChain(bool vif1) {
    uint64_t serial = 0;
    if (vif1 && on() && installed) {
        const int id = tid(installed);
        const auto it = active.find(id);
        if (it != active.end())
            serial = it->second.sceneSerial;
        bump(serial ? DChainOwner : DChainMissing);
        if (finalSend[id]) {
            active.erase(id);
            finalSend.erase(id);
        }
    }
    return Tracker::Chain(tracker, serial);
}
VifJob::VifJob(const Job* job) {
    bump(DJobs);
    if (job) {
        bump(DSidecars);
        bump(DMarkers, job->markers.size());
    }
    cursor.start(job);
}
VifJob::VifJob(const Job* job, bool begin, bool end) : close(end) {
    if (begin) {
        bump(DJobs);
        if (job) {
            bump(DSidecars);
            bump(DMarkers, job->markers.size());
        }
        cursor.start(job);
    }
}
VifJob::~VifJob() {
    if (close)
        cursor.close(emit, nullptr);
}
void command(uint32_t offset) {
    cursor.command(offset, emit, nullptr);
}
bool boundary(uint32_t offset) {
    return cursor.boundary(offset);
}
bool tracking() {
    return cursor.tracking();
}
void clipping(bool possible) {
    cursor.clipping(possible, emit, nullptr);
}
void replayCaptured(const Scope& p) {
    emit(p, nullptr);
}
Cursor captureCursor() {
    return cursor;
}
void restoreCursor(const Cursor& c) {
    cursor = c;
}
} // namespace drakan_object_motion
