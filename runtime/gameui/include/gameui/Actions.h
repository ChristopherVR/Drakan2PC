// UI intents and their verification. The UI thread pushes UiActions; the game (EE) side pops them, executes each
// through the existing native routes (selection substitution + pad taps, see the data-model note section 5) and
// tracks the expected effect. Native refusals are silent, so every action carries an Expectation that is checked
// against later snapshots: the effect appears (Confirmed) or the time-out passes (Refused).
#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "gameui/Model.h"

namespace gameui {

enum class ActionKind : uint8_t {
    SelectItem,       // target = cell: steer the native selection (fills the details buffers)
    UseOrEquip,       // target = cell: Square in Equipment
    Drop,             // target = cell: Triangle
    ToggleHotSlot,    // target = cell: Cross in Equipment (not in a shop)
    SelectSpell,      // target = spell index
    CastOrSummon,     // target = spell index: Square in Spells
    SpendSkillPoint,  // target = skill row
    ReturnSkillPoint, // target = skill row
    ShopBrowse,       // target = +1 / -1: step the shop list selection
    Buy,              // target = shop selection: opens the native Yes / No box
    Sell,             // target = cell: Circle in a shop, opens the native Yes / No box
    SwitchSection,    // target = native section (0 Equipment, 1 Spells, 2 Skills, 3 Map, 4 Journal)
    SelectMarker,     // target = quest record: steer the native map selection (map section +0x6c) by arrow taps
    Close,            // close screen 0x15 (SELECT path)
};
const char* actionName(ActionKind k);

// What the executor watches for after performing an action.
enum class Effect : uint8_t {
    SelectionIs,        // ctl+0x20 == target and the controller is idle
    ItemChanged,        // the item's equipped flag / quantity changed, it left the grid, or a box opened
    ItemRemoved,        // no slot holds the handle any more (or a box opened)
    HotSlotChanged,     // the item's hot-slot flag differs from `before`
    SpellSelected,      // spells.selected == target
    SummonStarted,      // summon state or mana changed, or the screen closed
    SkillPointsChanged, // unspent points moved by `delta` or the row's level changed
    ShopSelectionMoved, // ctl+0x1c or the stock handle changed
    BoxOrGoldChanged,   // the top screen changed (Yes / No box) or the gold changed
    SectionIs,          // screen+0x154 == target
    MarkerIs,           // map section +0x6c == target
    ScreenClosed,       // S+0x34 == 0
};

struct Expectation {
    Effect effect = Effect::SelectionIs;
    int target = -1;
    uint32_t handle = 0;    // item identity at execution time
    int32_t before = 0;     // the observed value the effect is measured against (flag, quantity, points, gold)
    int32_t before2 = 0;    // second value (mana, level, stock handle low bits)
    int32_t delta = 0;      // SkillPointsChanged: -1 spend, +1 return
    uint32_t topScreen = 0; // *(0x4fb60c) at execution time
    uint64_t startSequence = 0;
    uint32_t timeoutFrames = 30;
};

struct UiAction {
    uint32_t id = 0; // assigned by ActionQueue::push
    ActionKind kind = ActionKind::Close;
    int target = -1;
    uint32_t handle = 0; // item the UI meant (0 when not an item action); the executor refuses on a mismatch
};

enum class Outcome : uint8_t { Pending, Confirmed, Refused, Aborted };
const char* outcomeName(Outcome o);

struct ActionResult {
    uint32_t id = 0;
    ActionKind kind = ActionKind::Close;
    int target = -1;
    Outcome outcome = Outcome::Pending;
};

// Default time-out per action, in snapshots (frames).
uint32_t defaultTimeout(ActionKind k);
// Expectation for `a` executed now, given the snapshot taken just before executing it.
Expectation makeExpectation(const UiAction& a, const Snapshot& before);
// Pending until the effect shows in `now` or the time-out passes. Aborted when the snapshot became invalid (no
// player, dragon) or the screen closed under an action that needs it.
Outcome evaluate(const Expectation& e, const Snapshot& now);

// Thread-safe FIFO. Consecutive selection steers coalesce (only the newest SelectItem survives), so a fast
// mouse sweep does not queue a backlog of native selections.
class ActionQueue {
public:
    uint32_t push(UiAction a); // returns the assigned id
    std::optional<UiAction> pop();
    size_t size() const;
    void clear();

private:
    mutable std::mutex m_;
    std::deque<UiAction> q_;
    uint32_t nextId_ = 1;
};

// Thread-safe FIFO of results (game side -> UI).
class ResultQueue {
public:
    void push(const ActionResult& r);
    std::optional<ActionResult> pop();

private:
    mutable std::mutex m_;
    std::deque<ActionResult> q_;
};

// Game side bookkeeping: one action in flight at a time (native routes are serial). begin() after performing the
// action, update() with every new snapshot; finished actions are returned.
class ActionTracker {
public:
    // `framesPerTick` > 1 stretches the time-out when snapshots come faster than the ~60 per second the defaults
    // assume (an uncapped EE takes ~250): native effects run on game time (an equip shows after ~0.1-0.3 s).
    void begin(const UiAction& a, const Snapshot& before, float framesPerTick = 1.0f);
    std::optional<ActionResult> update(const Snapshot& now);
    bool busy() const { return active_.has_value(); }
    const std::optional<Expectation>& expectation() const { return expect_; }

private:
    std::optional<UiAction> active_;
    std::optional<Expectation> expect_;
};

// Close of screen 0x15 through its own Select path (42f640 -> 431d10 starts the close animation, 42fd18 closes at its
// end). The native screen ignores Select while its open animation runs (screen+0x15c > 0), so a close asked for then
// (the key that opened the screen, a quick second tap) is deferred until the animation ends, never dropped. A tap the
// screen did not act on (no close animation, still open) is repeated; Failed after `maxTaps` or the time limits, so the
// glue can fall back to the native page instead of leaving the screen open and invisible.
class CloseExecutor {
public:
    enum class Step : uint8_t { Wait, Tap, Done, Failed };
    struct Limits {
        uint32_t openWait = 600; // frames waiting for the open animation to end
        uint32_t tapWait = 45;   // frames after a tap before it counts as ignored
        int maxTaps = 3;
        uint32_t closeWait = 300; // frames for the close animation
        // Before the tap the inventory controller must have been idle (ctl+0 == 0) for `idleFrames` snapshots in a
        // row: a selection change in flight (icon object swap, details builder) and the screen's teardown must not
        // overlap. After `busyWait` frames the tap goes ahead anyway.
        uint32_t idleFrames = 3;
        uint32_t busyWait = 120;
    };
    CloseExecutor() = default;
    explicit CloseExecutor(Limits l) : lim_(l) {}
    // `idleSoFar`: snapshots in a row the controller was already idle, with no action in flight, before the close was
    // asked for (they count towards idleFrames: a close of a settled screen taps at once).
    void begin(uint32_t idleSoFar = 0);
    // Once per fresh snapshot (frame) while the close is in flight; Tap = tap Select now.
    Step update(const Snapshot& s);
    bool active() const { return active_; }
    int taps() const { return taps_; }
    uint32_t deferredFrames() const { return deferred_; }
    uint32_t busyFrames() const { return busy_; } // frames the tap waited for the controller

private:
    Limits lim_;
    bool active_ = false;
    int taps_ = 0;
    uint32_t deferred_ = 0, sinceTap_ = 0, closing_ = 0, idle_ = 0, busy_ = 0;
};

} // namespace gameui
