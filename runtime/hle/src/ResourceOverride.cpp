#include "hle/ResourceOverride.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace hle {
namespace fs = std::filesystem;

static std::string upperStr(std::string s) {
    for (auto& c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}
static std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// ---- DirectoryOverride
// --------------------------------------------------------------------------------------------------------
bool DirectoryOverride::Key::operator<(const Key& o) const {
    if (file != o.file)
        return file < o.file;
    if (type != o.type)
        return type < o.type;
    if (id != o.id)
        return id < o.id;
    return sub < o.sub;
}

size_t DirectoryOverride::load(const std::string& root) {
    files_.clear();
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec))
        return 0;
    for (auto& d : fs::directory_iterator(root, ec)) {
        if (!d.is_directory(ec))
            continue;
        std::string file = upperStr(d.path().filename().string());
        for (auto& f : fs::directory_iterator(d.path(), ec)) {
            if (!f.is_regular_file(ec))
                continue;
            std::string n = f.path().filename().string();
            if (upperStr(f.path().extension().string()) != ".BIN")
                continue;
            n = n.substr(0, n.size() - 4);
            size_t a = n.find('_'), b = a == std::string::npos ? a : n.find('_', a + 1);
            if (b == std::string::npos)
                continue;
            try {
                Key k;
                k.file = file;
                k.type = (u16)std::stoul(n.substr(0, a), nullptr, 16);
                k.id = (u16)std::stoul(n.substr(a + 1, b - a - 1), nullptr, 10);
                std::string s = n.substr(b + 1);
                k.sub = (s == "x" || s == "X") ? 0xFFFF : (u16)std::stoul(s, nullptr, 10);
                files_[k] = f.path().string();
            } catch (...) {
                logf("override: ignoring file name %s", f.path().string().c_str());
            }
        }
    }
    return files_.size();
}

const std::string* DirectoryOverride::find(const ResourceId& id) const {
    if (files_.empty())
        return nullptr;
    auto it = files_.find(Key{id.file, id.type, id.id, id.sub});
    if (it == files_.end())
        it = files_.find(Key{id.file, id.type, id.id, 0xFFFF});
    return it == files_.end() ? nullptr : &it->second;
}

bool DirectoryOverride::wants(const ResourceId& id) {
    return find(id) != nullptr;
}

bool DirectoryOverride::apply(const ResourceId& id, std::vector<u8>& data) {
    const std::string* p = find(id);
    if (!p)
        return false;
    std::ifstream in(*p, std::ios::binary);
    if (!in) {
        logf("override: cannot read %s", p->c_str());
        return false;
    }
    std::vector<u8> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (verbose())
        logf("override: %s %04x/%u/%u replaced by %s (%zu -> %zu bytes)", id.file.c_str(), id.type, id.id, id.sub,
             p->c_str(), data.size(), b.size());
    data = std::move(b);
    return true;
}

// ---- XDB codec
// ---------------------------------------------------------------------------------------------------------------
namespace xdb {
bool decode(const std::vector<u8>& d, std::vector<std::string>& slots) {
    slots.clear();
    size_t p = 0;
    for (int i = 0; i < NUM_SLOTS; ++i) {
        if (p + 2 > d.size())
            return false;
        u16 n = (u16)(d[p] | (d[p + 1] << 8));
        if (p + 2 + n > d.size() || ((n + 2) & 3) != 0)
            return false;
        const u8* s = &d[p + 2];
        size_t len = 0;
        while (len < n && s[len])
            ++len;
        slots.emplace_back((const char*)s, len);
        p += 2 + n;
    }
    return p == d.size();
}
std::vector<u8> encode(const std::vector<std::string>& slots) {
    std::vector<u8> out;
    for (int i = 0; i < NUM_SLOTS; ++i) {
        std::string s = i < (int)slots.size() ? slots[i] : std::string();
        size_t n = ((s.size() + 2 + 3) & ~(size_t)3) - 2;
        out.push_back((u8)(n & 0xff));
        out.push_back((u8)(n >> 8));
        out.insert(out.end(), s.begin(), s.end());
        out.insert(out.end(), n - s.size(), 0);
    }
    return out;
}
int langFromName(const std::string& s) {
    std::string u = upperStr(s);
    if (u.empty() || u == "*")
        return -1;
    static const char* n[] = {"EN", "DE", "FR", "IT", "ES"};
    for (int i = 0; i < NUM_LANGS; ++i)
        if (u == n[i])
            return i;
    return -2;
}
} // namespace xdb

// ---- XdbTextOverride
// ----------------------------------------------------------------------------------------------------------
static bool isXdbText(const ResourceId& id) {
    return id.type == 0x400 && id.file.size() > 4 && id.file.compare(id.file.size() - 4, 4, ".XDB") == 0;
}

void XdbTextOverride::setText(const std::string& file, u16 id, int lang, std::string text) {
    patches_.push_back({upperStr(file), id, lang, std::move(text)});
}

static std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            o += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
        } else
            o += s[i];
    }
    return o;
}

size_t XdbTextOverride::loadDirectory(const std::string& root) {
    size_t n = 0;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec))
        return 0;
    for (auto& d : fs::directory_iterator(root, ec)) {
        if (!d.is_directory(ec))
            continue;
        std::string file = upperStr(d.path().filename().string());
        std::ifstream in(d.path() / "text.ini", std::ios::binary);
        if (!in)
            continue;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::string t = trimStr(line);
            if (t.empty() || t[0] == ';' || t[0] == '#')
                continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trimStr(line.substr(0, eq));
            std::string val = line.substr(eq + 1);
            if (!val.empty() && val[0] == ' ')
                val.erase(0, 1); // one separating blank
            size_t dot = key.find('.');
            try {
                u16 id = (u16)std::stoul(dot == std::string::npos ? key : key.substr(0, dot));
                int lang = dot == std::string::npos ? -1 : xdb::langFromName(key.substr(dot + 1));
                if (lang == -2)
                    continue;
                setText(file, id, lang, unescape(val));
                ++n;
            } catch (...) {
            }
        }
    }
    return n;
}

bool XdbTextOverride::wants(const ResourceId& id) {
    if (!isXdbText(id))
        return false;
    if (!rules_.empty())
        return true;
    for (auto& p : patches_)
        if (p.id == id.id && p.file == id.file)
            return true;
    return false;
}

bool XdbTextOverride::apply(const ResourceId& id, std::vector<u8>& data) {
    std::vector<std::string> slots;
    if (!xdb::decode(data, slots))
        return false; // not a text entry (different layout): leave untouched
    bool changed = false;
    for (int l = 0; l < xdb::NUM_LANGS; ++l) {
        std::string t = slots[l];
        for (auto& r : rules_)
            r(id, l, t);
        for (auto& p : patches_)
            if (p.id == id.id && p.file == id.file && (p.lang == -1 || p.lang == l))
                t = p.text;
        if (t != slots[l]) {
            slots[l] = t;
            changed = true;
        }
    }
    if (changed)
        data = xdb::encode(slots);
    return changed;
}

} // namespace hle
