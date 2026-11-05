// Engine lifecycle events (drakan_lifecycle.h). Hooks the guest at Level_Load, the save writer, the save loader and the
// controlled-actor setter. "After" events must survive a guest thread switch inside the hooked call (the loader waits
// on semaphores), so the return address is redirected to a host trampoline that fires them whenever the call finally
// returns. The hook itself fires "before" only on a real entry (c->pc == the function), never on a resume.
#include "runtime/ee_no_preempt.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace drakan_lifecycle {
namespace {

// SCES-500.06. Guest function addresses go through the disc profile; object offsets are PAL layouts.
constexpr uint32_t kLevelLoad = 0x1c94c8u;    // (path char*, flags): 0 on success; flags 0 = fresh, 1 / 3 = from a save
constexpr uint32_t kSaveWrite = 0x169c28u;    // (engine, ?, path char*, buffer): flushes the kill list first, 0 = ok
constexpr uint32_t kSaveRestore = 0x16adb8u;  // (engine, path char*): the loader; calls Level_Load itself
constexpr uint32_t kPadMgrUpdate = 0x1e3970u; // once per frame, before the actor lists are walked
constexpr uint32_t kKillFlush = 0x142e50u;    // (engine)
constexpr uint32_t kKillActor = 0x1413c0u;    // (engine, actor): engine vtable slot 0x51a884
constexpr uint32_t kContextPtr = 0x5565d4u;   // *kContextPtr = engine (0x4de5e0)
constexpr uint32_t kHandleVtable = 0x51b8b0u;
constexpr uint32_t kClassRynn = 0x10009u, kClassArokh = 0x1001cu;

struct Ram {
    uint8_t* base;
    static bool ok(uint32_t a, uint32_t n = 4) {
        const uint32_t p = a & 0x1fffffffu;
        return p >= 0x100000u && p < PS2_RAM_SIZE && n <= PS2_RAM_SIZE - p;
    }
    uint32_t u32(uint32_t a) const {
        uint32_t v = 0;
        if (ok(a))
            std::memcpy(&v, base + (a & 0x1fffffffu), 4);
        return v;
    }
    uint32_t u16(uint32_t a) const {
        uint16_t v = 0;
        if (ok(a, 2))
            std::memcpy(&v, base + (a & 0x1fffffffu), 2);
        return v;
    }
    std::string str(uint32_t a, size_t max = 255) const {
        std::string s;
        if (!ok(a))
            return s;
        for (size_t i = 0; i < max && ok(a + static_cast<uint32_t>(i), 1); ++i) {
            const char ch = static_cast<char>(base[(a & 0x1fffffffu) + i]);
            if (!ch)
                break;
            if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7e)
                return std::string();
            s.push_back(ch);
        }
        return s;
    }
};

uint32_t addr(uint32_t pal) {
    return drakan_region::guestAddress(pal);
}

struct Registry {
    std::mutex m;
    std::vector<std::pair<ListenerId, Listener>> byKind[static_cast<size_t>(Kind::Count)];
    ListenerId next = 1;
};
Registry& registry() {
    static Registry r;
    return r;
}

bool g_installed = false, g_log = false;
uint64_t g_flushCalls = 0; // kill-list flushes (0x142e50) seen; EE thread
Mount g_mount = Mount::None;
std::chrono::steady_clock::time_point g_t0 = std::chrono::steady_clock::now();

double nowS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
}

void fire(Event& e, uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    e.rdram = rdram;
    e.runtime = rt;
    e.ctx = c;
    if (g_log) {
        std::fprintf(
            stderr, "[lifecycle %.3f] %s level='%s' path='%s' fromSave=%d ok=%d result=%d mount=%d->%d flushes=%llu\n",
            nowS(), kindName(e.kind), e.level.c_str(), e.path.c_str(), e.fromSave ? 1 : 0, e.ok ? 1 : 0, e.result,
            static_cast<int>(e.previous), static_cast<int>(e.current), static_cast<unsigned long long>(g_flushCalls));
    }
    std::vector<std::pair<ListenerId, Listener>> copy;
    {
        Registry& r = registry();
        std::lock_guard<std::mutex> l(r.m);
        copy = r.byKind[static_cast<size_t>(e.kind)];
    }
    for (auto& entry : copy)
        entry.second(e);
}

std::string stem(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
        s.resize(dot);
    return s;
}

// --- mount tracking ---------------------------------------------------------------------------------------------

uint32_t engineOf(const Ram& r) {
    const uint32_t p = r.u32(addr(kContextPtr));
    return Ram::ok(p, 0x4b8) ? p : 0;
}

// Class id of the resource record of `handle` (same walk as drakan_controlled_actor.h).
uint32_t classOf(const Ram& r, uint32_t engine, uint32_t handle) {
    if (!Ram::ok(handle, 0x100))
        return 0;
    const uint32_t resources = r.u32(engine + 0x320);
    if (!Ram::ok(resources, 0x14))
        return 0;
    const uint32_t index = r.u16(handle + 0x10);
    if (index == 0xffffu || index > r.u32(resources + 0x10))
        return 0;
    const uint32_t table = r.u32(resources + 8);
    if (!Ram::ok(table, index * 0x24u + 0x24u))
        return 0;
    return r.u32(table + index * 0x24u + 8);
}

Mount mountOf(const Ram& r) {
    const uint32_t engine = engineOf(r);
    if (!engine)
        return Mount::None;
    const uint32_t cls = classOf(r, engine, r.u32(engine + 0x4b0));
    return cls == kClassRynn ? Mount::Rynn : cls == kClassArokh ? Mount::Arokh : Mount::None;
}

// Fires MountChanged for a real Rynn <-> Arokh switch. None (level being rebuilt) and a repeated class never fire; a
// temporary second Rynn body (the engine's free-camera clone, a ghost) keeps the class and is not a change.
void pollMount(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const Ram r{rdram};
    const Mount now = mountOf(r);
    if (now == Mount::None || now == g_mount)
        return;
    const Mount before = g_mount;
    g_mount = now;
    if (before == Mount::None)
        return;
    Event e;
    e.kind = Kind::MountChanged;
    e.previous = before;
    e.current = now;
    e.controlled = r.u32(engineOf(r) + 0x4b0);
    fire(e, rdram, c, rt);
}

// --- hooked calls -----------------------------------------------------------------------------------------------

enum Slot { kSlotLevel, kSlotSave, kSlotRestore, kSlotCount };

struct Pending {
    uint32_t ra = 0;
    std::string path, level;
    bool fromSave = false;
};

struct SlotState {
    uint32_t entry = 0, tramp = 0;
    PS2Runtime::RecompiledFunction orig = nullptr;
    Kind before = Kind::Count, after = Kind::Count;
    std::vector<Pending> pending; // EE game thread only
};
std::array<SlotState, kSlotCount> g_slots;
std::atomic<int> g_active{0}; // calls between their before and after event
std::mutex g_vetoM;
std::function<bool()> g_saveVeto;
std::function<std::string(const std::string&)> g_levelMapper;        // g_vetoM
std::function<std::string(const std::string&, bool)> g_pathRewriter; // g_vetoM
// Where a rewritten level path is put for the guest: the tail of the speech controller's text buffer (idle while a
// level loads; the DRAKAN_LIFECYCLE_EXIT test aid hands the same spot to the level-change entry).
constexpr uint32_t kRewrittenPath = 0x5b7d20u;
constexpr size_t kRewrittenPathMax = 0x200;

Event makeEvent(int slot, uint8_t* rdram, R5900Context* c, Pending& p, bool after) {
    const Ram r{rdram};
    Event e;
    e.kind = after ? g_slots[slot].after : g_slots[slot].before;
    if (!after) {
        if (slot == kSlotLevel) {
            p.path = r.str(getRegU32(c, 4));
            p.level = stem(p.path);
            std::function<std::string(const std::string&)> mapper;
            {
                std::lock_guard<std::mutex> l(g_vetoM);
                mapper = g_levelMapper;
            }
            if (mapper)
                p.level = mapper(p.level);
            p.fromSave = getRegU32(c, 5) != 0;
        } else {
            p.path = r.str(getRegU32(c, slot == kSlotSave ? 6 : 5));
            p.level = stem(p.path); // the .rsg file name is the level display name
        }
    }
    e.path = p.path;
    e.level = p.level;
    e.fromSave = p.fromSave;
    if (after) {
        e.result = static_cast<int32_t>(getRegU32(c, 2));
        e.ok = e.result == 0; // all three return 0 on success (save write: observed with files on the card)
    }
    return e;
}

template <int N>
void finish(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    SlotState& s = g_slots[N];
    if (s.pending.empty()) { // a return into the trampoline without an entry: cannot happen, do not hang the guest
        std::fprintf(stderr, "[lifecycle] trampoline %d without a pending call\n", N);
        c->pc = 0u;
        return;
    }
    Pending p = std::move(s.pending.back());
    s.pending.pop_back();
    --g_active;
    c->pc = p.ra; // first: a listener may throw / the nested calls use copies of the context
    SET_GPR_U32(c, 31, p.ra);
    Event e = makeEvent(N, rdram, c, p, true);
    fire(e, rdram, c, rt);
    if (N == kSlotLevel || N == kSlotRestore)
        pollMount(rdram, c, rt);
}

template <int N>
void trampoline(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    finish<N>(rdram, c, rt);
}

template <int N>
void hook(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    SlotState& s = g_slots[N];
    if (N == kSlotSave && c->pc == s.entry) {
        std::function<bool()> veto;
        {
            std::lock_guard<std::mutex> l(g_vetoM);
            veto = g_saveVeto;
        }
        if (veto && veto() && !std::getenv("DRAKAN_ARENA_ALLOW_SAVE")) {
            std::fprintf(stderr, "[lifecycle] save write vetoed (multiplayer arena)\n");
            SET_GPR_U32(c, 2, 1u);
            c->pc = getRegU32(c, 31);
            return;
        }
    }
    if (c->pc != s.entry) { // the guest thread is resuming inside the function after a switch: nothing to announce
        s.orig(rdram, c, rt);
        return;
    }
    if constexpr (N == kSlotLevel) {
        std::function<std::string(const std::string&, bool)> rewriter;
        {
            std::lock_guard<std::mutex> l(g_vetoM);
            rewriter = g_pathRewriter;
        }
        if (rewriter) {
            const Ram r{rdram};
            const std::string from = r.str(getRegU32(c, 4));
            const std::string to = rewriter(from, getRegU32(c, 5) != 0);
            if (!to.empty() && to != from && to.size() < kRewrittenPathMax) {
                std::memcpy(rdram + addr(kRewrittenPath), to.c_str(), to.size() + 1);
                SET_GPR_U32(c, 4, addr(kRewrittenPath));
                std::fprintf(stderr, "[lifecycle] level load '%s' -> '%s'\n", from.c_str(), to.c_str());
            }
        }
    }
    Pending p;
    p.ra = getRegU32(c, 31);
    Event e = makeEvent(N, rdram, c, p, false);
    fire(e, rdram, c, rt);
    s.pending.push_back(std::move(p));
    ++g_active;
    SET_GPR_U32(c, 31, s.tramp);
    s.orig(rdram, c, rt);
    if (c->pc == s.tramp) // returned normally: announce inline, no dispatch round trip
        finish<N>(rdram, c, rt);
}

// PadMgr_Update, the frame boundary every other host hook uses: the controlled actor is polled here. (The engine's
// setter 0x142c60 has no caller: the mount / dismount code stores ctx+0x4b0 inline, in 0x44d3b0 and 0x456de0.)
PS2Runtime::RecompiledFunction g_origTick = nullptr;

// --- DRAKAN_LIFECYCLE_EXIT: test aid for a level change out of a running level (populated registry) -----------------
// "<level>[@<ticks>]": that many frames after the first level other than the intro has loaded, the level-change entry
// 0x13b7c0(path) that the exit-trigger screen calls (flags 0, `Level_Load` behind it) is posted as a guest call on a
// dispatcher thread at the main thread's priority (a tail call from PadMgr_Update froze the guest, and so did the
// default priority 1: the loader polls and the threads it waits on never ran). Once. EXPERIMENTAL: at priority 7 the
// world keeps running on the main thread while the load tears it down (null vtable call in the main thread); a real
// exit goes through a modal screen. Kept for the journal's notes, not part of the default harness.
constexpr uint32_t kLevelChange = 0x13b7c0u;
constexpr int kExitPriority = 7; // the game's main thread: a higher priority starves the threads its loader polls
constexpr uint32_t kScratchPath = 0x5b7d20u; // tail of the speech controller's text buffer (idle in a test run)
struct ExitTest {
    std::string level;
    uint32_t after = 900, ticks = 0;
    bool armed = false, ready = false;
} g_exit;

void exitTestOnLoad(const Event& e) {
    if (g_exit.armed && !g_exit.ready && !drakan_game::isDiscCutsceneLevel(e.level) && e.level != g_exit.level) {
        g_exit.ready = true;
        g_exit.ticks = 0;
    }
}

bool exitTestDue(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (!g_exit.armed || !g_exit.ready || g_active.load() > 0 || ++g_exit.ticks < g_exit.after ||
        !rt->hasFunction(addr(kLevelChange)))
        return false;
    g_exit.armed = false;
    const std::string path = "cdrom0:/English/Levels/" + g_exit.level + "/" + g_exit.level + ".lvl";
    std::memcpy(rdram + kScratchPath, path.c_str(), path.size() + 1);
    rt->eeScheduler().postGuestCall(addr(kLevelChange), kScratchPath, 0, 0, 0, nullptr, kExitPriority);
    std::fprintf(stderr, "[lifecycle-test] level change to '%s' via %x\n", g_exit.level.c_str(), addr(kLevelChange));
    return true;
}

void hookTick(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kPadMgrUpdate)) {
        pollMount(rdram, c, rt);
        exitTestDue(rdram, c, rt);
    }
    g_origTick(rdram, c, rt);
}

// Counts the kill-list flushes (evidence for the ordering question: the per-frame update and the save writer flush, the
// level loader does not). With DRAKAN_LIFECYCLE_LOG a non-empty flush is logged between the event lines.
PS2Runtime::RecompiledFunction g_origFlush = nullptr;
void hookFlush(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kKillFlush)) {
        ++g_flushCalls;
        if (g_log) {
            const uint32_t n = Ram{rdram}.u32(getRegU32(c, 4) + 0x448);
            if (n > 0)
                std::fprintf(stderr, "[lifecycle %.3f] kill-list flush: %u entries, ra=%x\n", nowS(), n,
                             getRegU32(c, 31));
        }
    }
    g_origFlush(rdram, c, rt);
}

// Reference guard. The engine keeps arrays of 108-byte records on some objects (array at +0xf4, count at +0xfc); each
// record holds a u16 registry reference at +4. 0x1042c8 walks them and calls a virtual function on whatever the
// registry slot holds, without checking that the slot is still a live handle. When the referenced object was destroyed
// behind the holder's back (a ghost or a proxied creature whose own events are swallowed, a handle recycled), the slot
// holds a free-list link, the "object" is a neighbouring slot and the call goes through a float: the joiner crash in
// docs/journal/2026-11-02 (missing-target JALR at 0x1043a0 / 0x1946cc). A reference whose slot is not a live handle
// with that very reference is cleared, which the engine already treats as "no reference".
constexpr uint32_t kRefRelease = 0x1042c8u;
PS2Runtime::RecompiledFunction g_origRefs = nullptr;
uint32_t g_refsCleared = 0;

void clearStaleRefs(uint8_t* rdram, uint32_t owner) {
    const Ram r{rdram};
    const uint32_t eng = engineOf(r);
    if (!eng || !Ram::ok(owner, 0x100))
        return;
    const int32_t count = static_cast<int32_t>(r.u32(owner + 0xfc));
    const uint32_t arr = r.u32(owner + 0xf4);
    if (count <= 0 || count > 4096 || !Ram::ok(arr, static_cast<uint32_t>(count) * 108u))
        return;
    const uint32_t table = r.u32(eng + 0x7c);
    const uint32_t high = r.u32(eng + 0x84);
    const uint32_t vtable = addr(kHandleVtable);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t rec = arr + static_cast<uint32_t>(i) * 108u;
        const uint32_t ref = r.u16(rec + 4);
        if (!ref || ref > high + 1 || !Ram::ok(table + (ref - 1) * 8u, 8))
            continue; // empty, or out of range (the engine clears these itself)
        const uint32_t v = r.u32(table + (ref - 1) * 8u);
        if (!v || (Ram::ok(v, 0x120) && r.u32(v) == vtable && r.u16(v + 0x12) == ref))
            continue;
        const uint16_t zero = 0;
        std::memcpy(rdram + ((rec + 4) & 0x1fffffffu), &zero, 2);
        if (++g_refsCleared <= 24)
            std::fprintf(stderr,
                         "[refguard %.3f] owner %08x (class %x) record %d held reference %u to a destroyed object "
                         "(slot value %08x): cleared\n",
                         nowS(), owner, classOf(r, eng, owner), i, ref, v);
    }
}

void hookRefs(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    if (c->pc == addr(kRefRelease))
        clearStaleRefs(rdram, getRegU32(c, 4));
    g_origRefs(rdram, c, rt);
}

uint32_t freeTableSlot(PS2Runtime& rt, PS2Runtime::RecompiledFunction fn) {
    for (uint32_t a = 0x100000u; a < 0x100100u; a += 4)
        if (!rt.hasFunction(a) && rt.registerFunction(a, fn))
            return a;
    return 0;
}

template <int N>
bool installSlot(PS2Runtime& rt, uint32_t pal, Kind before, Kind after) {
    SlotState& s = g_slots[N];
    s.entry = addr(pal);
    if (!s.entry || !rt.hasFunction(s.entry)) {
        std::fprintf(stderr, "[lifecycle] guest function %x missing: events %s/%s off\n", pal, kindName(before),
                     kindName(after));
        return false;
    }
    s.tramp = freeTableSlot(rt, &trampoline<N>);
    if (!s.tramp) {
        std::fprintf(stderr, "[lifecycle] no free function-table slot for %x\n", pal);
        return false;
    }
    s.before = before;
    s.after = after;
    s.orig = rt.lookupFunction(s.entry);
    rt.replaceFunction(s.entry, &hook<N>);
    return true;
}

// --- DRAKAN_LIFECYCLE_KILL: test aid for the "is the kill list flushed before the serialiser walks the registry" ----
// Destroys the first live handle of the given class (hex, e.g. 1000a) in BeforeSaveWrite through the plain 0x1413c0 (no
// flush) and reports the kill list size and liveness around the save.
struct KillTest {
    uint32_t cls = 0, victim = 0, ref = 0;
    bool armed = false, flush = false;
    Kind before = Kind::BeforeSaveWrite, after = Kind::AfterSaveWrite;
} g_kill;

uint32_t findHandleOfClass(const Event& e, uint32_t cls) {
    const Ram r{e.rdram};
    const uint32_t eng = engine(e);
    if (!eng)
        return 0;
    const uint32_t table = r.u32(eng + 0x7c), top = r.u32(eng + 0x84);
    const uint32_t player = r.u32(eng + 0x4b0), camera = r.u32(eng + 0x4b4);
    for (uint32_t i = 0; i < top + 1 && i < 0x4000; ++i) {
        const uint32_t h = r.u32(table + i * 8);
        if (!h || h == player || h == camera || !actorAlive(e, h))
            continue;
        if (cls == 0xffffffffu ? classOf(r, eng, h) != 0 : classOf(r, eng, h) == cls)
            return h;
    }
    return 0;
}

void killTestBefore(const Event& e) {
    if (!g_kill.armed)
        return;
    const Ram r{e.rdram};
    g_kill.victim = findHandleOfClass(e, g_kill.cls);
    if (!g_kill.victim) { // stays armed: a later matching event may find one (the first load sees an empty registry)
        std::fprintf(stderr, "[lifecycle-test] %s: no live handle of class %x\n", kindName(e.kind), g_kill.cls);
        return;
    }
    g_kill.armed = false;
    g_kill.ref = r.u16(g_kill.victim + 0x12);
    const uint32_t before = killListCount(e);
    const bool ok = destroyActor(e, g_kill.victim, g_kill.flush);
    std::fprintf(stderr,
                 "[lifecycle-test] %s: destroy %x (class %x ref %u, flush=%d) ok=%d killList %u -> %u alive=%d\n",
                 kindName(e.kind), g_kill.victim, g_kill.cls, g_kill.ref, g_kill.flush ? 1 : 0, ok ? 1 : 0, before,
                 killListCount(e), actorAlive(e, g_kill.victim) ? 1 : 0);
}

void killTestAfter(const Event& e) {
    if (!g_kill.victim)
        return;
    const Ram r{e.rdram};
    const uint32_t eng = engine(e);
    const uint32_t slotPtr = r.u32(r.u32(eng + 0x7c) + (g_kill.ref - 1) * 8);
    std::fprintf(stderr, "[lifecycle-test] %s: ok=%d killList=%u victim alive=%d slot=%x (was %x)\n", kindName(e.kind),
                 e.ok ? 1 : 0, killListCount(e), actorAlive(e, g_kill.victim) ? 1 : 0, slotPtr, g_kill.victim);
    g_kill.victim = 0;
}

} // namespace

const char* kindName(Kind kind) {
    switch (kind) {
        case Kind::BeforeLevelLoad:
            return "beforeLevelLoad";
        case Kind::AfterLevelLoad:
            return "afterLevelLoad";
        case Kind::BeforeSaveWrite:
            return "beforeSaveWrite";
        case Kind::AfterSaveWrite:
            return "afterSaveWrite";
        case Kind::BeforeSaveRestore:
            return "beforeSaveRestore";
        case Kind::AfterSaveRestore:
            return "afterSaveRestore";
        case Kind::MountChanged:
            return "mountChanged";
        default:
            return "?";
    }
}

ListenerId subscribe(Kind kind, Listener listener) {
    if (kind >= Kind::Count || !listener)
        return 0;
    Registry& r = registry();
    std::lock_guard<std::mutex> l(r.m);
    const ListenerId id = r.next++;
    r.byKind[static_cast<size_t>(kind)].emplace_back(id, std::move(listener));
    return id;
}

void unsubscribe(ListenerId id) {
    Registry& r = registry();
    std::lock_guard<std::mutex> l(r.m);
    for (auto& v : r.byKind)
        v.erase(std::remove_if(v.begin(), v.end(), [id](const auto& p) { return p.first == id; }), v.end());
}

bool installed() {
    return g_installed;
}

void setLevelNameMapper(std::function<std::string(const std::string&)> mapper) {
    std::lock_guard<std::mutex> l(g_vetoM);
    g_levelMapper = std::move(mapper);
}

void setLevelPathRewriter(std::function<std::string(const std::string&, bool)> rewriter) {
    std::lock_guard<std::mutex> l(g_vetoM);
    g_pathRewriter = std::move(rewriter);
}

void setSaveWriteVeto(std::function<bool()> veto) {
    std::lock_guard<std::mutex> l(g_vetoM);
    g_saveVeto = std::move(veto);
}

bool transitionActive() {
    return g_active.load() > 0;
}

Mount currentMount() {
    return g_mount;
}

uint32_t engine(const Event& e) {
    return e.rdram ? engineOf(Ram{e.rdram}) : 0;
}

bool actorAlive(const Event& e, uint32_t actor) {
    const Ram r{e.rdram};
    const uint32_t eng = engine(e);
    if (!eng || !Ram::ok(actor, 0x120) || r.u32(actor) != addr(kHandleVtable))
        return false;
    const uint32_t ref = r.u16(actor + 0x12);
    if (ref == 0 || ref > r.u32(eng + 0x84) + 1)
        return false;
    return r.u32(r.u32(eng + 0x7c) + (ref - 1) * 8) == actor && static_cast<int32_t>(r.u32(actor + 4)) >= 0;
}

uint32_t callGuest(const Event& e, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    if (!fn || !e.runtime || !e.ctx || !e.runtime->hasFunction(fn))
        return 0;
    // R5900Context is large: keep it off the stack. pc must equal the function's entry; $ra = 0 marks "finished".
    auto tmp = std::make_unique<R5900Context>(*e.ctx);
    SET_GPR_U32(tmp.get(), 4, a0);
    SET_GPR_U32(tmp.get(), 5, a1);
    SET_GPR_U32(tmp.get(), 6, a2);
    SET_GPR_U32(tmp.get(), 7, a3);
    SET_GPR_U32(tmp.get(), 31, 0);
    tmp->pc = fn;
    tmp->in_delay_slot = false;
    {
        const ScopedEeNoPreempt noPreempt;
        for (int guard = 0; guard < 64 && tmp->pc != 0u; ++guard) {
            if (!e.runtime->hasFunction(tmp->pc))
                break;
            e.runtime->lookupFunction(tmp->pc)(e.rdram, tmp.get(), e.runtime);
        }
    }
    return getRegU32(tmp.get(), 2);
}

uint32_t killListCount(const Event& e) {
    const uint32_t eng = engine(e);
    return eng ? Ram{e.rdram}.u32(eng + 0x448) : 0;
}

void flushKillList(const Event& e) {
    if (const uint32_t eng = engine(e))
        callGuest(e, addr(kKillFlush), eng);
}

bool destroyActor(const Event& e, uint32_t actor, bool flush) {
    const uint32_t eng = engine(e);
    if (!eng || !actorAlive(e, actor))
        return false;
    callGuest(e, addr(kKillActor), eng, actor);
    if (flush)
        flushKillList(e);
    return true;
}

void install(PS2Runtime& runtime) {
    if (g_installed)
        return;
    if (const char* off = std::getenv("DRAKAN_LIFECYCLE"); off && off[0] == '0')
        return;
    const char* log = std::getenv("DRAKAN_LIFECYCLE_LOG");
    g_log = log && log[0] && log[0] != '0';
    if (!drakan_region::build().identityAddresses) {
        std::fprintf(stderr, "[lifecycle] not installed: addresses are SCES-500.06 only (profile %s)\n",
                     drakan_region::build().id);
        return;
    }
    const bool level = installSlot<kSlotLevel>(runtime, kLevelLoad, Kind::BeforeLevelLoad, Kind::AfterLevelLoad);
    const bool save = installSlot<kSlotSave>(runtime, kSaveWrite, Kind::BeforeSaveWrite, Kind::AfterSaveWrite);
    const bool restore =
        installSlot<kSlotRestore>(runtime, kSaveRestore, Kind::BeforeSaveRestore, Kind::AfterSaveRestore);
    bool mount = false;
    if (runtime.hasFunction(addr(kPadMgrUpdate))) {
        g_origTick = runtime.lookupFunction(addr(kPadMgrUpdate));
        runtime.replaceFunction(addr(kPadMgrUpdate), &hookTick);
        mount = true;
    }
    if (runtime.hasFunction(addr(kKillFlush))) {
        g_origFlush = runtime.lookupFunction(addr(kKillFlush));
        runtime.replaceFunction(addr(kKillFlush), &hookFlush);
    }
    if (const char* g = std::getenv("DRAKAN_REFGUARD"); !(g && g[0] == '0') && runtime.hasFunction(addr(kRefRelease))) {
        g_origRefs = runtime.lookupFunction(addr(kRefRelease));
        runtime.replaceFunction(addr(kRefRelease), &hookRefs);
    }
    if (const char* k = std::getenv("DRAKAN_LIFECYCLE_KILL"); k && *k) {
        // "<class hex>[:save|restore|level]": destroy the first live handle of that class in the chosen "before" event
        // (save: plain 0x1413c0, the writer flushes; restore / level: with the synchronous flush)
        char* end = nullptr;
        const bool any = k[0] == '*'; // "*" = the first live handle of any class
        g_kill.cls = any ? 0xffffffffu : static_cast<uint32_t>(std::strtoul(k, &end, 16));
        const char* rest = any ? k + 1 : end;
        const std::string when = rest && *rest == ':' ? rest + 1 : "save";
        g_kill.armed = g_kill.cls != 0;
        g_kill.flush = when != "save";
        g_kill.before = when == "level"     ? Kind::BeforeLevelLoad
                        : when == "restore" ? Kind::BeforeSaveRestore
                                            : Kind::BeforeSaveWrite;
        g_kill.after = when == "level"     ? Kind::AfterLevelLoad
                       : when == "restore" ? Kind::AfterSaveRestore
                                           : Kind::AfterSaveWrite;
        subscribe(g_kill.before, killTestBefore);
        subscribe(g_kill.after, killTestAfter);
    }
    if (const char* x = std::getenv("DRAKAN_LIFECYCLE_EXIT"); x && *x) {
        g_exit.level = x;
        if (const size_t at = g_exit.level.find('@'); at != std::string::npos) {
            g_exit.after = static_cast<uint32_t>(std::strtoul(g_exit.level.c_str() + at + 1, nullptr, 10));
            g_exit.level.resize(at);
        }
        g_exit.armed = !g_exit.level.empty();
        subscribe(Kind::AfterLevelLoad, exitTestOnLoad);
    }
    g_installed = level || save || restore || mount;
    std::fprintf(stderr, "[lifecycle] hooks: level=%d saveWrite=%d saveRestore=%d mount=%d%s\n", level, save, restore,
                 mount, g_log ? " (logging)" : "");
}

} // namespace drakan_lifecycle
