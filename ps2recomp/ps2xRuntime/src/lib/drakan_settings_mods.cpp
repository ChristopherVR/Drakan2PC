// Drakan: the Mods page of the PC settings (a snapshot of the mod manager taken at start-up, edited through
// dsettings::ModsModel) and the title's new mod packs card.
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace drakan_settings_host::detail {

namespace {
dsettings::ModsModel g_modsModel;
std::atomic<bool> g_modsNotice{false}; // the title shows the new packs card in this run (sticky)

// Shows `dir` in the platform's file browser (created first). False when this platform cannot.
bool openFolderInBrowser(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
#if defined(_WIN32)
    const std::filesystem::path abs = std::filesystem::absolute(dir, ec);
    std::wstring cmd = L"explorer.exe \"" + abs.wstring() + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#elif defined(__EMSCRIPTEN__)
    return false;
#else
    const pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", dir.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return pid > 0;
#endif
}

std::string joinProfiles(const hle::mods::Manifest& m) {
    std::string t;
    for (const std::string& p : m.profiles)
        t += (t.empty() ? "" : ", ") + (p == "*" ? std::string("any disc") : p);
    if (t.empty() && !m.profileHashes.empty())
        t = "one disc release";
    return t;
}

// The pack's picture for the Mods page: written next to the exe like the map pictures (previews/mods/pack-<key>.png,
// rewritten only when it changed) and found by name like them. Returns its picture id.
uint32_t writePackCover(const std::string& key, const std::vector<uint8_t>& cover) {
    const std::string safe = mapSafeName(key);
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(drakan_cfg::exeDir()) / "previews" / "mods";
    const std::filesystem::path f = dir / ("pack-" + safe + ".png");
    std::vector<char> have;
    if (std::ifstream in(f, std::ios::binary); in)
        have.assign(std::istreambuf_iterator<char>(in), {});
    if (have.size() != cover.size() || !std::equal(have.begin(), have.end(), cover.begin())) {
        std::filesystem::create_directories(dir, ec);
        std::ofstream(f, std::ios::binary)
            .write(reinterpret_cast<const char*>(cover.data()), static_cast<std::streamsize>(cover.size()));
    }
    return mapPreviewId("pack-" + safe);
}

void snapshotMods(const hle::mods::ModManager* mm, const std::string& dir) {
    std::vector<dsettings::ModInfo> mods;
    std::vector<dsettings::ModConflict> conflicts;
    if (mm) {
        std::map<std::string, std::string> keyOfId;
        for (const hle::mods::ModStatus& st : mm->status()) {
            dsettings::ModInfo m;
            m.key = st.key;
            m.hasManifest = st.hasManifest;
            m.found = !st.source.empty();
            m.enabled = m.bootEnabled = st.enabled;
            m.isNew = st.isNew;
            m.boot = st.state == hle::mods::ModState::Loaded     ? dsettings::ModBoot::Active
                     : st.state == hle::mods::ModState::Disabled ? dsettings::ModBoot::Disabled
                                                                 : dsettings::ModBoot::Skipped;
            m.message = st.message;
            m.name = st.key;
            m.readError = st.readError;
            m.file = st.source.empty() ? st.key : std::filesystem::path(st.source).filename().string();
            if (!st.cover.empty())
                m.picture = writePackCover(st.key, st.cover);
            if (st.hasManifest) {
                const hle::mods::Manifest& mf = st.manifest;
                if (!mf.name.empty())
                    m.name = mf.name;
                m.id = mf.id;
                m.version = mf.version;
                m.author = mf.author;
                m.description = mf.description;
                m.target = joinProfiles(mf);
                m.campaign = mf.campaign;
                keyOfId[mf.id] = st.key;
            }
            mods.push_back(std::move(m));
        }
        auto keyFor = [&](const std::string& id) {
            auto it = keyOfId.find(id);
            return it == keyOfId.end() ? id : it->second;
        };
        for (const hle::mods::Conflict& c : mm->conflicts())
            conflicts.push_back({c.what, keyFor(c.winner), keyFor(c.loser)});
    }
    g_modsModel.setSnapshot(dir, std::move(mods), std::move(conflicts));
    g_modsModel.setOpenFolder(&openFolderInBrowser);
    // The new packs card (docs/reference/modding.md "New packs"): never under a scripted input run unless
    // DRAKAN_MODS_NOTICE=1 asks for it; DRAKAN_MODS_NOTICE=0 turns it off.
    const char* script = std::getenv("DRAKAN_INPUT_SCRIPT");
    const bool show = dsettings::showModNotice(
        g_modsModel, dsettings::modNoticeModeFrom(std::getenv("DRAKAN_MODS_NOTICE")), script && *script, false);
    g_modsNotice.store(show);
    for (const dsettings::ModInfo* m : g_modsModel.undecided())
        std::fprintf(stderr, "[mods] new pack '%s' (%s): off until enabled; title notice %s\n", m->key.c_str(),
                     m->name.c_str(), show ? "on" : "off");
    for (const dsettings::ModInfo* m : dsettings::unreadablePacks(g_modsModel))
        std::fprintf(stderr, "[mods] pack '%s' cannot be read (%s): named on the title screen\n", m->file.c_str(),
                     m->readError.c_str());
    for (const dsettings::ModInfo* m : dsettings::missingPacks(g_modsModel))
        std::fprintf(stderr,
                     "[mods] pack '%s' is enabled in mods.ini but missing from the mods folder: named on the title "
                     "screen\n",
                     m->key.c_str());
}
} // namespace

dsettings::ModsModel& harnessMods() {
    return g_modsModel;
}

// The title's new packs card answered: write mods.ini (and restart for Enable).
void titleModsAction(const gameui::TitleAction& a) {
    std::string err;
    switch (a.kind) {
        case gameui::TitleActionKind::ModsEnable: {
            const size_t n = g_modsModel.enableUndecided();
            const bool ok = g_modsModel.save(&err);
            std::fprintf(stderr, "[mods] title notice: enabled %zu new pack(s)%s%s\n", n,
                         ok ? "" : ", not saved: ", err.c_str());
            if (ok) { // the packs are installed before the first resource request: they load in the new process
                relaunchSelf();
                drakan_win::requestClose();
            }
            break;
        }
        case gameui::TitleActionKind::ModsKeepOff: {
            const size_t n = g_modsModel.keepUndecidedOff();
            const bool ok = g_modsModel.save(&err);
            std::fprintf(stderr, "[mods] title notice: kept %zu new pack(s) off%s%s\n", n,
                         ok ? "" : ", not saved: ", err.c_str());
            break;
        }
        case gameui::TitleActionKind::OpenMods:
            std::fprintf(stderr, "[mods] title notice: Mods page (%s)\n", a.value ? "title pages" : "overlay");
            if (!a.value) {
                requestOpenMods();
            }
            break;
        default:
            break;
    }
}
} // namespace drakan_settings_host::detail

namespace drakan_settings_host {
using namespace detail;

void setModManager(const hle::mods::ModManager* mods, const std::string& modsDir) {
    snapshotMods(mods, modsDir);
}
void setGameChoice(const std::string& setting, bool fromEnv, bool offered, const std::vector<std::string>& campaigns,
                   const std::vector<std::pair<std::string, std::string>>& refused) {
    dsettings::GameChoiceState s;
    s.setting = setting;
    s.settingFromEnv = fromEnv;
    s.offered = offered;
    s.cardCampaigns = campaigns;
    for (const auto& [id, why] : refused)
        s.refused[id] = why;
    g_modsModel.setGameChoice(std::move(s));
    const std::string why =
        dsettings::gameChoiceNoteText(dsettings::gameChoiceNote(g_modsModel, g_modsModel.gameChoice()), false);
    if (!why.empty())
        std::fprintf(stderr, "[game] %s\n", why.c_str());
}
bool modsNoticeWanted() {
    return g_modsNotice.load();
}
} // namespace drakan_settings_host
#endif // PS2X_HAVE_DRAKAN_SETTINGS
