#include "drakan_save_guard.h"

#include "Kernel/Stubs/MemoryCard.h"
#include "drakan_region.h"
#include "hle/ModPack.h"
#include "hle/SaveRecord.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "settings/Hud.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace drakan_save_guard {
namespace {
namespace fs = std::filesystem;
using hle::mods::SaveVerdict;
using Clock = std::chrono::steady_clock;

// SCES-500.06 (the USA address comes from the region table): the native save check, (path) -> 0 ok. Its first step
// asks the card manager whether the file is a valid save and returns 1 at once when it is not; 43b4d0 then shows the
// "could not be loaded" message (property 0x105) and opens the title, as for a damaged save.
constexpr uint32_t kValidateSave = 0x23f170u;
constexpr const char* kRecordSuffix = ".mods";
// The native Load screen (screen 4; docs/journal/2026-10-25/modern-save-load-title.md 1.3): its message 2 (Enter,
// Cross, a click on a slot) runs 43d028(screen, widget), which asks in game (box 0x107) and loads on the title. For a
// refused save it opens the screen's own message box instead, as the slot screen does for "no memory card": 428568
// (f12 = -1.0f, parent, text property, default widget) creates the box (screen 0x1b) and returns it; the caller then
// sets box +0xbc (2: the box closes only when the card goes) and +0xb4 (the screen's callback node, screen + 0xc8).
// Default widget OK (0x238c) = a box with OK alone. Its answer (message 0x10, screen +0xa0 = 0) does nothing else.
constexpr uint32_t kLoadAsk = 0x43d028u;
constexpr uint32_t kFnMessageBox = 0x428568u;
constexpr uint32_t kPropLoadFailed = 0x105u; // the game's own "could not be loaded" text (43b4d0's failure path)
constexpr uint32_t kWidgetOk = 0x238cu;
constexpr uint32_t kScreenId = 0x18u, kScreenLoad = 4u;
constexpr uint32_t kFlags = 0x9cu, kBoxState = 0xa0u, kSelection = 0xa4u, kEntries = 0xb8u, kCount = 0xc0u;
constexpr uint32_t kEntrySize = 0x168u, kCallbackNode = 0xc8u, kBoxCardMode = 0xbcu, kBoxCallback = 0xb4u;

std::mutex g_mutex;
hle::mods::ActivePacks g_active;
struct Cached {
    Verdict verdict;
    Clock::time_point at;
};
std::map<int, Cached> g_slotCache;
std::map<std::string, Cached> g_entryCache;
std::string g_notice;
Clock::time_point g_noticeUntil;
PS2Runtime::RecompiledFunction g_origValidate = nullptr;
PS2Runtime::RecompiledFunction g_origLoadAsk = nullptr;
// The box call's return goes through a free slot of the function table (the trampoline), which finishes the box as
// 43d028 does and returns to 43d028's caller. GUI thread only; one box at a time.
uint32_t g_trampoline = 0;
struct PendingBox {
    bool busy = false;
    uint32_t screen = 0, ra = 0;
} g_box;

fs::path cardRoot() {
    const auto& io = PS2Runtime::getIoPaths();
    return io.mcRoot.empty() ? io.elfDirectory / "mc0" : io.mcRoot;
}
std::string slotPrefix() {
    return std::string(drakan_region::build().saveId) + "Slot";
}
// Every save the game writes gets a record: the slot saves (manual, quick, level change) and the level-start save at
// the card root (`/<level>.rsg`), which the game reloads when the player dies. Without a record that one was refused as
// "made before pack builds were recorded" in every pack level, so a death in a converted level ended at the title.
bool isRecordedSave(const fs::path& rsg) {
    return rsg.extension() == ".rsg";
}
fs::path recordPath(const fs::path& rsg) {
    return fs::path(rsg.native() + fs::path(kRecordSuffix).native());
}
bool readText(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return !f.bad();
}

gameui::SavePackProblem problemOf(SaveVerdict v) {
    switch (v) {
        case SaveVerdict::Ok:
            return gameui::SavePackProblem::None;
        case SaveVerdict::DifferentBuild:
            return gameui::SavePackProblem::DifferentBuild;
        case SaveVerdict::MissingPack:
            return gameui::SavePackProblem::MissingPack;
        case SaveVerdict::OlderThanPack:
            return gameui::SavePackProblem::OlderThanPack;
        case SaveVerdict::BadRecord:
            return gameui::SavePackProblem::BadRecord;
    }
    return gameui::SavePackProblem::None;
}

// After every successful write on the card: the pack record of a save (slot folder or card root).
void written(const fs::path& path) {
    if (!isRecordedSave(path))
        return;
    std::vector<hle::mods::PackStamp> packs;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        packs = hle::mods::recordFor(g_active);
        g_slotCache.clear();
        g_entryCache.clear();
    }
    const fs::path record = recordPath(path);
    std::error_code ec;
    if (packs.empty()) {
        if (fs::remove(record, ec))
            std::fprintf(stderr, "[saves] %s: written without content packs, pack record removed\n",
                         path.filename().string().c_str());
    } else {
        const fs::path temp = fs::path(record.native() + fs::path(".tmp").native());
        {
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            const std::string text = hle::mods::formatSaveRecord(packs);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        fs::rename(temp, record, ec);
        if (ec) {
            fs::remove(temp, ec);
            std::fprintf(stderr, "[saves] %s: the pack record could not be written\n",
                         path.filename().string().c_str());
        } else {
            std::string list;
            for (const auto& p : packs)
                list += (list.empty() ? "" : ", ") + p.id + " " + p.build;
            std::fprintf(stderr, "[saves] %s: pack record %s\n", path.filename().string().c_str(), list.c_str());
        }
    }
    // A slot holds one save; a record whose save the game replaced (another level) or deleted goes too.
    for (fs::directory_iterator it(path.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        const std::string name = p.filename().string(), suffix = std::string(".rsg") + kRecordSuffix;
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            std::error_code e2;
            if (!fs::exists(p.parent_path() / name.substr(0, name.size() - std::strlen(kRecordSuffix)), e2))
                fs::remove(p, e2);
        }
    }
}

std::string guestString(const uint8_t* rdram, uint32_t addr) {
    std::string s;
    for (uint32_t i = 0; i < 512; ++i) {
        const char ch = static_cast<char>(rdram[(addr + i) & 0x01FFFFFFu]);
        if (!ch)
            break;
        s.push_back(ch);
    }
    return s;
}

void hookValidate(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const std::string guest = guestString(rdram, getRegU32(c, 4));
    const int port = guest.size() > 3 && (guest.compare(0, 4, "mc1:") == 0 || guest.compare(0, 4, "MC1:") == 0);
    const fs::path host = ps2_stubs::mcHostPath(port, guest);
    const Verdict v = checkSave(host);
    if (!v.refused()) {
        g_origValidate(rdram, c, rt); // tail call: nothing runs after it
        return;
    }
    std::fprintf(stderr, "[saves] load of '%s' refused before the restore (%s)\n", guest.c_str(),
                 host.filename().string().c_str());
    showNotice(v.message);
    SET_GPR_S32(c, 2, 1); // what the native check answers for a file that is not a valid save
    c->pc = getRegU32(c, 31);
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    uint32_t v;
    std::memcpy(&v, rdram + (a & 0x01FFFFFCu), 4);
    return v;
}
void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    std::memcpy(rdram + (a & 0x01FFFFFCu), &v, 4);
}
bool okPtr(uint32_t a) {
    a &= 0x1fffffffu;
    return a >= 0x100000u && a < 0x02000000u - 0x200u;
}

// 43d028(screen, widget): Load screen confirm. A refused save never reaches the in-game question or the load: the
// player reads the native message, the reason shows in the notice band, and the screen and the game stay as they are.
void hookLoadAsk(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t screen = getRegU32(c, 4) & 0x1fffffffu;
    if (c->pc != drakan_region::guestAddress(kLoadAsk) || !okPtr(screen) || g_box.busy || !g_trampoline ||
        rd32(rdram, screen + kScreenId) != kScreenLoad) {
        g_origLoadAsk(rdram, c, rt);
        return;
    }
    const uint32_t sel = rd32(rdram, screen + kSelection), count = rd32(rdram, screen + kCount);
    const uint32_t entries = rd32(rdram, screen + kEntries) & 0x1fffffffu;
    if (sel >= count || sel >= 4 || !okPtr(entries)) {
        g_origLoadAsk(rdram, c, rt);
        return;
    }
    const std::string path = guestString(rdram, entries + sel * kEntrySize);
    const Verdict v = path.empty() ? Verdict{} : checkEntry(path);
    if (!v.refused()) {
        g_origLoadAsk(rdram, c, rt);
        return;
    }
    std::fprintf(stderr, "[saves] native Load screen: '%s' refused, the screen stays (message box 0x%x)\n",
                 path.c_str(), kPropLoadFailed);
    showNotice(v.message);
    uint16_t flags;
    std::memcpy(&flags, rdram + screen + kFlags, 2);
    flags |= 2u; // a box is up (as 43d028 marks its question): the screen's own card check waits for it
    std::memcpy(rdram + screen + kFlags, &flags, 2);
    wr32(rdram, screen + kBoxState, 0); // its answer arms nothing (not 5 = load, not 2 = close the screen)
    g_box = {true, screen, getRegU32(c, 31)};
    const uint32_t minusOne = 0xbf800000u;
    std::memcpy(&c->f[12], &minusOne, 4);
    SET_GPR_U32(c, 5, kPropLoadFailed);
    SET_GPR_U32(c, 6, kWidgetOk);
    SET_GPR_U32(c, 31, g_trampoline);
    c->pc = drakan_region::guestAddress(kFnMessageBox); // a tail call on the real context: the box may block
}

void trampoline(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    const uint32_t box = getRegU32(c, 2) & 0x1fffffffu;
    if (g_box.busy && okPtr(box)) {
        wr32(rdram, box + kBoxCardMode, 2);
        wr32(rdram, box + kBoxCallback, g_box.screen + kCallbackNode);
    } else {
        std::fprintf(stderr, "[saves] native Load screen: the message box was not created\n");
    }
    const uint32_t ra = g_box.ra;
    g_box = {};
    SET_GPR_U32(c, 2, 0); // what 43d028 returns
    c->pc = ra;
}
} // namespace

void configure(const hle::mods::ModManager* mods) {
    hle::mods::ActivePacks active;
    if (mods) {
        for (const hle::mods::ModStatus& st : mods->status()) {
            if (st.state != hle::mods::ModState::Loaded || !st.content)
                continue;
            active.packs.push_back({st.manifest.id, st.manifest.name, st.manifest.version, st.build});
        }
        for (const hle::mods::ExtraLevel& l : mods->extraLevels()) {
            const auto st = std::find_if(mods->status().begin(), mods->status().end(),
                                         [&](const hle::mods::ModStatus& s) { return s.manifest.id == l.modId; });
            active.levels.push_back({l.name, l.modId, st != mods->status().end() && st->buildDeclared});
        }
    }
    for (const auto& p : active.packs)
        std::fprintf(stderr, "[saves] content pack '%s' build %s: saves record it\n", p.id.c_str(), p.build.c_str());
    std::lock_guard<std::mutex> lock(g_mutex);
    g_active = std::move(active);
    g_slotCache.clear();
    g_entryCache.clear();
}

void install(PS2Runtime& runtime) {
    ps2_stubs::setMcFileWrittenCallback(&written, ps2_stubs::McWriteObserver::SaveRecords);
    const uint32_t addr = drakan_region::guestAddress(kValidateSave);
    if (!runtime.hasFunction(addr)) {
        std::fprintf(stderr,
                     "[saves] save check (0x%x) not found: pack records are written but not checked by the "
                     "native load\n",
                     addr);
        return;
    }
    g_origValidate = runtime.lookupFunction(addr);
    runtime.replaceFunction(addr, &hookValidate);
    // The native Load screen's confirm (save_menu = native): refused on the screen instead of at the native check.
    const uint32_t ask = drakan_region::guestAddress(kLoadAsk), box = drakan_region::guestAddress(kFnMessageBox);
    if (!runtime.hasFunction(ask) || !runtime.hasFunction(box)) {
        std::fprintf(stderr, "[saves] native Load screen confirm (0x%x) not found: refused there by the save check\n",
                     ask);
        return;
    }
    for (uint32_t a = 0x100000u; a < 0x100100u && !g_trampoline; a += 4) // a table slot no guest code reaches
        if (!runtime.hasFunction(a) && runtime.registerFunction(a, &trampoline))
            g_trampoline = a;
    if (!g_trampoline) {
        std::fprintf(stderr, "[saves] no free function slot: the native Load screen is refused by the save check\n");
        return;
    }
    g_origLoadAsk = runtime.lookupFunction(ask);
    runtime.replaceFunction(ask, &hookLoadAsk);
}

Verdict checkSave(const fs::path& rsg) {
    Verdict out;
    std::error_code ec;
    if (rsg.empty() || !fs::is_regular_file(rsg, ec))
        return out;
    hle::mods::SaveCheckInput in;
    in.level = rsg.stem().string();
    std::string text;
    const fs::path record = recordPath(rsg);
    if (fs::exists(record, ec)) {
        in.hasRecord = true;
        in.recordReadable = readText(record, text) && hle::mods::parseSaveRecord(text, in.record);
    }
    hle::mods::SaveCheck r;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        r = hle::mods::checkSave(in, g_active);
    }
    out.problem = problemOf(r.verdict);
    if (!out.refused())
        return out;
    out.pack = r.pack.name.empty() ? r.pack.id : r.pack.name;
    out.message = gameui::savePackProblemText(out.problem, out.pack);
    { // once per save and verdict: the native grid asks every second
        static std::set<std::string> logged;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!logged.insert(rsg.string() + "|" + hle::mods::saveVerdictName(r.verdict)).second)
            return out;
    }
    std::fprintf(stderr, "[saves] %s: %s (pack '%s' build %s)\n", rsg.filename().string().c_str(),
                 hle::mods::saveVerdictName(r.verdict), r.pack.id.c_str(), r.pack.build.c_str());
    return out;
}

Verdict checkSlot(int slot) {
    const auto now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_slotCache.find(slot);
        if (it != g_slotCache.end() && now - it->second.at < std::chrono::seconds(1))
            return it->second.verdict;
    }
    Verdict v;
    std::error_code ec;
    for (fs::directory_iterator it(cardRoot() / (slotPrefix() + std::to_string(slot)), ec), end; !ec && it != end;
         it.increment(ec))
        if (it->path().extension() == ".rsg" && it->is_regular_file(ec)) {
            v = checkSave(it->path());
            break;
        }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_slotCache[slot] = {v, now};
    return v;
}

Verdict checkEntry(const std::string& guestPath) {
    const auto now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_entryCache.find(guestPath);
        if (it != g_entryCache.end() && now - it->second.at < std::chrono::seconds(1))
            return it->second.verdict;
    }
    const int port =
        guestPath.size() > 3 && (guestPath.compare(0, 4, "mc1:") == 0 || guestPath.compare(0, 4, "MC1:") == 0);
    const Verdict v = guestPath.empty() ? Verdict{} : checkSave(ps2_stubs::mcHostPath(port, guestPath));
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_entryCache.size() > 64)
        g_entryCache.clear();
    g_entryCache[guestPath] = {v, now};
    return v;
}

void showNotice(const std::string& text) {
    if (text.empty())
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notice = text;
    g_noticeUntil = Clock::now() + std::chrono::seconds(8);
}

void installHudLayer() {
    dsettings::HudLayer layer;
    layer.active = [] {
        std::lock_guard<std::mutex> lock(g_mutex);
        return !g_notice.empty() && Clock::now() < g_noticeUntil;
    };
    layer.draw = [](const dsettings::HudView& view) {
        std::string text;
        float left = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            text = g_notice;
            left = std::chrono::duration<float>(g_noticeUntil - Clock::now()).count();
        }
        dsettings::drawNoticeBand(text, std::clamp(left / 0.5f, 0.0f, 1.0f), view.w, view.h);
    };
    dsettings::hudLayers().set(dsettings::HudSlot::Notice, std::move(layer));
}

} // namespace drakan_save_guard
