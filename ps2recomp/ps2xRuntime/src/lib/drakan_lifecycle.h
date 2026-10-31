// Engine lifecycle events for host features that own guest actors (ghost players, editor hot reload, multiplayer).
// Hook points, ordering and evidence: docs/journal/2026-10-31/ghost-spike-s3.md, docs/reference/world-model.md
// section 6.
//
// All listeners run on the EE game thread at a call boundary of the hooked guest function ("safe point": the guest is
// between two instructions of its own code, no actor list is being iterated by the hooked function itself). A listener
// must not block. Guest calls from a listener go through callGuest() (nested call on a copy of the context, no
// preemption), so only call guest functions that do not wait on a semaphore held by another guest thread.
//
// Addresses are SCES-500.06 (PAL) only; other disc profiles log "not installed" and never fire an event.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

class PS2Runtime;
struct R5900Context;

namespace drakan_lifecycle {

enum class Kind : uint8_t {
    BeforeLevelLoad,   // Level_Load 0x1c94c8 about to run (level change, New Game, and the load inside a save restore)
    AfterLevelLoad,    // it returned (ok = the guest returned 0)
    BeforeSaveWrite,   // save serialiser 0x169c28 about to run (menu save, quick save, level-change save)
    AfterSaveWrite,    // it returned (ok = the guest returned 0)
    BeforeSaveRestore, // save loader 0x16adb8 about to run (menu load, quick load, Continue)
    AfterSaveRestore,  // it returned
    MountChanged,      // the controlled actor switched class: Rynn <-> Arokh
    Count
};

enum class Mount : uint8_t { None, Rynn, Arokh };

const char* kindName(Kind kind);

struct Event {
    Kind kind = Kind::Count;
    // Level events: the disc path the guest was given and its stem ("Level 1A - Surdana"). Save events: the save path
    // as the guest builds it ("BESCES-50006Slot33/Level 1A - Surdana.rsg") and its stem; empty when unreadable.
    std::string path;
    std::string level;
    bool fromSave = false; // a level load that runs inside a save restore (Level_Load flags != 0)
    bool ok = true;        // After* events: the guest's return value says success
    int32_t result = 0;    // After* events: raw guest return value ($v0)
    // MountChanged
    Mount previous = Mount::None, current = Mount::None;
    uint32_t controlled = 0; // handle now in ctx+0x4b0
    // Guest access for helpers (valid only during the callback).
    uint8_t* rdram = nullptr;
    PS2Runtime* runtime = nullptr;
    R5900Context* ctx = nullptr;
};

using Listener = std::function<void(const Event&)>;
using ListenerId = int;

// Thread safe. Listeners of one kind run in subscription order. A listener may unsubscribe itself.
ListenerId subscribe(Kind kind, Listener listener);
void unsubscribe(ListenerId id);

// Installed from applyDrakanOverrides (after the function table is populated). DRAKAN_LIFECYCLE_LOG=1 prints every
// event to stderr; DRAKAN_LIFECYCLE=0 disables the hooks.
void install(PS2Runtime& runtime);
bool installed();

// True from the "before" to the "after" event of a level load, a save write or a save restore. Those calls can be
// preempted by another guest thread (the loader waits on semaphores), so a host spawn (postGuestCall) must not run
// while this is set: an actor created mid-save would be serialised, one created mid-load would be torn down or half
// built. Readable from any thread.
bool transitionActive();

// The mount state of the controlled actor as the hooks last saw it (live read of ctx+0x4b0 when the guest is idle).
Mount currentMount();

// --- helpers for listeners (EE thread, inside a callback) ---
// Runs guest function `addr` to completion on a copy of the current context; returns $v0, 0 when the function does not
// exist. Never preempted.
uint32_t callGuest(const Event& e, uint32_t addr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);
// Engine object (`*(0x5565d4)`, 0x4de5e0) or 0 before it exists.
uint32_t engine(const Event& e);
// True when `actor` is a live registered handle (vtable 0x51b8b0, own ref == slot + 1, bit 31 of +4 clear).
bool actorAlive(const Event& e, uint32_t actor);
// Destroys a guest actor through the engine's public entry 0x1413c0 (flags it and appends it to the kill list at
// engine+0x440). With flush = true the kill list is flushed at once (0x142e50), so the actor is gone when this returns.
// Required before Level_Load (see the journal: the level loader does not flush); optional before a save write (the
// serialiser flushes first). Returns false when the actor is not alive.
bool destroyActor(const Event& e, uint32_t actor, bool flush = true);
// Flushes the kill list now (0x142e50).
void flushKillList(const Event& e);
// Number of entries waiting in the kill list.
uint32_t killListCount(const Event& e);

} // namespace drakan_lifecycle
