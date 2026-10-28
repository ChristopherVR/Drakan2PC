#include "gameui/Actions.h"

#include <algorithm>

#include "gameui/Addresses.h"

namespace gameui {

const char* actionName(ActionKind k) {
    switch (k) {
        case ActionKind::SelectItem:
            return "select-item";
        case ActionKind::UseOrEquip:
            return "use-or-equip";
        case ActionKind::Drop:
            return "drop";
        case ActionKind::ToggleHotSlot:
            return "toggle-hot-slot";
        case ActionKind::SelectSpell:
            return "select-spell";
        case ActionKind::CastOrSummon:
            return "cast-or-summon";
        case ActionKind::SpendSkillPoint:
            return "spend-skill-point";
        case ActionKind::ReturnSkillPoint:
            return "return-skill-point";
        case ActionKind::ShopBrowse:
            return "shop-browse";
        case ActionKind::Buy:
            return "buy";
        case ActionKind::Sell:
            return "sell";
        case ActionKind::SwitchSection:
            return "switch-section";
        case ActionKind::SelectMarker:
            return "select-marker";
        case ActionKind::Close:
            return "close";
    }
    return "?";
}

const char* outcomeName(Outcome o) {
    switch (o) {
        case Outcome::Pending:
            return "pending";
        case Outcome::Confirmed:
            return "confirmed";
        case Outcome::Refused:
            return "refused";
        case Outcome::Aborted:
            return "aborted";
    }
    return "?";
}

uint32_t defaultTimeout(ActionKind k) {
    switch (k) {
        case ActionKind::SelectItem:
        case ActionKind::SelectSpell:
        case ActionKind::ToggleHotSlot:
        case ActionKind::ShopBrowse:
        case ActionKind::SwitchSection:
        case ActionKind::SelectMarker:
            return 15;
        case ActionKind::Close:
            return 90; // the close animation runs before S+0x34 drops
        default:
            return 30;
    }
}

namespace {

Effect effectFor(ActionKind k) {
    switch (k) {
        case ActionKind::SelectItem:
            return Effect::SelectionIs;
        case ActionKind::UseOrEquip:
            return Effect::ItemChanged;
        case ActionKind::Drop:
            return Effect::ItemRemoved;
        case ActionKind::ToggleHotSlot:
            return Effect::HotSlotChanged;
        case ActionKind::SelectSpell:
            return Effect::SpellSelected;
        case ActionKind::CastOrSummon:
            return Effect::SummonStarted;
        case ActionKind::SpendSkillPoint:
        case ActionKind::ReturnSkillPoint:
            return Effect::SkillPointsChanged;
        case ActionKind::ShopBrowse:
            return Effect::ShopSelectionMoved;
        case ActionKind::Buy:
        case ActionKind::Sell:
            return Effect::BoxOrGoldChanged;
        case ActionKind::SwitchSection:
            return Effect::SectionIs;
        case ActionKind::SelectMarker:
            return Effect::MarkerIs;
        case ActionKind::Close:
            return Effect::ScreenClosed;
    }
    return Effect::ScreenClosed;
}

int skillLevel(const Snapshot& s, int row) {
    if (row < 0 || row >= static_cast<int>(s.skills.entries.size()))
        return -1;
    return s.skills.entries[static_cast<size_t>(row)].level;
}

bool boxOpened(const Expectation& e, const Snapshot& now) {
    return now.native.topScreen != e.topScreen && now.native.topScreenId != addr::screen::kInventoryId;
}

} // namespace

Expectation makeExpectation(const UiAction& a, const Snapshot& before) {
    Expectation e;
    e.effect = effectFor(a.kind);
    e.target = a.target;
    e.handle = a.handle;
    e.topScreen = before.native.topScreen;
    e.startSequence = before.sequence;
    e.timeoutFrames = defaultTimeout(a.kind);
    const ItemEntry* item = a.handle ? before.itemByHandle(a.handle) : before.itemAtCell(a.target);
    if (item && !e.handle)
        e.handle = item->handle;
    switch (e.effect) {
        case Effect::ItemChanged:
            e.before = item ? (item->equipped ? 1 : 0) : 0;
            e.before2 = item ? item->quantity : 0;
            break;
        case Effect::HotSlotChanged:
            e.before = item && item->hotSlot ? 1 : 0;
            break;
        case Effect::SummonStarted:
            e.before = static_cast<int32_t>(before.spells.summonState);
            e.before2 = before.stats.mana;
            break;
        case Effect::SkillPointsChanged:
            e.before = before.skills.unspentPoints;
            e.before2 = skillLevel(before, a.target);
            e.delta = a.kind == ActionKind::SpendSkillPoint ? -1 : 1;
            break;
        case Effect::ShopSelectionMoved:
            e.before = before.shop.selection;
            e.before2 = static_cast<int32_t>(before.shop.stock.handle);
            break;
        case Effect::BoxOrGoldChanged:
            e.before = before.stats.gold;
            break;
        default:
            break;
    }
    return e;
}

Outcome evaluate(const Expectation& e, const Snapshot& now) {
    if (!now.native.valid)
        return Outcome::Aborted;
    bool done = false;
    const ItemEntry* item = e.handle ? now.itemByHandle(e.handle) : nullptr;
    switch (e.effect) {
        case Effect::SelectionIs:
            done = now.native.selection == e.target && !now.native.controllerBusy;
            break;
        case Effect::ItemChanged:
            done = !item || (item->equipped ? 1 : 0) != e.before || item->quantity != e.before2 || boxOpened(e, now);
            break;
        case Effect::ItemRemoved:
            done = !item || boxOpened(e, now);
            break;
        case Effect::HotSlotChanged:
            done = item && (item->hotSlot ? 1 : 0) != e.before;
            break;
        case Effect::SpellSelected:
            done = now.spells.selected == e.target;
            break;
        case Effect::SummonStarted:
            done = static_cast<int32_t>(now.spells.summonState) != e.before || now.stats.mana != e.before2 ||
                   !now.native.inventoryOpen;
            break;
        case Effect::SkillPointsChanged:
            done = now.skills.unspentPoints == e.before + e.delta || skillLevel(now, e.target) != e.before2;
            break;
        case Effect::ShopSelectionMoved:
            done = now.shop.selection != e.before || static_cast<int32_t>(now.shop.stock.handle) != e.before2;
            break;
        case Effect::BoxOrGoldChanged:
            done = boxOpened(e, now) || now.stats.gold != e.before;
            break;
        case Effect::SectionIs:
            done = now.native.section == e.target;
            break;
        case Effect::MarkerIs:
            done = now.native.mapSelection == e.target;
            break;
        case Effect::ScreenClosed:
            done = !now.native.inventoryOpen;
            break;
    }
    if (done)
        return Outcome::Confirmed;
    if (e.effect != Effect::ScreenClosed && e.effect != Effect::SummonStarted && !now.native.inventoryOpen)
        return Outcome::Aborted;
    if (now.sequence >= e.startSequence + e.timeoutFrames)
        return Outcome::Refused;
    return Outcome::Pending;
}

// ---- queues ----

uint32_t ActionQueue::push(UiAction a) {
    std::lock_guard<std::mutex> lock(m_);
    a.id = nextId_++;
    if (nextId_ == 0)
        nextId_ = 1;
    const bool coalesce = a.kind == ActionKind::SelectItem || a.kind == ActionKind::SelectMarker;
    if (coalesce && !q_.empty() && q_.back().kind == a.kind)
        q_.back() = a;
    else
        q_.push_back(a);
    return a.id;
}

std::optional<UiAction> ActionQueue::pop() {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.empty())
        return std::nullopt;
    UiAction a = q_.front();
    q_.pop_front();
    return a;
}

size_t ActionQueue::size() const {
    std::lock_guard<std::mutex> lock(m_);
    return q_.size();
}

void ActionQueue::clear() {
    std::lock_guard<std::mutex> lock(m_);
    q_.clear();
}

void ResultQueue::push(const ActionResult& r) {
    std::lock_guard<std::mutex> lock(m_);
    q_.push_back(r);
}

std::optional<ActionResult> ResultQueue::pop() {
    std::lock_guard<std::mutex> lock(m_);
    if (q_.empty())
        return std::nullopt;
    ActionResult r = q_.front();
    q_.pop_front();
    return r;
}

// ---- tracker ----

void ActionTracker::begin(const UiAction& a, const Snapshot& before, float framesPerTick) {
    active_ = a;
    expect_ = makeExpectation(a, before);
    if (framesPerTick > 1.0f)
        expect_->timeoutFrames =
            static_cast<uint32_t>(static_cast<float>(expect_->timeoutFrames) * std::min(framesPerTick, 16.0f));
}

std::optional<ActionResult> ActionTracker::update(const Snapshot& now) {
    if (!active_)
        return std::nullopt;
    const Outcome o = evaluate(*expect_, now);
    if (o == Outcome::Pending)
        return std::nullopt;
    ActionResult r{active_->id, active_->kind, active_->target, o};
    active_.reset();
    expect_.reset();
    return r;
}

// ---- close ----

void CloseExecutor::begin(uint32_t idleSoFar) {
    active_ = true;
    taps_ = 0;
    deferred_ = sinceTap_ = closing_ = busy_ = 0;
    idle_ = idleSoFar;
}

CloseExecutor::Step CloseExecutor::update(const Snapshot& s) {
    if (!active_)
        return Step::Done;
    const auto finish = [&](Step st) {
        active_ = false;
        return st;
    };
    if (!s.native.inventoryOpen)
        return finish(Step::Done);
    if (s.native.topScreenId != addr::screen::kInventoryId)
        return Step::Wait;      // a box over the screen has the input; the glue does not step meanwhile
    if (s.native.animation < 0) // closing: the tap (or the native close path) was taken
        return ++closing_ > lim_.closeWait ? finish(Step::Failed) : Step::Wait;
    if (s.native.animation > 0) // still opening: Select would be ignored
        return ++deferred_ > lim_.openWait ? finish(Step::Failed) : Step::Wait;
    idle_ = s.native.controllerBusy ? 0 : idle_ + 1;
    if (taps_ == 0 && idle_ < lim_.idleFrames && busy_ < lim_.busyWait) {
        ++busy_; // the controller is (or just was) busy: let it settle before the screen starts closing
        return Step::Wait;
    }
    if (taps_ > 0 && ++sinceTap_ <= lim_.tapWait)
        return Step::Wait;
    if (taps_ >= lim_.maxTaps)
        return finish(Step::Failed);
    ++taps_;
    sinceTap_ = 0;
    return Step::Tap;
}

} // namespace gameui
