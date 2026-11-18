#include "drakan_save_guard.h"

#include "Kernel/Stubs/MemoryCard.h"
#include "drakan_config.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_region.h"
#include "hle/ModPack.h"
#include "hle/SaveRecord.h"
#include "hle/SaveUpgrade.h"
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
#include <optional>
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
// The installed creature swap removed four NPC classes while keeping their placed object IDs.
constexpr std::string_view kOotfPreviousBuild = "da1e598e3d3adfdd";
constexpr std::string_view kOotfCreatureSwapBuild = "493b53de681b8791";
constexpr hle::mods::SaveClassRemap kOotfCreatureSwaps[] = {
    {"/npc/npcs", 7084, 3040},
    {"/npc/npcs", 7096, 2676},
    {"/npc/npcs", 7109, 5604},
    {"/npc/npcs", 7130, 3134},
};
// Save revisions (SaveUpgrade.h): per content pack with a `save_revision`, its rules. g_mutex.
struct PackRevisions {
    std::string id;
    int revision = 0;
    std::vector<hle::mods::SaveRevisionRule> rules;
    bool migrateOotfCreatures = false;
};
std::vector<PackRevisions> g_revisions;
// What the read filter decided per save file (same size and time = same answer). g_mutex.
enum class UpgradeState { Unchanged, Replaced, Failed };
struct Upgraded {
    std::uintmax_t size = 0;
    fs::file_time_type time{};
    UpgradeState state = UpgradeState::Unchanged;
    std::vector<uint8_t> data;
};
std::map<std::string, Upgraded> g_upgraded;

constexpr char kBookLevel[] = "OotF 02 - Ruined Village Book";
constexpr char kVillageLevel[] = "OotF 01 - Ruined Village";
constexpr char kBookReturnFile[] = "OotF 01 - Ruined Village.rlt";
struct BookReturn {
    fs::path requested;
    std::vector<uint8_t> state;
    bool restored = false;
};
std::optional<BookReturn> g_bookReturn; // g_mutex

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

bool writePackRecord(const fs::path& record, const std::vector<hle::mods::PackStamp>& packs) {
    const fs::path temp = fs::path(record.native() + fs::path(".tmp").native());
    const std::string text = hle::mods::formatSaveRecord(packs);
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    std::error_code ec;
    if (file) {
        fs::rename(temp, record, ec);
        if (!ec)
            return true;
    }
    fs::remove(temp, ec);
    return false;
}

void preserveOotfLevelStamps(const fs::path& writtenPath);

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
    // A level state (`.rlt`, the levels of the slot left earlier) gets a record too: it names the pack revision its
    // content was written with (save revisions, SaveUpgrade.h); the load policy only reads the `.rsg` one.
    if (!isRecordedSave(path) && path.extension() != ".rlt")
        return;
    std::vector<hle::mods::PackStamp> packs;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        packs = hle::mods::recordFor(g_active, path.stem().string());
        g_slotCache.clear();
        g_entryCache.clear();
        g_upgraded.erase(path.string());
    }
    const fs::path record = recordPath(path);
    std::error_code ec;
    if (path.parent_path().filename().string().starts_with(slotPrefix()) &&
        std::any_of(packs.begin(), packs.end(), [](const hle::mods::PackStamp& stamp) {
            return stamp.id == "ootf-levels" && stamp.build == kOotfCreatureSwapBuild;
        })) {
        preserveOotfLevelStamps(path);
    }
    if (packs.empty()) {
        if (fs::remove(record, ec))
            std::fprintf(stderr, "[saves] %s: written without content packs, pack record removed\n",
                         path.filename().string().c_str());
    } else if (writePackRecord(record, packs)) {
        std::string list;
        for (const auto& p : packs)
            list += (list.empty() ? "" : ", ") + p.id + " " + p.build;
        std::fprintf(stderr, "[saves] %s: pack record %s\n", path.filename().string().c_str(), list.c_str());
    } else {
        std::fprintf(stderr, "[saves] %s: the pack record could not be written\n", path.filename().string().c_str());
    }
    // A slot holds one save; a record whose save the game replaced (another level) or deleted goes too.
    for (fs::directory_iterator it(path.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        const std::string name = p.filename().string();
        for (const char* ext : {".rsg", ".rlt"}) {
            const std::string suffix = std::string(ext) + kRecordSuffix;
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                std::error_code e2;
                if (!fs::exists(p.parent_path() / name.substr(0, name.size() - std::strlen(kRecordSuffix)), e2))
                    fs::remove(p, e2);
            }
        }
    }
}

std::string guestString(const uint8_t* rdram, uint32_t addr) {
    std::string s;
    for (uint32_t i = 0; i < 512; ++i) {
        const char ch = static_cast<char>(rdram[(addr + i) & PS2_RAM_MASK]);
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
    std::memcpy(&v, rdram + (a & PS2_RAM_MASK & ~3u), 4);
    return v;
}
void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    std::memcpy(rdram + (a & PS2_RAM_MASK & ~3u), &v, 4);
}
bool okPtr(uint32_t a) {
    a &= 0x1fffffffu;
    return a >= 0x100000u && a < PS2_RAM_SIZE - 0x200u;
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

// --- save revisions (SaveUpgrade.h) -----------------------------------------------------------------------------

PackRevisions revisionsOf(const hle::mods::Manifest& m) {
    PackRevisions pr{m.id, m.saveRevision, {}};
    using Kind = hle::mods::SaveRevisionRule::Kind;
    const auto take = [&](const std::vector<std::string>& lines, Kind kind) {
        for (const std::string& line : lines) {
            hle::mods::SaveRevisionRule r;
            if (hle::mods::parseSaveRevisionRule(line, kind, r) && r.revision <= pr.revision)
                pr.rules.push_back(std::move(r));
            else
                std::fprintf(stderr, "[saves] content pack '%s': save revision line '%s' ignored\n", m.id.c_str(),
                             line.c_str());
        }
    };
    take(m.saveAddedObjects, Kind::AddObjects);
    take(m.saveReplacedEffects, Kind::ReplaceEffects);
    take(m.saveRelinkedPowerUps, Kind::RelinkPowerUps);
    take(m.saveResetTogglers, Kind::ResetTogglers);
    std::fprintf(stderr, "[saves] content pack '%s' save revision %d: %zu upgrade rule(s) for older saves\n",
                 m.id.c_str(), pr.revision, pr.rules.size());
    return pr;
}

struct SavedBuild {
    std::string build;
    int revision = 0;
};
SavedBuild savedBuild(const fs::path& file, const std::string& packId) {
    const auto fromRecord = [&](const fs::path& record) -> SavedBuild {
        std::string text;
        std::vector<hle::mods::PackStamp> packs;
        if (!readText(record, text) || !hle::mods::parseSaveRecord(text, packs))
            return {};
        for (const auto& p : packs)
            if (p.id == packId)
                return {p.build, p.revision};
        return {};
    };
    std::error_code ec;
    if (fs::exists(recordPath(file), ec) || file.extension() != ".rlt")
        return fromRecord(recordPath(file));
    SavedBuild oldest;
    const std::string suffix = std::string(".rsg") + kRecordSuffix;
    for (fs::directory_iterator it(file.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!name.ends_with(suffix))
            continue;
        SavedBuild stamp = fromRecord(it->path());
        if (stamp.build.empty())
            continue;
        if (!oldest.build.empty() && oldest.build != stamp.build)
            return {}; // an unstamped .rlt can only inherit an unambiguous build from its slot
        if (oldest.build.empty() || stamp.revision < oldest.revision)
            oldest = std::move(stamp);
    }
    return oldest;
}

void preserveOotfLevelStamps(const fs::path& writtenPath) {
    // Unvisited levels still contain old records after the slot's new RSG replaces their only inherited pack stamp.
    std::error_code ec;
    for (fs::directory_iterator it(writtenPath.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& path = it->path();
        if (path == writtenPath || path.extension() != ".rlt" || !it->is_regular_file(ec) ||
            fs::exists(recordPath(path), ec) || ec)
            continue;
        const SavedBuild old = savedBuild(path, "ootf-levels");
        if (old.build != kOotfPreviousBuild)
            continue;
        const hle::mods::PackStamp stamp{"ootf-levels", "Order of the Flame", "", old.build, old.revision};
        const bool written = writePackRecord(recordPath(path), {stamp});
        std::fprintf(stderr, "[saves] %s: inherited pack build %s %s\n", path.filename().string().c_str(),
                     old.build.c_str(), written ? "recorded" : "could not be recorded");
    }
}

// Memory card read filter: a save or level state of an older pack revision is read upgraded (SaveUpgrade.h): the level
// objects added since are appended to its tables and the spawned records replaced since get their inert class. The
// file on the card stays as it is; the game's next save writes the upgraded state with the current revision.
UpgradeState upgradeFile(const fs::path& host, std::vector<uint8_t>* data) {
    const std::string ext = host.extension().string();
    if (ext != ".rsg" && ext != ".rlt")
        return UpgradeState::Unchanged;
    std::vector<PackRevisions> revisions;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_revisions.empty())
            return UpgradeState::Unchanged;
        revisions = g_revisions;
    }
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(host, ec);
    const fs::file_time_type time = fs::last_write_time(host, ec);
    const std::string key = host.string(), level = host.stem().string(), name = host.filename().string();
    std::vector<const hle::mods::SaveRevisionRule*> rules;
    std::span<const hle::mods::SaveClassRemap> remaps;
    std::string about;
    using Kind = hle::mods::SaveRevisionRule::Kind;
    const hle::mods::SaveRevisionRule statue{Kind::AddObjects, 1, "OotF 01 - Ruined Village", {3353}, {2855}};
    for (const PackRevisions& pr : revisions) {
        const SavedBuild saved = savedBuild(host, pr.id);
        const int rev = saved.revision;
        if (pr.migrateOotfCreatures && saved.build == kOotfPreviousBuild) {
            remaps = kOotfCreatureSwaps;
            if (rev == 0 && hle::mods::ruleLevelMatches(statue.level, level))
                rules.push_back(&statue);
            about = "Order of the Flame build " + saved.build;
        }
        if (rev >= pr.revision)
            continue;
        for (const Kind kind : {Kind::AddObjects, Kind::ReplaceEffects, Kind::RelinkPowerUps, Kind::ResetTogglers})
            for (const auto* r : hle::mods::rulesFor(pr.rules, kind, level, rev))
                rules.push_back(r);
        about += (about.empty() ? "" : ", ") + ("pack '" + pr.id + "' revision ") + std::to_string(rev) + " < " +
                 std::to_string(pr.revision);
    }
    if (rules.empty() && remaps.empty())
        return UpgradeState::Unchanged;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_upgraded.find(key);
        if (it != g_upgraded.end() && it->second.size == size && it->second.time == time) {
            if (data && it->second.state == UpgradeState::Replaced)
                *data = it->second.data;
            return it->second.state;
        }
    }
    Upgraded u{size, time};
    std::vector<uint8_t> in;
    {
        std::ifstream f(host, std::ios::binary);
        in.assign(std::istreambuf_iterator<char>(f), {});
    }
    hle::mods::SaveUpgradeReport report;
    std::string err;
    if (!hle::mods::upgradeSave(in, rules, u.data, report, err, remaps)) {
        std::fprintf(stderr, "[saves] %s (%s): upgrade failed (%s)\n", name.c_str(), about.c_str(), err.c_str());
        u.state = UpgradeState::Failed;
    } else if (!report.changed()) {
        std::fprintf(stderr, "[saves] %s (%s): nothing to upgrade\n", name.c_str(), about.c_str());
    } else {
        std::string list;
        for (uint32_t id : report.added)
            list += (list.empty() ? "" : " ") + std::to_string(id);
        std::fprintf(stderr,
                     "[saves] %s (%s): upgraded on load: level objects added [%s], %zu spawned record(s) made inert, "
                     "%zu dragon(s) given their power-ups again, %zu toggler(s) reset, %zu creature class(es) mapped\n",
                     name.c_str(), about.c_str(), list.c_str(), report.replaced, report.relinked, report.togglers,
                     report.reclassified);
        u.state = UpgradeState::Replaced;
    }
    if (u.state != UpgradeState::Replaced)
        u.data.clear();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_upgraded.size() > 64)
        g_upgraded.clear();
    g_upgraded[key] = u;
    if (data && u.state == UpgradeState::Replaced)
        *data = std::move(u.data);
    return u.state;
}
ps2_stubs::McReadResult upgradeOnRead(const fs::path& host, std::vector<uint8_t>& data) {
    using ps2_stubs::McReadResult;
    if (host.extension() != ".rsg" && host.extension() != ".rlt")
        return McReadResult::Unchanged;

    if (host.filename() == kBookReturnFile && drakan_game::current() == drakan_game::Game::Ootf) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_bookReturn && g_bookReturn->requested == host) {
            data = g_bookReturn->state;
            return McReadResult::Replaced;
        }
    }
    std::error_code ec;
    if (!fs::is_regular_file(host, ec))
        return McReadResult::Unchanged;

    switch (upgradeFile(host, &data)) {
        case UpgradeState::Unchanged:
            return McReadResult::Unchanged;
        case UpgradeState::Replaced:
            return McReadResult::Replaced;
        case UpgradeState::Failed:
            return McReadResult::Refused;
    }

    return McReadResult::Refused;
}

void clearBookReturn() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bookReturn.reset();
}

void onBeforeSaveRestore(const drakan_lifecycle::Event& event) {
    const int port = event.path.starts_with("mc1:") || event.path.starts_with("MC1:");
    const fs::path path = ps2_stubs::mcHostPath(port, event.path);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_bookReturn && g_bookReturn->requested != path)
        g_bookReturn.reset();
}

void onAfterSaveRestore(const drakan_lifecycle::Event& event) {
    const int port = event.path.starts_with("mc1:") || event.path.starts_with("MC1:");
    const fs::path path = ps2_stubs::mcHostPath(port, event.path);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (event.ok && g_bookReturn && g_bookReturn->requested == path) {
            g_bookReturn->restored = true;
            return;
        }
        g_bookReturn.reset();
    }
    if (!event.ok || drakan_game::current() != drakan_game::Game::Ootf ||
        !hle::mods::ruleLevelMatches(kBookLevel, event.level))
        return;

    const fs::path slot = path.parent_path();
    const fs::path root = ps2_stubs::mcHostPath(port, "/");
    if (!isRecordedSave(path) || slot.parent_path() != root || !slot.filename().string().starts_with(slotPrefix()))
        return;

    const fs::path companion = slot / kBookReturnFile;
    std::error_code ec;
    std::string text;
    hle::mods::SaveCheckInput in;
    in.level = kVillageLevel;
    in.hasRecord = true;
    if (!fs::is_regular_file(companion, ec) || !readText(recordPath(companion), text) ||
        !hle::mods::parseSaveRecord(text, in.record) ||
        std::none_of(in.record.begin(), in.record.end(),
                     [](const hle::mods::PackStamp& stamp) { return stamp.id == "ootf-levels"; }))
        return;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const hle::mods::SaveCheck check = hle::mods::checkSave(in, g_active);
        if (check.verdict != SaveVerdict::Ok || check.overridden)
            return;
    }

    std::vector<uint8_t> state;
    const UpgradeState upgrade = upgradeFile(companion, &state);
    if (upgrade == UpgradeState::Failed)
        return;
    if (upgrade == UpgradeState::Unchanged) {
        std::ifstream file(companion, std::ios::binary);
        state.assign(std::istreambuf_iterator<char>(file), {});
        std::vector<uint8_t> unused;
        hle::mods::SaveUpgradeReport report;
        std::string error;
        if (!file || !hle::mods::upgradeSave(state, {}, unused, report, error))
            return;
    }

    // The native Book exit probes, restores and rereads the card-root state before writing its new level-start save.
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bookReturn = BookReturn{root / kBookReturnFile, std::move(state)};
    std::fprintf(stderr, "[saves] Book return will use the paired Village state in %s\n",
                 slot.filename().string().c_str());
}

bool deleteRestoredBookReturn(const fs::path& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_bookReturn || !g_bookReturn->restored || g_bookReturn->requested != path)
        return false;
    g_bookReturn.reset();
    return true;
}

void onAfterSaveWrite(const drakan_lifecycle::Event& event) {
    if (event.level != kVillageLevel)
        return;
    const int port = event.path.starts_with("mc1:") || event.path.starts_with("MC1:");
    const fs::path path = ps2_stubs::mcHostPath(port, event.path);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_bookReturn && g_bookReturn->restored && path.parent_path() == g_bookReturn->requested.parent_path() &&
        path.extension() == ".rsg")
        g_bookReturn.reset();
}
} // namespace

void configure(const hle::mods::ModManager* mods) {
    hle::mods::ActivePacks active;
    std::vector<PackRevisions> revisions;
    if (mods) {
        for (const hle::mods::ModStatus& st : mods->status()) {
            if (st.state != hle::mods::ModState::Loaded || !st.content)
                continue;
            active.packs.push_back({st.manifest.id, st.manifest.name, st.manifest.version, st.build});
            active.packs.back().revision = st.manifest.saveRevision;
            if (st.manifest.saveScope == "levels")
                active.levelScoped.push_back(st.manifest.id);
            const bool migrateOotfCreatures = st.manifest.id == "ootf-levels" && st.build == kOotfCreatureSwapBuild;
            if (st.manifest.saveRevision > 0 || migrateOotfCreatures) {
                revisions.push_back(revisionsOf(st.manifest));
                revisions.back().migrateOotfCreatures = migrateOotfCreatures;
            }
            if (migrateOotfCreatures)
                active.compatibleBuilds.push_back({st.manifest.id, std::string(kOotfPreviousBuild)});
        }
        for (const hle::mods::ExtraLevel& l : mods->extraLevels()) {
            const auto st = std::find_if(mods->status().begin(), mods->status().end(),
                                         [&](const hle::mods::ModStatus& s) { return s.manifest.id == l.modId; });
            active.levels.push_back({l.name, l.modId, st != mods->status().end() && st->buildDeclared});
        }
    }
    // [general] save_pack_check = 0: saves of another pack build load anyway (the player's choice; they may misbehave).
    active.loadOtherBuilds = !drakan_cfg::getBool("general", "save_pack_check", "DRAKAN_SAVE_PACK_CHECK", true);
    for (const auto& p : active.packs) {
        const bool scoped =
            std::find(active.levelScoped.begin(), active.levelScoped.end(), p.id) != active.levelScoped.end();
        std::fprintf(stderr, "[saves] content pack '%s' build %s: %s\n", p.id.c_str(), p.build.c_str(),
                     scoped ? "saves of its own levels record it" : "saves record it");
    }
    if (active.loadOtherBuilds)
        std::fprintf(stderr, "[saves] save_pack_check = 0: saves of other pack builds load anyway\n");
    std::lock_guard<std::mutex> lock(g_mutex);
    g_active = std::move(active);
    g_revisions = std::move(revisions);
    g_upgraded.clear();
    g_bookReturn.reset();
    g_slotCache.clear();
    g_entryCache.clear();
}

void install(PS2Runtime& runtime) {
    ps2_stubs::setMcFileWrittenCallback(&written, ps2_stubs::McWriteObserver::SaveRecords);
    ps2_stubs::setMcReadFilter(&upgradeOnRead);
    if (drakan_lifecycle::installed()) {
        using drakan_lifecycle::Kind;
        ps2_stubs::setMcDeleteVirtual(&deleteRestoredBookReturn);
        drakan_lifecycle::subscribe(Kind::BeforeSaveRestore, onBeforeSaveRestore);
        drakan_lifecycle::subscribe(Kind::AfterSaveRestore, onAfterSaveRestore);
        drakan_lifecycle::subscribe(Kind::AfterSaveWrite, onAfterSaveWrite);
        drakan_lifecycle::subscribe(Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event& event) {
            if (!event.fromSave || event.level != kVillageLevel)
                clearBookReturn();
        });
    }
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
    if (r.verdict == SaveVerdict::Ok && r.needsUpgrade && upgradeFile(rsg, nullptr) == UpgradeState::Failed)
        r.verdict = SaveVerdict::BadRecord;
    if (r.overridden) { // loads anyway: say so once per save
        static std::set<std::string> warned;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (warned.insert(rsg.string()).second)
            std::fprintf(stderr, "[saves] %s: %s (pack '%s' build %s), loaded anyway (save_pack_check = 0)\n",
                         rsg.filename().string().c_str(), hle::mods::saveVerdictName(r.verdict), r.pack.id.c_str(),
                         r.pack.build.c_str());
        return out;
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
