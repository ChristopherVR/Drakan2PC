#include "hle/IsoFs.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace hle {

// ---------------------------------------------------------------- logging
static LogSink g_sink;
static bool g_verbose = false;
void setLogSink(LogSink s) {
    g_sink = std::move(s);
}
void setVerbose(bool v) {
    g_verbose = v;
}
bool verbose() {
    return g_verbose;
}
void logf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_sink)
        g_sink(buf);
    else
        fprintf(stderr, "[rsrciop] %s\n", buf);
}

// ---------------------------------------------------------------- mangling
// Mirrors analysis/scripts/iop_mangle.py exactly:
//   keep [0-9A-Za-z_.] upper-cased; k = rfind('.');
//   k > 0: if k >= 9 -> out[:6] + out[k-2:]   else (no dot, or dot at 0): if len >= 9 -> out[:6] + out[-2:]
std::string mangleComponent(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.')
            out += (char)c;
        else if (c >= 'a' && c <= 'z')
            out += (char)(c - 32);
    }
    size_t kp = out.rfind('.');
    long k = kp == std::string::npos ? -1 : (long)kp;
    if (k > 0) {
        if (k >= 9)
            out = out.substr(0, 6) + out.substr((size_t)k - 2);
    } else {
        if (out.size() >= 9)
            out = out.substr(0, 6) + out.substr(out.size() - 2);
    }
    return out;
}

static std::vector<std::string> splitPath(const std::string& p) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : p) {
        if (c == '\\' || c == '/') {
            if (!cur.empty())
                v.push_back(cur);
            cur.clear();
        } else
            cur += c;
    }
    if (!cur.empty())
        v.push_back(cur);
    return v;
}

std::string manglePath(const std::string& p) {
    std::string r;
    for (auto& c : splitPath(p)) {
        if (!r.empty())
            r += '\\';
        r += mangleComponent(c);
    }
    return r;
}

static std::string upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    return s;
}

static const char* kLangs[] = {"ENGLISH", "GERMAN", "FRENCH", "ITALIAN", "SPANISH"};

// ---------------------------------------------------------------- IsoFs
IsoFs::~IsoFs() = default;

bool IsoFs::readRaw(u64 off, void* dst, size_t n) const {
    std::lock_guard<std::mutex> lk(mu_);
    return source_ && source_->readAt(off, dst, n);
}

static u32 le32(const u8* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

void IsoFs::walk(u32 lba, u32 size, const std::string& prefix, int depth) {
    if (depth > 32 || size == 0 || size > (64u << 20))
        return;
    std::vector<u8> d(size);
    if (!readRaw((u64)lba * 2048, d.data(), size))
        return;
    size_t pos = 0;
    while (pos < size) {
        u8 len = d[pos];
        if (len == 0) {
            pos = (pos / 2048 + 1) * 2048;
            continue;
        }
        if (pos + len > size || len < 34)
            break;
        const u8* r = &d[pos];
        u8 nl = r[32];
        if (33u + nl > len)
            break;
        bool isDir = (r[25] & 2) != 0;
        if (!(nl == 1 && (r[33] == 0 || r[33] == 1))) {
            std::string name((const char*)r + 33, nl);
            size_t semi = name.find(';');
            if (semi != std::string::npos)
                name.resize(semi);
            if (!name.empty() && name.back() == '.')
                name.pop_back();
            name = upper(name);
            Node n;
            n.dir = isDir;
            n.lba = le32(r + 2);
            n.size = le32(r + 10);
            n.key = prefix.empty() ? name : prefix + "\\" + name;
            map_[n.key] = n;
            if (isDir)
                walk(n.lba, (u32)n.size, n.key, depth + 1);
        }
        pos += len;
    }
}

bool IsoFs::openIso(const std::string& path, std::string* err) {
    return openSource(openIsoSource(path, err), err);
}

bool IsoFs::openSource(std::shared_ptr<const IsoSource> source, std::string* err) {
    source_ = std::move(source);
    map_.clear();
    if (!source_)
        return false;
    u8 pvd[2048];
    if (!readRaw(16ull * 2048, pvd, sizeof pvd) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        if (err)
            *err = "no ISO9660 primary volume descriptor";
        source_.reset();
        return false;
    }
    const u8* root = pvd + 156;
    walk(le32(root + 2), le32(root + 10), "", 0);
    return true;
}

bool IsoFs::openFolder(const std::string& rootDir, std::string* err) {
    namespace fs = std::filesystem;
    source_.reset();
    map_.clear();
    std::error_code ec;
    fs::path root(rootDir);
    if (!fs::is_directory(root, ec)) {
        if (err)
            *err = "not a directory: " + rootDir;
        return false;
    }
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::string rel = fs::relative(it->path(), root, ec).string();
        std::string key;
        for (char c : rel)
            key += (c == '/' ? '\\' : c);
        key = upper(key);
        // strip ";1" if the folder was produced with version suffixes
        size_t semi = key.find(';');
        if (semi != std::string::npos)
            key.resize(semi);
        Node n;
        n.key = key;
        n.dir = it->is_directory(ec);
        n.size = n.dir ? 0 : (u64)it->file_size(ec);
        n.host = it->path().string();
        map_[key] = n;
    }
    return true;
}

void IsoFs::setLanguageRoot(const std::string& root) {
    // RSRCIOP gets "\\<Language>"; mangle it like every other component.
    lang_ = manglePath(root);
}

std::string IsoFs::resolveKey(const std::string& in) const {
    std::string s = in;
    // strip device prefix
    std::string lo = s;
    for (auto& c : lo)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    if (lo.rfind("cdrom0:", 0) == 0)
        s = s.substr(7);
    else if (lo.rfind("cdrom:", 0) == 0)
        s = s.substr(6);
    // strip ";N" version
    size_t semi = s.rfind(';');
    if (semi != std::string::npos)
        s.resize(semi);
    // normalise . and .. before mangling (the IOP does not, but DB dependency paths use "..")
    std::vector<std::string> comps;
    for (auto& c : splitPath(s)) {
        if (c == ".")
            continue;
        if (c == "..") {
            if (!comps.empty())
                comps.pop_back();
            continue;
        }
        comps.push_back(mangleComponent(c));
    }
    std::string out;
    bool hasLang = !comps.empty() &&
                   std::any_of(std::begin(kLangs), std::end(kLangs), [&](const char* l) { return comps[0] == l; });
    if (!hasLang && !lang_.empty())
        out = lang_;
    for (auto& c : comps) {
        if (!out.empty())
            out += '\\';
        out += c;
    }
    return out;
}

bool IsoFs::findKey(const std::string& key, Node& out) const {
    auto it = map_.find(key);
    if (it == map_.end())
        return false;
    out = it->second;
    return true;
}

bool IsoFs::lookup(const std::string& gamePath, Node& out) const {
    std::string key = resolveKey(gamePath);
    if (findKey(key, out))
        return true;
    if (!lang_.empty() && key.rfind(lang_ + "\\", 0) == 0 && findKey(key.substr(lang_.size() + 1), out))
        return true;
    return false;
}

size_t IsoFs::read(const Node& n, u64 off, void* dst, size_t len) const {
    if (n.dir || off >= n.size)
        return 0;
    if (len > n.size - off)
        len = (size_t)(n.size - off);
    if (!len)
        return 0;
    if (source_)
        return readRaw((u64)n.lba * 2048 + off, dst, len) ? len : 0;
    FILE* f = fopen(n.host.c_str(), "rb");
    if (!f)
        return 0;
#ifdef _WIN32
    _fseeki64(f, (long long)off, SEEK_SET);
#else
    fseeko(f, (off_t)off, SEEK_SET);
#endif
    size_t got = fread(dst, 1, len, f);
    fclose(f);
    return got;
}

std::vector<u8> IsoFs::readAll(const Node& n) const {
    std::vector<u8> v((size_t)n.size);
    v.resize(read(n, 0, v.data(), v.size()));
    return v;
}

std::vector<std::string> IsoFs::listDir(const std::string& dirKey) const {
    std::vector<std::string> v;
    std::string pfx = dirKey.empty() ? "" : dirKey + "\\";
    for (auto& [k, n] : map_) {
        if (k.size() <= pfx.size() || k.compare(0, pfx.size(), pfx) != 0)
            continue;
        if (k.find('\\', pfx.size()) != std::string::npos)
            continue;
        v.push_back(k);
    }
    return v;
}

} // namespace hle
