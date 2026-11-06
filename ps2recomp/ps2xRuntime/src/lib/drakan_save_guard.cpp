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

std::mutex g_mutex;
hle::mods::ActivePacks g_active;
struct Cached {
    Verdict verdict;
    Clock::time_point at;
};
std::map<int, Cached> g_slotCache;
std::string g_notice;
Clock::time_point g_noticeUntil;
PS2Runtime::RecompiledFunction g_origValidate = nullptr;

fs::path cardRoot() {
    const auto& io = PS2Runtime::getIoPaths();
    return io.mcRoot.empty() ? io.elfDirectory / "mc0" : io.mcRoot;
}
std::string slotPrefix() {
    return std::string(drakan_region::build().saveId) + "Slot";
}
bool isSlotSave(const fs::path& rsg) {
    return rsg.extension() == ".rsg" && rsg.parent_path().filename().string().rfind(slotPrefix(), 0) == 0;
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

// After every successful write on the card: the pack record of a save in a slot folder.
void written(const fs::path& path) {
    if (!isSlotSave(path))
        return;
    std::vector<hle::mods::PackStamp> packs;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        packs = hle::mods::recordFor(g_active);
        g_slotCache.clear();
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
