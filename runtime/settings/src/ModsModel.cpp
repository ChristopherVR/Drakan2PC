#include "settings/ModsModel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace dsettings {

namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

// A mods.ini line that names a pack ("key = on"): not blank, not a comment, not a section header.
bool isKeyLine(const std::string& raw) {
    const std::string line = trim(raw);
    return !line.empty() && line[0] != ';' && line[0] != '#' && line[0] != '[' && line.find('=') != std::string::npos;
}

} // namespace

void ModsModel::setSnapshot(std::string folder, std::vector<ModInfo> mods, std::vector<ModConflict> conflicts) {
    folder_ = std::move(folder);
    mods_ = std::move(mods);
    boot_ = mods_;
    conflicts_ = std::move(conflicts);
}

int ModsModel::indexOf(const std::string& key) const {
    const std::string k = lower(key);
    for (size_t i = 0; i < mods_.size(); ++i)
        if (lower(mods_[i].key) == k)
            return static_cast<int>(i);
    return -1;
}

const ModInfo* ModsModel::find(const std::string& key) const {
    const int i = indexOf(key);
    return i < 0 ? nullptr : &mods_[static_cast<size_t>(i)];
}

bool ModsModel::setEnabled(const std::string& key, bool on) {
    const int i = indexOf(key);
    if (i < 0)
        return false;
    mods_[static_cast<size_t>(i)].enabled = on;
    mods_[static_cast<size_t>(i)].decided = true;
    return true;
}

std::vector<const ModInfo*> ModsModel::undecided() const {
    std::vector<const ModInfo*> out;
    for (const ModInfo& m : mods_)
        if (m.isNew && !m.decided && m.found && !m.enabled)
            out.push_back(&m);
    return out;
}

size_t ModsModel::enableUndecided() {
    size_t n = 0;
    for (ModInfo& m : mods_)
        if (m.isNew && !m.decided && m.found && !m.enabled) {
            m.enabled = m.decided = true;
            ++n;
        }
    return n;
}

size_t ModsModel::keepUndecidedOff() {
    size_t n = 0;
    for (ModInfo& m : mods_)
        if (m.isNew && !m.decided && m.found && !m.enabled) {
            m.decided = true;
            ++n;
        }
    return n;
}

bool ModsModel::moveTo(const std::string& key, int index) {
    const int i = indexOf(key);
    if (i < 0)
        return false;
    index = std::clamp(index, 0, static_cast<int>(mods_.size()) - 1);
    if (index == i)
        return true;
    ModInfo m = std::move(mods_[static_cast<size_t>(i)]);
    m.decided = true; // moving a pack is looking at it: no longer "new" in mods.ini
    mods_.erase(mods_.begin() + i);
    mods_.insert(mods_.begin() + index, std::move(m));
    return true;
}

bool ModsModel::moveBy(const std::string& key, int delta) {
    const int i = indexOf(key);
    return i >= 0 && moveTo(key, i + delta);
}

bool ModsModel::restartRequired() const {
    // What the game loaded: the enabled packs in order. Disabled packs and their position do not matter.
    std::vector<std::string> now, then;
    for (const ModInfo& m : mods_)
        if (m.enabled)
            now.push_back(lower(m.key));
    for (const ModInfo& m : boot_)
        if (m.bootEnabled)
            then.push_back(lower(m.key));
    return now != then;
}

std::string ModsModel::stateText(const ModInfo& m) const {
    if (!m.found && m.enabled)
        return "Skipped: not found in the mods folder";
    if (m.enabled != m.bootEnabled)
        return m.enabled ? "Enabled, loads after restart" : "Disabled, unloads after restart";
    if (!m.enabled)
        return m.isNew && !m.decided ? "New: not enabled yet" : "Disabled";
    if (m.boot == ModBoot::Skipped)
        return "Skipped: " + (m.message.empty() ? std::string("not loaded") : m.message);
    return "Active";
}

std::vector<std::string> ModsModel::conflictLines(const std::string& key) const {
    const std::string k = lower(key);
    std::map<std::string, std::vector<std::string>> over, under; // other pack key -> entries
    for (const ModConflict& c : conflicts_) {
        if (lower(c.winnerKey) == k)
            over[c.loserKey].push_back(c.what);
        else if (lower(c.loserKey) == k)
            under[c.winnerKey].push_back(c.what);
    }
    auto line = [&](const char* verb, const std::string& other, const std::vector<std::string>& what) {
        const ModInfo* m = find(other);
        std::ostringstream o;
        o << verb << " '" << (m && !m->name.empty() ? m->name : other) << "' on " << what.size()
          << (what.size() == 1 ? " entry (" : " entries (");
        for (size_t i = 0; i < what.size() && i < 2; ++i)
            o << (i ? ", " : "") << what[i];
        if (what.size() > 2)
            o << ", ...";
        o << ")";
        return o.str();
    };
    std::vector<std::string> out;
    for (const auto& [other, what] : over)
        out.push_back(line("Overrides", other, what));
    for (const auto& [other, what] : under)
        out.push_back(line("Overridden by", other, what));
    return out;
}

std::string ModsModel::iniText(const std::string& existing) const {
    std::istringstream in(existing);
    bool hasSection = false;
    std::string line;
    std::vector<std::string> keep;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (isKeyLine(line))
            continue; // rewritten below
        if (lower(trim(line)) == "[mods]")
            hasSection = true;
        keep.push_back(line);
    }
    if (keep.empty()) {
        keep.push_back("; Drakan mods. One pack per line (folder name or zip name without .zip). Top to bottom is");
        keep.push_back("; the load order; later packs win conflicts. Set a pack to on to enable it, off to disable.");
    }
    while (!keep.empty() && trim(keep.back()).empty())
        keep.pop_back();
    std::ostringstream out;
    for (const std::string& l : keep)
        out << l << "\n";
    if (!hasSection)
        out << "[mods]\n";
    for (const ModInfo& m : mods_)
        out << m.key << " = " << (m.enabled ? "on" : m.isNew && !m.decided ? "new" : "off") << "\n";
    return out.str();
}

bool ModsModel::save(std::string* err) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (folder_.empty()) {
        if (err)
            *err = "no mods folder";
        return false;
    }
    fs::create_directories(folder_, ec);
    const fs::path path = fs::path(folder_) / "mods.ini";
    std::string existing;
    {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            existing = ss.str();
        }
    }
    const std::string text = iniText(existing);
    const fs::path tmp = fs::path(folder_) / "mods.ini.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
        if (!f) {
            if (err)
                *err = "cannot write " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) {
            if (err)
                *err = "cannot replace " + path.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

bool ModsModel::apply(const std::string& rowKey, const std::string& value, std::string* err) {
    const std::string en = kKeyModsEnable, ord = kKeyModsOrder;
    bool changed = false;
    if (rowKey.rfind(en, 0) == 0) {
        changed = setEnabled(rowKey.substr(en.size()), value == "1" || lower(value) == "on");
    } else if (rowKey.rfind(ord, 0) == 0) {
        char* e = nullptr;
        const long pos = std::strtol(value.c_str(), &e, 10);
        changed = e != value.c_str() && moveTo(rowKey.substr(ord.size()), static_cast<int>(pos) - 1);
    } else if (rowKey == kKeyModsFolder) {
        if (!openFolder() && err)
            *err = "Could not open " + folder_;
        return openFolder_ != nullptr;
    } else {
        if (err)
            *err = "Unknown mods setting: " + rowKey;
        return false;
    }
    if (!changed) {
        if (err)
            *err = "Unknown mod in " + rowKey;
        return false;
    }
    return save(err);
}

} // namespace dsettings
