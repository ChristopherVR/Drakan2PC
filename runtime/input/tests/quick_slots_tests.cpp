// Quick-slot hotkeys: QUICK1..QUICK9 bindings, the ACT:0x02..0x06 migration, the native quick inventory order and the
// driver against a small model of the native bar (23cae0 / Game_OnInputCommand 0x4006). Synthetic guest only.
#include "input/Config.h"
#include "input/FrameInput.h"
#include "input/QuickSlots.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
using namespace drakan_input;
namespace q = quickslot;

static unsigned checks;
static void check(bool b, const char* name) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}

static int quickOf(const Config& c, int dik) {
    int n = 0;
    for (const auto& b : c.game)
        if (b.src == Binding::Key && b.code == dik && b.act == Binding::Quick)
            n = int(b.value);
    return n;
}
static Config parsed(const std::string& text) {
    Config c = Config::defaults();
    std::string warn;
    Config::parse(text, c, &warn);
    check(warn.empty(), "test ini parses without warnings");
    return c;
}

struct Guest final : IGuest {
    std::map<uint32_t, uint8_t> mem;
    uint8_t read8(uint32_t p) override {
        auto i = mem.find(p);
        return i == mem.end() ? 0 : i->second;
    }
    uint32_t read32(uint32_t p) override {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i)
            v |= uint32_t(read8(p + i)) << (i * 8);
        return v;
    }
    void write8(uint32_t p, uint8_t v) override { mem[p] = v; }
    void write32(uint32_t p, uint32_t v) override {
        for (unsigned i = 0; i < 4; ++i)
            write8(p + i, uint8_t(v >> (i * 8)));
    }
    uint32_t call(const GuestCall&) override { return 0; }
};

// A player with a quick inventory and the native bar's reaction to the pad (only what the driver relies on).
struct Game {
    static constexpr uint32_t ctx = 0x400000, handle = 0x410000, inner = 0x420000, player = 0x430000;
    static constexpr uint32_t ctl = player + q::kController, list = 0x440000, padMgr = 0x4e90c0;
    static constexpr uint32_t kR1 = 0x8, kSquare = 0x80, kActR1 = 0x127, kActSquare = 0x124;
    Guest g;
    uint16_t prev = 0; // pad buttons of the previous sample (active high)
    int opens = 0, confirms = 0, steps = 0;
    int refuseOpens = 0; // the next R1 presses are refused (a weapon swap / drink still playing)
    explicit Game(std::initializer_list<int> hotCells, uint32_t weaponCell = 1000) {
        g.write32(q::kContextPtr, ctx);
        g.write32(ctx + 0x4b0, handle);
        g.write32(handle + 0xfc, inner);
        g.write32(inner + 0x10, player);
        // Standard scheme as in the live table: Start (0x120) -> 0x4006 first, Square (0x124) -> Attack 0x4006,
        // R1 (0x128) -> Quick Inventory (records {action, mask, -}).
        const uint32_t table = padMgr + q::kPadTable;
        g.write32(table + 0 * q::kPadTableStride, 0x120);
        g.write32(table + 0 * q::kPadTableStride + 4, 0x800);
        g.write32(table + 1 * q::kPadTableStride, kActSquare);
        g.write32(table + 1 * q::kPadTableStride + 4, kSquare);
        g.write32(table + 2 * q::kPadTableStride, kActR1);
        g.write32(table + 2 * q::kPadTableStride + 4, kR1);
        auto key = [&](uint32_t act, uint16_t cmd) {
            g.write8(padMgr + q::kKeymap + act * 2, uint8_t(cmd));
            g.write8(padMgr + q::kKeymap + act * 2 + 1, uint8_t(cmd >> 8));
        };
        key(0x120, q::kCmdAttack);
        key(kActSquare, q::kCmdAttack);
        key(kActR1, q::kCmdQuickInventory);
        uint32_t n = 0;
        for (int cell : hotCells) {
            const uint32_t item = 0x500000 + uint32_t(cell) * 0x100;
            const uint32_t rec = ctl + q::kSlots + uint32_t(cell) * q::kSlotStride;
            g.write32(rec + q::kHot, 1);
            g.write32(rec + q::kItem, item);
            g.write32(item + q::kItemType, uint32_t(cell) == weaponCell ? q::kTypeWeapon : 11);
            g.write32(list + n * 8, uint32_t(cell));
            ++n;
        }
        g.write32(ctl + q::kList, list);
        g.write32(ctl + q::kListCapacity, 25);
        g.write32(ctl + q::kListCount, n);
    }
    uint32_t rec(int cell) const { return ctl + q::kSlots + uint32_t(cell) * q::kSlotStride; }
    bool equipped(int cell) { return g.read32(rec(cell) + q::kEquipped) != 0; }
    // One PadMgr update: pad = physical buttons, then the driver's overlay; press edges drive the bar.
    void sample(const QuickSlotDriver& d, uint16_t physical = 0) {
        const auto o = d.overlay();
        const uint16_t pad = uint16_t((physical & ~o.up) | o.down);
        const uint16_t edge = uint16_t(pad & ~prev);
        prev = pad;
        if ((edge & kR1) && !g.read32(ctl + q::kListCount) && g.read32(ctl + q::kListDirty)) {
            // 341388 -> 33fa20: the list is rebuilt column by column from the hot flags.
            uint32_t n = 0;
            for (int col = 0; col < 8; ++col)
                for (int row = 0; row < 3; ++row)
                    if (g.read32(rec(col + row * 8) + q::kHot))
                        g.write32(list + 8 * n++, uint32_t(col + row * 8));
            g.write32(ctl + q::kListCount, n);
            g.write32(ctl + q::kListDirty, 0);
        }
        const uint32_t count = g.read32(ctl + q::kListCount);
        auto use = [&](uint32_t index) {
            const int cell = int(g.read32(list + index * 8));
            g.write32(rec(cell) + q::kEquipped, 1);
        };
        if ((edge & kR1) && refuseOpens > 0) {
            --refuseOpens;
        } else if (edge & kR1) {
            if (g.read32(ctl + q::kBarShown)) {
                ++steps;
                g.write32(ctl + q::kListIndex, (g.read32(ctl + q::kListIndex) + 1) % count);
            } else if (count == 1 && g.read32(g.read32(rec(int(g.read32(list))) + q::kItem) + q::kItemType) == 10) {
                use(g.read32(ctl + q::kListIndex)); // one weapon: used at once
            } else if (count >= 1) {
                ++opens;
                if (g.read32(ctl + q::kListIndex) >= count)
                    g.write32(ctl + q::kListIndex, 0);
                g.write32(ctl + q::kBarShown, 1);
            }
        }
        if ((edge & kSquare) && g.read32(ctl + q::kBarShown)) {
            ++confirms;
            use(g.read32(ctl + q::kListIndex));
            g.write32(ctl + q::kBarShown, 0);
        }
    }
    // Runs the driver for `frames` updates; slot is pressed on the first one only.
    void run(QuickSlotDriver& d, int slot, int frames, std::string* log, uint16_t physical = 0, bool allowed = true) {
        for (int i = 0; i < frames; ++i) {
            d.tick(g, i == 0 ? slot : 0, allowed, padMgr, log);
            sample(d, physical);
        }
    }
};

int main() {
    // ---- bindings
    {
        const Config d = Config::defaults();
        for (int n = 1; n <= 5; ++n)
            check(quickOf(d, 0x01 + n) == n, "default 1..5 = QUICK1..QUICK5");
        bool six = false;
        for (const auto& b : d.game)
            six = six || (b.src == Binding::Key && b.code == 0x07 && b.act == Binding::Action && b.value == 0x0e);
        check(six, "6 stays the keymap's Quick Inventory");
    }
    {
        // An older file: the shipped slot actions become quick slots, on whatever key carries them.
        const Config c = parsed("[bindings]\nW = LSTICK_UP\n1 = ACT:0x02\n2 = ACT:0x03\n3 = ACT:0x04\n4 = ACT:0x05\n"
                                "5 = ACT:0x06\n6 = ACT:0x0e\nNUMPAD1 = ACT:0x02\nI = ACT:0x17\n");
        for (int n = 1; n <= 5; ++n)
            check(quickOf(c, 0x01 + n) == n, "ACT:0x02..0x06 migrate to QUICK1..QUICK5");
        check(quickOf(c, 0x4F) == 1, "a user's own key for ACT:0x02 becomes QUICK1");
        bool six = false, other = false;
        for (const auto& b : c.game) {
            six = six || (b.src == Binding::Key && b.code == 0x07 && b.act == Binding::Action && b.value == 0x0e);
            other = other || (b.act == Binding::Action && b.value >= 2 && b.value <= 6);
        }
        check(six, "ACT:0x0e is not migrated");
        check(!other, "no slot action is left");
    }
    {
        const Config c = parsed("[bindings]\nZ = quick3\n1 = NONE\nX = QUICK9\n");
        check(quickOf(c, 0x2C) == 3 && quickOf(c, 0x2D) == 9, "QUICKn parses case-insensitively, up to 9");
        check(quickOf(c, 0x02) == 0, "1 = NONE stays unbound");
        Config bad = Config::defaults();
        std::string warn;
        Config::parse("[bindings]\nZ = QUICK0\nX = QUICK10\n", bad, &warn);
        check(!warn.empty(), "QUICK0 / QUICK10 are rejected");
    }
    // ---- native order: column by column (33fa20 / 434288)
    {
        bool hot[q::kGridCells] = {};
        for (int c : {0, 1, 9, 16, 23, 7})
            hot[c] = true;
        const auto order = quickSlotOrder(hot);
        const std::vector<int> want = {0, 16, 1, 9, 7, 23};
        check(order == want, "quick slot order is column-major");
        bool none[q::kGridCells] = {};
        check(quickSlotOrder(none).empty(), "no hot items, no order");
    }
    // ---- live list
    {
        Game game({16, 1, 2});
        const auto list = nativeQuickList(game.g, Game::ctl);
        check(list == std::vector<int>({16, 1, 2}), "the live list is read in bar order");
        game.g.write32(Game::ctl + q::kListCount, 40);
        check(nativeQuickList(game.g, Game::ctl).empty(), "an implausible count reads as empty");
        check(nativePadButton(game.g, Game::padMgr, q::kCmdQuickInventory) == Game::kR1, "Quick Inventory = R1");
        check(nativePadButton(game.g, Game::padMgr, q::kCmdAttack) == Game::kSquare, "Attack = Square, never Start");
        check(nativePadButton(game.g, Game::padMgr, 0x4011) == 0, "an unbound command has no button");
    }
    // ---- driver
    {
        Game game({0, 8, 3});
        QuickSlotDriver d;
        std::string log;
        game.run(d, 3, 40, &log);
        check(game.equipped(3) && !game.equipped(0) && !game.equipped(8), "key 3 uses the third bar item");
        check(game.opens == 1 && game.confirms == 1 && game.steps == 0, "one open, one confirm, no step");
        check(d.phase() == QuickSlotDriver::Phase::Idle, "driver idle again");
        check(game.g.read32(Game::ctl + q::kBarShown) == 0, "bar closed");
        check(log.find("confirmed") != std::string::npos, "logged the confirm");
    }
    {
        Game game({0, 8});
        QuickSlotDriver d;
        std::string log;
        game.prev = Game::kSquare | Game::kR1; // both buttons physically held since before the key
        game.run(d, 2, 40, &log, Game::kSquare | Game::kR1);
        check(game.equipped(8) && game.confirms == 1, "a held button is released first, then pressed");
    }
    {
        Game game({0, 8});
        QuickSlotDriver d;
        std::string log;
        game.run(d, 4, 20, &log);
        check(!game.equipped(0) && !game.equipped(8) && game.opens == 0, "an empty slot does nothing");
        check(log.find("empty") != std::string::npos, "the empty slot is logged");
    }
    {
        Game game({5}, 5); // one weapon: the native R1 uses it without the bar
        QuickSlotDriver d;
        std::string log;
        game.run(d, 1, 30, &log);
        check(game.equipped(5) && game.opens == 0 && game.confirms == 0, "a single weapon is used directly");
        check(d.phase() == QuickSlotDriver::Phase::Idle, "idle after the direct use");
        log.clear();
        game.run(d, 1, 30, &log);
        check(log.find("already equipped") != std::string::npos, "an equipped weapon is not toggled away");
    }
    {
        Game game({0, 8, 3});
        QuickSlotDriver d;
        std::string log;
        // Key repeat / a second press while the first is running is ignored.
        for (int i = 0; i < 40; ++i) {
            d.tick(game.g, i < 6 ? 2 : 0, true, Game::padMgr, &log);
            game.sample(d);
        }
        check(game.opens == 1 && game.confirms == 1, "presses during a running use are ignored");
    }
    {
        Game game({0, 8, 3});
        QuickSlotDriver d;
        std::string log;
        game.g.write32(Game::player + q::kSummonState, 2);
        game.run(d, 1, 20, &log);
        game.g.write32(Game::player + q::kSummonState, 0);
        game.g.write32(q::kScriptedFlag, 1);
        game.run(d, 1, 20, &log);
        game.g.write32(q::kScriptedFlag, 0);
        game.run(d, 1, 20, &log, 0, false); // menu / Arokh / focus loss
        check(game.opens == 0 && !game.equipped(0), "refused while summoning, scripted or not in free gameplay");
    }
    {
        Game game({0, 8, 3});
        QuickSlotDriver d;
        std::string log;
        d.tick(game.g, 2, true, Game::padMgr, &log);
        game.sample(d);
        d.tick(game.g, 0, false, Game::padMgr, &log); // a menu opened mid-way
        check(d.phase() == QuickSlotDriver::Phase::Idle && d.overlay().down == 0 && d.overlay().up == 0,
              "cancel clears the overlay");
    }
    {
        // A refused open is tried again; a lasting refusal gives up.
        Game game({0, 8, 3});
        game.refuseOpens = 2;
        QuickSlotDriver d;
        std::string log;
        game.run(d, 2, 200, &log);
        check(game.equipped(8) && game.confirms == 1 && game.refuseOpens == 0, "a briefly refused open is retried");
        Game busy({0, 8, 3});
        busy.refuseOpens = 100;
        log.clear();
        busy.run(d, 2, 400, &log);
        check(!busy.equipped(8) && busy.confirms == 0 && busy.refuseOpens == 100 - 1 - QuickSlotDriver::kRetries,
              "a lasting refusal gives up after the retries");
        check(log.find("refused") != std::string::npos && d.phase() == QuickSlotDriver::Phase::Idle, "gave up");
    }
    {
        // After a load: an empty list marked for a rebuild. The order comes from the hot flags (column-major).
        Game game({16, 1, 2});
        game.g.write32(Game::ctl + q::kListCount, 0);
        game.g.write32(Game::ctl + q::kListDirty, 1);
        QuickSlotDriver d;
        std::string log;
        game.run(d, 1, 40, &log);
        check(game.equipped(16) && !game.equipped(1) && game.confirms == 1, "key 1 after a load = cell 16 (column 0)");
        check(nativeQuickList(game.g, Game::ctl) == std::vector<int>({16, 1, 2}), "the native rebuild ran");
    }
    {
        // The bar is already shown (the player pressed Q): the key picks its item and confirms.
        Game game({0, 8, 3});
        game.g.write32(Game::ctl + q::kBarShown, 1);
        QuickSlotDriver d;
        std::string log;
        game.run(d, 2, 30, &log);
        check(game.equipped(8) && game.opens == 0 && game.confirms == 1, "a shown bar is confirmed at the slot");
    }
    std::printf("quick_slots_test: %u checks passed\n", checks);
    return 0;
}
