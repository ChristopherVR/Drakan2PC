#include "hle/ModPack.h"
#include "hle/IsoFs.h"
#include "hle/Srsc.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <tuple>

namespace hle::mods {
namespace fs = std::filesystem;

// ---- small helpers ----------------------------------------------------------------------------------------------
static std::string lower(std::string s) {
    for (auto& c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}
static std::string upper(std::string s) {
    for (auto& c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
static bool readWhole(const fs::path& p, std::vector<u8>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
static bool parseHex64(std::string s, u64& v) {
    s = lower(trim(s));
    if (s.rfind("0x", 0) == 0)
        s = s.substr(2);
    if (s.empty() || s.size() > 16)
        return false;
    v = 0;
    for (char c : s) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0)
            return false;
        v = (v << 4) | (u64)d;
    }
    return true;
}

u64 profileHash(const std::string& serial, const std::string& saveId) {
    std::string s = serial + "|" + saveId;
    u64 h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h ? h : 1ull;
}
std::string hex64(u64 v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4)
        s[(size_t)i] = d[v & 15];
    return s;
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<long> v;
        size_t p = 0;
        while (p <= s.size()) {
            size_t e = s.find('.', p);
            std::string t = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
            v.push_back(std::strtol(t.c_str(), nullptr, 10));
            if (e == std::string::npos)
                break;
            p = e + 1;
        }
        return v;
    };
    auto x = parts(a), y = parts(b);
    size_t n = std::max(x.size(), y.size());
    x.resize(n, 0);
    y.resize(n, 0);
    return x < y ? -1 : x > y ? 1 : 0;
}

// ---- manifest: TOML subset and JSON, both flattened to dotted keys -> list of scalar strings ----------------------
using Flat = std::map<std::string, std::vector<std::string>>;

static bool tomlString(const std::string& s, size_t& p, std::string& out, std::string& err) {
    char q = s[p++];
    out.clear();
    while (p < s.size() && s[p] != q) {
        if (q == '"' && s[p] == '\\' && p + 1 < s.size()) {
            char c = s[++p];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
            ++p;
        } else
            out += s[p++];
    }
    if (p >= s.size()) {
        err = "unterminated string";
        return false;
    }
    ++p;
    return true;
}

// Parses a TOML value text (scalar or array of scalars) into `out`.
static bool tomlValue(const std::string& v, std::vector<std::string>& out, std::string& err) {
    size_t p = 0;
    auto skip = [&] {
        while (p < v.size() && (v[p] == ' ' || v[p] == '\t' || v[p] == '\r' || v[p] == '\n' || v[p] == ','))
            ++p;
    };
    skip();
    if (p >= v.size()) {
        err = "missing value";
        return false;
    }
    bool arr = v[p] == '[';
    if (arr)
        ++p;
    for (;;) {
        skip();
        if (p >= v.size()) {
            if (arr) {
                err = "unterminated array";
                return false;
            }
            return true;
        }
        if (arr && v[p] == ']')
            return true;
        std::string item;
        if (v[p] == '"' || v[p] == '\'') {
            if (!tomlString(v, p, item, err))
                return false;
        } else {
            size_t e = p;
            while (e < v.size() && v[e] != ',' && v[e] != ']' && v[e] != ' ' && v[e] != '\t' && v[e] != '\r' &&
                   v[e] != '\n')
                ++e;
            item = v.substr(p, e - p);
            p = e;
        }
        out.push_back(item);
        if (!arr)
            return true;
    }
}

// Strips a '#' comment that is outside a string.
static std::string tomlStrip(const std::string& l) {
    char q = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        if (q) {
            if (l[i] == '\\' && q == '"')
                ++i;
            else if (l[i] == q)
                q = 0;
        } else if (l[i] == '"' || l[i] == '\'')
            q = l[i];
        else if (l[i] == '#')
            return l.substr(0, i);
    }
    return l;
}
static int bracketDepth(const std::string& l) {
    int d = 0;
    char q = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        if (q) {
            if (l[i] == '\\' && q == '"')
                ++i;
            else if (l[i] == q)
                q = 0;
        } else if (l[i] == '"' || l[i] == '\'')
            q = l[i];
        else if (l[i] == '[')
            ++d;
        else if (l[i] == ']')
            --d;
    }
    return d;
}

static bool parseToml(const std::string& text, Flat& out, std::string& err) {
    std::string table;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        std::string line = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? text.size() : e + 1;
        ++lineNo;
        line = trim(tomlStrip(line));
        if (line.empty())
            continue;
        if (line[0] == '[') {
            if (line.size() > 1 && line[1] == '[')
                continue; // array of tables: not part of the schema
            size_t close = line.find(']');
            if (close == std::string::npos) {
                err = "line " + std::to_string(lineNo) + ": bad table header";
                return false;
            }
            table = trim(line.substr(1, close - 1));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            err = "line " + std::to_string(lineNo) + ": expected key = value";
            return false;
        }
        std::string key = trim(line.substr(0, eq));
        if (key.size() >= 2 && (key[0] == '"' || key[0] == '\'') && key.back() == key[0])
            key = key.substr(1, key.size() - 2);
        std::string val = line.substr(eq + 1);
        // multi-line arrays
        while (bracketDepth(val) > 0 && pos < text.size()) {
            size_t e2 = text.find('\n', pos);
            std::string more = text.substr(pos, e2 == std::string::npos ? std::string::npos : e2 - pos);
            pos = e2 == std::string::npos ? text.size() : e2 + 1;
            ++lineNo;
            val += "\n" + tomlStrip(more);
        }
        std::vector<std::string> items;
        std::string verr;
        if (!tomlValue(val, items, verr)) {
            err = "line " + std::to_string(lineNo) + ": " + verr;
            return false;
        }
        out[table.empty() ? key : table + "." + key] = items;
    }
    return true;
}

struct JsonParser {
    const std::string& s;
    size_t p = 0;
    std::string err;
    Flat& out;
    void ws() {
        while (p < s.size() && isspace((unsigned char)s[p]))
            ++p;
    }
    bool fail(const char* m) {
        if (err.empty())
            err = std::string(m) + " at offset " + std::to_string(p);
        return false;
    }
    bool str(std::string& o) {
        if (p >= s.size() || s[p] != '"')
            return fail("expected string");
        ++p;
        o.clear();
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                char c = s[++p];
                if (c == 'u' && p + 4 < s.size()) {
                    unsigned cp = (unsigned)std::strtoul(s.substr(p + 1, 4).c_str(), nullptr, 16);
                    p += 4;
                    if (cp < 0x80)
                        o += (char)cp;
                    else if (cp < 0x800) {
                        o += (char)(0xC0 | (cp >> 6));
                        o += (char)(0x80 | (cp & 63));
                    } else {
                        o += (char)(0xE0 | (cp >> 12));
                        o += (char)(0x80 | ((cp >> 6) & 63));
                        o += (char)(0x80 | (cp & 63));
                    }
                } else
                    o += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;
                ++p;
            } else
                o += s[p++];
        }
        if (p >= s.size())
            return fail("unterminated string");
        ++p;
        return true;
    }
    // sink != null: inside an array, scalars go to the sink and containers are skipped (parsed with an empty prefix
    // into a scratch map). sink == null: object member / top level.
    bool value(const std::string& prefix, std::vector<std::string>* sink) {
        ws();
        if (p >= s.size())
            return fail("unexpected end");
        char c = s[p];
        if (c == '{') {
            if (sink) { // object inside an array: parse and discard
                Flat scratch;
                JsonParser inner{s, p, {}, scratch};
                if (!inner.value(std::string(), nullptr))
                    return fail(inner.err.c_str());
                p = inner.p;
                return true;
            }
            ++p;
            ws();
            if (p < s.size() && s[p] == '}') {
                ++p;
                return true;
            }
            for (;;) {
                ws();
                std::string k;
                if (!str(k))
                    return false;
                ws();
                if (p >= s.size() || s[p] != ':')
                    return fail("expected ':'");
                ++p;
                if (!value(prefix.empty() ? k : prefix + "." + k, nullptr))
                    return false;
                ws();
                if (p < s.size() && s[p] == ',') {
                    ++p;
                    continue;
                }
                if (p < s.size() && s[p] == '}') {
                    ++p;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++p;
            std::vector<std::string> items;
            ws();
            if (p < s.size() && s[p] == ']') {
                ++p;
                if (!sink)
                    out[prefix] = items;
                return true;
            }
            for (;;) {
                if (!value(prefix, &items))
                    return false;
                ws();
                if (p < s.size() && s[p] == ',') {
                    ++p;
                    continue;
                }
                if (p < s.size() && s[p] == ']') {
                    ++p;
                    break;
                }
                return fail("expected ',' or ']'");
            }
            if (!sink)
                out[prefix] = items;
            return true;
        }
        std::string v;
        if (c == '"') {
            if (!str(v))
                return false;
        } else {
            size_t e = p;
            while (e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != ']' && !isspace((unsigned char)s[e]))
                ++e;
            v = s.substr(p, e - p);
            p = e;
            if (v.empty())
                return fail("bad value");
        }
        if (sink)
            sink->push_back(v);
        else
            out[prefix] = {v};
        return true;
    }
};

static bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64)
        return false;
    for (char c : id)
        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-'))
            return false;
    return true;
}

bool parseManifest(const std::string& textIn, bool json, Manifest& m, std::string& err) {
    Flat f;
    std::string text = textIn;
    if (text.size() >= 3 && (u8)text[0] == 0xEF && (u8)text[1] == 0xBB && (u8)text[2] == 0xBF)
        text.erase(0, 3); // BOM
    if (json) {
        JsonParser jp{text, 0, {}, f};
        jp.ws();
        if (jp.p >= text.size() || text[jp.p] != '{') {
            err = "mod.json: top level must be an object";
            return false;
        }
        if (!jp.value(std::string(), nullptr)) {
            err = "mod.json: " + jp.err;
            return false;
        }
    } else if (!parseToml(text, f, err)) {
        err = "mod.toml: " + err;
        return false;
    }
    auto one = [&](const char* k) -> std::string {
        auto it = f.find(k);
        return it == f.end() || it->second.empty() ? std::string() : it->second[0];
    };
    auto many = [&](const char* k) {
        auto it = f.find(k);
        return it == f.end() ? std::vector<std::string>() : it->second;
    };
    m = Manifest();
    std::string fmt = one("format");
    m.format = fmt.empty() ? 1 : std::atoi(fmt.c_str());
    m.id = one("id");
    m.name = one("name");
    m.version = one("version");
    m.author = one("author");
    m.description = one("description");
    m.profiles = many("target.profile");
    m.profileHashes = many("target.hash");
    m.loadAfter = many("load.after");
    m.loadBefore = many("load.before");
    m.dependencies = many("dependencies");
    m.levels = many("levels");
    m.campaign = one("campaign");
    m.campaignName = one("campaign_name");
    m.campaignStart = one("campaign_start");
    m.levelCopies = many("level_copies");
    for (auto& d : many("load.dependencies")) // tolerated: written after a [load] header by mistake
        m.dependencies.push_back(d);
    if (!validId(m.id)) {
        err = "'id' is missing or invalid (use 1-64 of letters, digits, '.', '_', '-')";
        return false;
    }
    if (m.name.empty())
        m.name = m.id;
    if (m.version.empty())
        m.version = "0";
    return true;
}

// ---- archives -----------------------------------------------------------------------------------------------------
static bool safeRelPath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find(':') != std::string::npos)
        return false;
    size_t s = 0;
    while (s <= p.size()) {
        size_t e = p.find('/', s);
        std::string seg = p.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (seg == "..")
            return false;
        if (e == std::string::npos)
            break;
        s = e + 1;
    }
    return true;
}

static u32 rd16(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}
static u32 rd32(const u8* p) {
    return rd16(p) | (rd16(p + 2) << 16);
}

namespace {
class FolderArchive final : public ModArchive {
public:
    explicit FolderArchive(fs::path root) : root_(std::move(root)) {}
    std::vector<Item> list() const override {
        std::vector<Item> v;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec))
                continue;
            Item i;
            i.path = fs::relative(it->path(), root_, ec).generic_string();
            i.size = (u64)it->file_size(ec);
            v.push_back(i);
        }
        return v;
    }
    bool read(const std::string& path, std::vector<u8>& out) const override {
        return safeRelPath(path) && readWhole(root_ / path, out);
    }
    std::string describe() const override { return "folder " + root_.string(); }

private:
    fs::path root_;
};

u32 crc32(const u8* d, size_t n) {
    static u32 table[256];
    static std::once_flag once;
    std::call_once(once, [] {
        for (u32 i = 0; i < 256; ++i) {
            u32 c = i;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    });
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
        c = table[(c ^ d[i]) & 255] ^ (c >> 8);
    return ~c;
}

class ZipArchive final : public ModArchive {
public:
    struct Ent {
        std::string path;
        u32 method, crc, csize, usize, lho;
    };
    bool open(const std::string& path, std::string& err) {
        path_ = path;
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            err = "cannot open " + path;
            return false;
        }
        std::streamoff size = in.tellg();
        if (size < 22) {
            err = "not a zip (too small)";
            return false;
        }
        size_t tail = (size_t)std::min<std::streamoff>(size, 22 + 65535);
        std::vector<u8> t(tail);
        in.seekg(size - (std::streamoff)tail);
        in.read((char*)t.data(), (std::streamsize)tail);
        size_t eocd = std::string::npos;
        for (size_t i = tail - 22 + 1; i-- > 0;)
            if (rd32(&t[i]) == 0x06054b50u) {
                eocd = i;
                break;
            }
        if (eocd == std::string::npos) {
            err = "not a zip (no end-of-central-directory record)";
            return false;
        }
        u32 count = rd16(&t[eocd + 10]), cdSize = rd32(&t[eocd + 12]), cdOff = rd32(&t[eocd + 16]);
        if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) {
            err = "zip64 archives are not supported";
            return false;
        }
        if ((u64)cdOff + cdSize > (u64)size) {
            err = "corrupt zip (central directory out of range)";
            return false;
        }
        std::vector<u8> cd(cdSize);
        in.seekg(cdOff);
        in.read((char*)cd.data(), cdSize);
        size_t p = 0;
        for (u32 i = 0; i < count; ++i) {
            if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50u) {
                err = "corrupt zip (bad central directory entry)";
                return false;
            }
            u32 flags = rd16(&cd[p + 8]), nlen = rd16(&cd[p + 28]), elen = rd16(&cd[p + 30]), clen = rd16(&cd[p + 32]);
            if (p + 46 + nlen + elen + clen > cd.size()) {
                err = "corrupt zip (entry overruns directory)";
                return false;
            }
            Ent e;
            e.method = rd16(&cd[p + 10]);
            e.crc = rd32(&cd[p + 16]);
            e.csize = rd32(&cd[p + 20]);
            e.usize = rd32(&cd[p + 24]);
            e.lho = rd32(&cd[p + 42]);
            e.path.assign((const char*)&cd[p + 46], nlen);
            std::replace(e.path.begin(), e.path.end(), '\\', '/');
            p += 46 + nlen + elen + clen;
            if (e.path.empty() || e.path.back() == '/')
                continue; // directory
            if (flags & 1) {
                err = "encrypted zip entries are not supported (" + e.path + ")";
                return false;
            }
            if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu) {
                err = "zip64 entries are not supported (" + e.path + ")";
                return false;
            }
            if (!safeRelPath(e.path))
                continue; // never reachable through read(): dropped (zip-slip)
            ents_.push_back(e);
        }
        return true;
    }
    std::vector<Item> list() const override {
        std::vector<Item> v;
        for (auto& e : ents_)
            v.push_back({e.path, e.usize});
        return v;
    }
    bool read(const std::string& path, std::vector<u8>& out) const override {
        for (auto& e : ents_) {
            if (e.path != path)
                continue;
            std::lock_guard<std::mutex> lk(mu_);
            std::ifstream in(path_, std::ios::binary);
            if (!in)
                return false;
            u8 h[30];
            in.seekg(e.lho);
            in.read((char*)h, 30);
            if (!in || rd32(h) != 0x04034b50u)
                return false;
            in.seekg((std::streamoff)e.lho + 30 + rd16(h + 26) + rd16(h + 28));
            std::vector<u8> c(e.csize);
            in.read((char*)c.data(), e.csize);
            if (!in)
                return false;
            if (e.method == 0) {
                if (e.csize != e.usize)
                    return false;
                out = std::move(c);
            } else if (e.method == 8) {
                if (!inflateRaw(c.data(), c.size(), out, e.usize))
                    return false;
            } else
                return false;
            return out.size() == e.usize && crc32(out.data(), out.size()) == e.crc;
        }
        return false;
    }
    std::string describe() const override { return "zip " + path_; }

private:
    std::string path_;
    std::vector<Ent> ents_;
    mutable std::mutex mu_;
};
} // namespace

std::shared_ptr<ModArchive> openArchive(const std::string& path, std::string& err) {
    std::error_code ec;
    if (fs::is_directory(path, ec))
        return std::make_shared<FolderArchive>(path);
    if (fs::is_regular_file(path, ec)) {
        auto z = std::make_shared<ZipArchive>();
        if (!z->open(path, err))
            return nullptr;
        return z;
    }
    err = "no such folder or zip: " + path;
    return nullptr;
}

// ---- inflate (RFC 1951), after the structure of zlib's puff -------------------------------------------------------
namespace {
struct Inflater {
    const u8* in;
    size_t n, pos = 0;
    u32 buf = 0;
    int cnt = 0;
    std::vector<u8>& out;
    size_t limit;
    bool bad = false;
    struct Huff {
        u16 count[16];
        u16 symbol[288];
    };
    int bits(int need) {
        u32 v = buf;
        while (cnt < need) {
            if (pos >= n) {
                bad = true;
                return 0;
            }
            v |= (u32)in[pos++] << cnt;
            cnt += 8;
        }
        buf = v >> need;
        cnt -= need;
        return (int)(v & ((1u << need) - 1));
    }
    static int construct(Huff& h, const u16* len, int num) {
        for (int i = 0; i < 16; ++i)
            h.count[i] = 0;
        for (int i = 0; i < num; ++i)
            h.count[len[i]]++;
        if (h.count[0] == num)
            return 0;
        int left = 1;
        for (int i = 1; i < 16; ++i) {
            left <<= 1;
            left -= h.count[i];
            if (left < 0)
                return left;
        }
        u16 offs[16];
        offs[1] = 0;
        for (int i = 1; i < 15; ++i)
            offs[i + 1] = (u16)(offs[i] + h.count[i]);
        for (int i = 0; i < num; ++i)
            if (len[i])
                h.symbol[offs[len[i]]++] = (u16)i;
        return left;
    }
    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= bits(1);
            if (bad)
                return -1;
            int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
    bool codes(const Huff& lc, const Huff& dc) {
        static const u16 lens[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const u16 lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const u16 dists[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const u16 dext[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lc);
            if (sym < 0 || bad)
                return false;
            if (sym < 256) {
                if (out.size() >= limit)
                    return false;
                out.push_back((u8)sym);
            } else if (sym == 256)
                return true;
            else {
                sym -= 257;
                if (sym >= 29)
                    return false;
                int len = lens[sym] + bits(lext[sym]);
                int ds = decode(dc);
                if (ds < 0 || ds >= 30 || bad)
                    return false;
                size_t dist = (size_t)dists[ds] + (size_t)bits(dext[ds]);
                if (bad || dist > out.size() || out.size() + (size_t)len > limit)
                    return false;
                size_t from = out.size() - dist;
                for (int i = 0; i < len; ++i)
                    out.push_back(out[from + (size_t)i]);
            }
        }
    }
    bool run() {
        int last;
        do {
            last = bits(1);
            int type = bits(2);
            if (bad)
                return false;
            if (type == 0) {
                buf = 0;
                cnt = 0;
                if (pos + 4 > n)
                    return false;
                u32 len = rd16(in + pos), nlen = rd16(in + pos + 2);
                pos += 4;
                if (len != (~nlen & 0xFFFF) || pos + len > n || out.size() + len > limit)
                    return false;
                out.insert(out.end(), in + pos, in + pos + len);
                pos += len;
            } else if (type == 1) {
                Huff lc, dc;
                u16 l[288];
                int s = 0;
                for (; s < 144; ++s)
                    l[s] = 8;
                for (; s < 256; ++s)
                    l[s] = 9;
                for (; s < 280; ++s)
                    l[s] = 7;
                for (; s < 288; ++s)
                    l[s] = 8;
                construct(lc, l, 288);
                for (s = 0; s < 30; ++s)
                    l[s] = 5;
                construct(dc, l, 30);
                if (!codes(lc, dc))
                    return false;
            } else if (type == 2) {
                static const u16 order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
                if (bad || nlen > 286 || ndist > 30)
                    return false;
                u16 lengths[320] = {};
                for (int i = 0; i < ncode; ++i)
                    lengths[order[i]] = (u16)bits(3);
                Huff lencode;
                if (construct(lencode, lengths, 19) != 0)
                    return false;
                int idx = 0;
                while (idx < nlen + ndist) {
                    int sym = decode(lencode);
                    if (sym < 0 || bad)
                        return false;
                    if (sym < 16)
                        lengths[idx++] = (u16)sym;
                    else {
                        int len = 0, rep;
                        if (sym == 16) {
                            if (idx == 0)
                                return false;
                            len = lengths[idx - 1];
                            rep = 3 + bits(2);
                        } else if (sym == 17)
                            rep = 3 + bits(3);
                        else
                            rep = 11 + bits(7);
                        if (idx + rep > nlen + ndist)
                            return false;
                        while (rep--)
                            lengths[idx++] = (u16)len;
                    }
                }
                if (lengths[256] == 0)
                    return false;
                Huff lc, dc;
                int e1 = construct(lc, lengths, nlen);
                if (e1 < 0 || (e1 > 0 && nlen - lc.count[0] != 1))
                    return false;
                int e2 = construct(dc, lengths + nlen, ndist);
                if (e2 < 0 || (e2 > 0 && ndist - dc.count[0] != 1))
                    return false;
                if (!codes(lc, dc))
                    return false;
            } else
                return false;
        } while (!last);
        return true;
    }
};
} // namespace

bool inflateRaw(const u8* in, size_t inSize, std::vector<u8>& out, size_t expectedSize) {
    out.clear();
    out.reserve(expectedSize);
    Inflater z{in, inSize, 0, 0, 0, out, expectedSize, false};
    return z.run();
}

// ---- merged entry replacements ------------------------------------------------------------------------------------
class ModOverride final : public IResourceOverride {
public:
    struct Key {
        std::string file;
        u16 type, id, sub;
        bool operator<(const Key& o) const {
            return std::tie(file, type, id, sub) < std::tie(o.file, o.type, o.id, o.sub);
        }
    };
    struct Src {
        std::shared_ptr<ModArchive> archive;
        std::string path;
        size_t modIndex;
        std::string modId;
        u64 size;
    };
    std::map<Key, Src> map;
    ModLimits limits;

    const Src* find(const ResourceId& id) const {
        auto a = map.find(Key{id.file, id.type, id.id, id.sub});
        auto b = map.find(Key{id.file, id.type, id.id, 0xFFFF});
        if (a == map.end() && b == map.end())
            return nullptr;
        if (a == map.end())
            return &b->second;
        if (b == map.end())
            return &a->second;
        return a->second.modIndex >= b->second.modIndex ? &a->second : &b->second; // later mod wins
    }
    bool wants(const ResourceId& id) override { return find(id) != nullptr; }
    bool apply(const ResourceId& id, std::vector<u8>& data) override {
        const Src* s = find(id);
        if (!s)
            return false;
        std::vector<u8> b;
        if (!s->archive->read(s->path, b) || b.empty()) {
            logf("mods: %s: cannot read %s, original entry kept", s->modId.c_str(), s->path.c_str());
            return false;
        }
        const size_t orig = data.size();
        if (b.size() > limits.maxWholeEntry && orig <= limits.maxWholeEntry) {
            logf("mods: %s: %s %04x/%u/%u would grow from %zu to %zu bytes, past the %u byte whole-entry limit of the "
                 "resource loader; original entry kept",
                 s->modId.c_str(), id.file.c_str(), id.type, id.id, id.sub, orig, b.size(), limits.maxWholeEntry);
            return false;
        }
        if (b.size() > orig)
            logf("mods: %s: %s %04x/%u/%u grows %zu -> %zu bytes (guest buffer sizes unverified)", s->modId.c_str(),
                 id.file.c_str(), id.type, id.id, id.sub, orig, b.size());
        data = std::move(b);
        return true;
    }
};

// ---- SRSC patching ------------------------------------------------------------------------------------------------
bool NewEntryKey::operator<(const NewEntryKey& o) const {
    return std::tie(file, type, id, sub) < std::tie(o.file, o.type, o.id, o.sub);
}

namespace {
struct OdbCounts {
    size_t classes = 0, groups = 0;
};
size_t countType(const std::vector<SrscEntry>& ents, u16 type) {
    size_t n = 0;
    for (const SrscEntry& e : ents)
        n += e.type == type;
    return n;
}
bool patchesTouchCounts(const OdbCounts& before, const std::vector<SrscEntry>& after) {
    return before.classes != countType(after, 0x102) || before.groups != countType(after, 0x101);
}
} // namespace

bool patchSrsc(const std::vector<u8>& in, const std::vector<SrscPatch>& patches, std::vector<u8>& out,
               std::string& err) {
    if (in.size() < 16 || memcmp(in.data(), "SRSC", 4) != 0) {
        err = "not an SRSC container";
        return false;
    }
    const u32 idx = rd32(&in[6]), cnt = rd32(&in[10]);
    if (idx < 16 || idx > in.size() || (u64)cnt * 14 > in.size() - idx) {
        err = "SRSC index out of range";
        return false;
    }
    std::vector<SrscEntry> ents(cnt);
    for (u32 i = 0; i < cnt; ++i) {
        const u8* q = &in[(size_t)idx + (size_t)i * 14];
        SrscEntry& e = ents[i];
        e.type = (u16)rd16(q);
        e.id = (u16)rd16(q + 2);
        e.sub = (u16)rd16(q + 4);
        e.off = rd32(q + 6);
        e.size = rd32(q + 10);
        if ((u64)e.off + e.size > idx) {
            err = "SRSC entry outside the data region";
            return false;
        }
    }
    OdbCounts origCounts;
    origCounts.classes = countType(ents, 0x102);
    origCounts.groups = countType(ents, 0x101);
    out.assign(in.begin(), in.begin() + idx);
    auto append = [&](const std::vector<u8>& d) {
        while (out.size() % 8)
            out.push_back(0);
        const u32 o = (u32)out.size();
        out.insert(out.end(), d.begin(), d.end());
        return o;
    };
    auto shared = [&](size_t self) {
        const SrscEntry& a = ents[self];
        for (size_t j = 0; j < ents.size(); ++j) {
            if (j == self || ents[j].size == 0 || a.size == 0)
                continue;
            if (ents[j].off < (u64)a.off + a.size && a.off < (u64)ents[j].off + ents[j].size)
                return true;
        }
        return false;
    };
    for (const SrscPatch& p : patches) {
        if (p.data.size() > 0xFFFFFFF0u) {
            err = "entry too large";
            return false;
        }
        bool hit = false;
        for (size_t i = 0; i < ents.size(); ++i) {
            SrscEntry& e = ents[i];
            const bool subOk = (p.sub == 0xFFFF && !p.add) || e.sub == p.sub;
            if (e.type != p.type || e.id != p.id || !subOk)
                continue;
            hit = true;
            if (p.data.size() <= e.size && !shared(i)) {
                if (!p.data.empty())
                    memcpy(&out[e.off], p.data.data(), p.data.size());
                e.size = (u32)p.data.size();
            } else {
                e.off = append(p.data);
                e.size = (u32)p.data.size();
            }
        }
        if (!hit) {
            if (p.add) {
                if (p.sub == 0xFFFF) {
                    err = "a new entry needs an exact sub id";
                    return false;
                }
                SrscEntry e{p.type, p.id, p.sub, append(p.data), (u32)p.data.size()};
                ents.push_back(e);
            } else
                logf("mods: patch for %04x/%u/%d matches no entry, skipped", p.type, p.id, (s16)p.sub);
        }
    }
    // ODB container header record 0x0100 (id 0, sub 0): u16 class count (0x0102 records), u16 group count (0x0101
    // records), then the library path. The guest sizes its per-ODB class and group tables from these two counts and
    // fills them from the index; records beyond the declared counts overflow the tables (2 extra records fit in the
    // allocator's slack, 3 or more corrupt the heap: the level never finishes loading). Keep the counts in step with
    // the index when records were added or removed, unless the pack set them itself (then they differ from the
    // original counts and stay as written).
    if (patchesTouchCounts(origCounts, ents)) {
        for (SrscEntry& e : ents) {
            if (e.type != 0x100 || e.id != 0 || e.sub != 0 || e.size < 4 || (u64)e.off + e.size > out.size())
                continue;
            std::vector<u8> hdr(out.begin() + e.off, out.begin() + e.off + e.size);
            const u32 c = rd16(&hdr[0]), g = rd16(&hdr[2]);
            if (c != origCounts.classes || g != origCounts.groups)
                break; // not the counts we expect (not an ODB, or the pack wrote its own)
            const u16 nc = (u16)std::min<size_t>(countType(ents, 0x102), 0xFFFF);
            const u16 ng = (u16)std::min<size_t>(countType(ents, 0x101), 0xFFFF);
            memcpy(&hdr[0], &nc, 2);
            memcpy(&hdr[2], &ng, 2);
            e.off = append(hdr);
            e.size = (u32)hdr.size();
            logf("mods: ODB header counts updated: %u -> %u classes, %u -> %u groups", c, nc, g, ng);
            break;
        }
    }
    while (out.size() % 8)
        out.push_back(0);
    const u32 newIdx = (u32)out.size();
    out.resize(out.size() + ents.size() * 14);
    for (size_t i = 0; i < ents.size(); ++i) {
        u8* q = &out[newIdx + i * 14];
        memcpy(q, &ents[i].type, 2);
        memcpy(q + 2, &ents[i].id, 2);
        memcpy(q + 4, &ents[i].sub, 2);
        memcpy(q + 6, &ents[i].off, 4);
        memcpy(q + 10, &ents[i].size, 4);
    }
    // header: keep version and pad, update index offset and count
    memcpy(out.data(), in.data(), 6);
    memcpy(out.data() + 6, &newIdx, 4);
    const u32 n = (u32)ents.size();
    memcpy(out.data() + 10, &n, 4);
    memcpy(out.data() + 14, in.data() + 14, 2);
    return true;
}

// ---- manager ------------------------------------------------------------------------------------------------------
ModManager::ModManager(DiscProfile profile, ModLimits limits)
    : profile_(std::move(profile)), limits_(limits), merged_(std::make_shared<ModOverride>()),
      texts_(std::make_shared<XdbTextOverride>()) {
    merged_->limits = limits_;
}
ModManager::~ModManager() = default;

size_t ModManager::entryCount() const {
    return merged_->map.size();
}
std::shared_ptr<IResourceOverride> ModManager::entries() const {
    return merged_->map.empty() ? nullptr : merged_;
}

static std::string keyStr(const ModOverride::Key& k) {
    char b[64];
    std::snprintf(b, sizeof b, " %04x/%u/", k.type, k.id);
    return k.file + b + (k.sub == 0xFFFF ? std::string("x") : std::to_string(k.sub));
}

static bool readManifestFrom(const ModArchive& a, Manifest& m, std::string& prefix, std::string& err) {
    std::string found;
    bool json = false;
    for (auto& it : a.list()) {
        std::string l = lower(it.path);
        for (const char* n : {"mod.toml", "mod.json"}) {
            std::string nm = n;
            bool root = l == nm;
            bool one = l.size() > nm.size() + 1 && l.compare(l.size() - nm.size(), nm.size(), nm) == 0 &&
                       l[l.size() - nm.size() - 1] == '/' && l.find('/') == l.size() - nm.size() - 1;
            if ((root || one) && (found.empty() || (root && found.find('/') != std::string::npos))) {
                found = it.path;
                json = nm == "mod.json";
            }
        }
    }
    if (found.empty()) {
        err = "no mod.toml or mod.json";
        return false;
    }
    std::vector<u8> b;
    if (!a.read(found, b)) {
        err = "cannot read " + found;
        return false;
    }
    size_t sl = found.rfind('/');
    prefix = sl == std::string::npos ? std::string() : found.substr(0, sl + 1);
    return parseManifest(std::string(b.begin(), b.end()), json, m, err);
}

static std::string stemOf(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) ? p.filename().string() : p.stem().string();
}

namespace {
struct Cand {
    ModStatus st;
    std::shared_ptr<ModArchive> archive;
    std::string prefix;
};
} // namespace

size_t ModManager::load(const std::string& modsDir) {
    std::error_code ec;
    std::vector<std::pair<std::string, bool>> order;
    const fs::path iniPath = fs::path(modsDir) / "mods.ini";
    std::set<std::string> listed;
    {
        std::ifstream in(iniPath);
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[')
                continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string k = trim(line.substr(0, eq)), v = lower(trim(line.substr(eq + 1)));
            if (k.empty() || !listed.insert(lower(k)).second)
                continue;
            order.emplace_back(k, v == "on" || v == "1" || v == "true" || v == "yes");
        }
    }
    // new packs on disk: appended as "off" (the player enables them by editing the file)
    std::vector<std::string> fresh;
    if (fs::is_directory(modsDir, ec)) {
        std::vector<std::string> names;
        for (auto& e : fs::directory_iterator(modsDir, ec)) {
            bool zip = e.is_regular_file(ec) && lower(e.path().extension().string()) == ".zip";
            if (e.is_directory(ec) || zip)
                names.push_back(stemOf(e.path()));
        }
        std::sort(names.begin(), names.end());
        for (auto& n : names)
            if (listed.insert(lower(n)).second) {
                order.emplace_back(n, false);
                fresh.push_back(n);
            }
        if (!fresh.empty()) {
            const bool isNew = !fs::exists(iniPath, ec) || fs::file_size(iniPath, ec) == 0;
            std::ofstream o(iniPath, std::ios::app);
            if (o) {
                if (isNew)
                    o << "; Drakan mods. One pack per line (folder name or zip name without .zip). Top to bottom is\n"
                         "; the load order; later packs win conflicts. Set a pack to on to enable it, off to disable.\n"
                         "[mods]\n";
                for (auto& n : fresh)
                    o << n << " = off\n";
            }
        }
    }
    return load(modsDir, order);
}

static bool validLevelName(const std::string& n, std::string& why) {
    if (n.empty() || n.size() > 80) {
        why = "name must be 1..80 characters";
        return false;
    }
    for (unsigned char c : n)
        if (c < 0x20 || c >= 0x7f || strchr("\\/:*?\"<>|;", c)) {
            why = "name has a character that cannot be part of a disc path";
            return false;
        }
    return true;
}

size_t ModManager::load(const std::string& modsDir, const std::vector<std::pair<std::string, bool>>& order) {
    status_.clear();
    conflicts_.clear();
    std::vector<Cand> cands;
    // locate every pack by key (folder name or zip stem), case-insensitively
    std::map<std::string, fs::path> found;
    std::error_code ec;
    if (fs::is_directory(modsDir, ec))
        for (auto& e : fs::directory_iterator(modsDir, ec)) {
            bool zip = e.is_regular_file(ec) && lower(e.path().extension().string()) == ".zip";
            if (e.is_directory(ec) || zip)
                found.emplace(lower(stemOf(e.path())), e.path());
        }
    // ---- phase A: manifests and per-pack validation
    for (auto& [key, on] : order) {
        Cand c;
        c.st.key = key;
        c.st.enabled = on;
        auto it = found.find(lower(key));
        if (it == found.end()) {
            c.st.state = ModState::Skipped;
            c.st.message = "listed in mods.ini but not found in " + modsDir;
            if (on)
                logf("mods: %s: %s", key.c_str(), c.st.message.c_str());
            status_.push_back(std::move(c.st));
            continue;
        }
        c.st.source = it->second.string();
        std::string err;
        c.archive = openArchive(c.st.source, err);
        if (c.archive && readManifestFrom(*c.archive, c.st.manifest, c.prefix, err))
            c.st.hasManifest = true;
        if (!on) {
            c.st.state = ModState::Disabled;
            c.st.message = "disabled in mods.ini";
            status_.push_back(std::move(c.st));
            continue;
        }
        c.st.state = ModState::Skipped;
        if (!c.st.hasManifest) {
            c.st.message = "invalid pack: " + err;
        } else {
            const Manifest& m = c.st.manifest;
            if (m.format > kManifestFormat)
                c.st.message = "needs mod format " + std::to_string(m.format) + " (this runtime reads up to " +
                               std::to_string(kManifestFormat) + ")";
            else if (m.profiles.empty() && m.profileHashes.empty())
                c.st.message = "manifest has no target profile (set target.profile = \"" + profile_.id + "\" or \"*\")";
            else {
                bool pOk = m.profiles.empty(), hOk = m.profileHashes.empty();
                for (auto& p : m.profiles)
                    if (p == "*" || lower(p) == lower(profile_.id))
                        pOk = true;
                for (auto& h : m.profileHashes) {
                    u64 v;
                    if (parseHex64(h, v) && v == profile_.hash)
                        hOk = true;
                }
                if (!pOk || !hOk) {
                    std::string want;
                    for (auto& p : m.profiles)
                        want += (want.empty() ? "" : ",") + p;
                    for (auto& h : m.profileHashes)
                        want += (want.empty() ? "" : ",") + h;
                    c.st.message = "targets disc profile " + want + " but this build is " + profile_.id + " (" +
                                   hex64(profile_.hash) + ")";
                } else
                    c.st.state = ModState::Loaded; // provisional
            }
        }
        if (c.st.state == ModState::Skipped)
            logf("mods: skipping '%s': %s", key.c_str(), c.st.message.c_str());
        cands.push_back(std::move(c));
    }
    // ---- duplicate ids: the earlier pack keeps the id
    {
        std::map<std::string, std::string> seen;
        for (auto& c : cands)
            if (c.st.state == ModState::Loaded) {
                auto r = seen.emplace(c.st.manifest.id, c.st.key);
                if (!r.second) {
                    c.st.state = ModState::Skipped;
                    c.st.message = "id '" + c.st.manifest.id + "' is already used by '" + r.first->second + "'";
                    logf("mods: skipping '%s': %s", c.st.key.c_str(), c.st.message.c_str());
                }
            }
    }
    // ---- dependencies, to a fixpoint
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& c : cands) {
            if (c.st.state != ModState::Loaded)
                continue;
            for (auto& d : c.st.manifest.dependencies) {
                std::string id = d, req;
                size_t op = d.find(">=");
                if (op != std::string::npos) {
                    id = d.substr(0, op);
                    req = trim(d.substr(op + 2));
                }
                id = trim(id);
                const Cand* dep = nullptr;
                for (auto& o : cands)
                    if (o.st.state == ModState::Loaded && o.st.manifest.id == id)
                        dep = &o;
                std::string why;
                if (!dep)
                    why = "needs '" + id + "', which is not installed, not enabled or was skipped";
                else if (!req.empty() && compareVersions(dep->st.manifest.version, req) < 0)
                    why = "needs '" + id + "' >= " + req + " but " + dep->st.manifest.version + " is installed";
                if (!why.empty()) {
                    c.st.state = ModState::Skipped;
                    c.st.message = why;
                    logf("mods: skipping '%s': %s", c.st.key.c_str(), why.c_str());
                    changed = true;
                    break;
                }
            }
        }
    }
    // ---- load order hints (warnings only; the mods.ini order is authoritative)
    {
        std::map<std::string, size_t> pos;
        for (size_t i = 0; i < cands.size(); ++i)
            if (cands[i].st.state == ModState::Loaded)
                pos[cands[i].st.manifest.id] = i;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (cands[i].st.state != ModState::Loaded)
                continue;
            const Manifest& m = cands[i].st.manifest;
            auto after = m.loadAfter;
            for (auto& d : m.dependencies)
                after.push_back(trim(d.substr(0, d.find(">="))));
            for (auto& a : after) {
                auto it = pos.find(a);
                if (it != pos.end() && it->second > i)
                    logf("mods: '%s' asks to load after '%s' but mods.ini lists it later", m.id.c_str(), a.c_str());
            }
            for (auto& b : m.loadBefore) {
                auto it = pos.find(b);
                if (it != pos.end() && it->second < i)
                    logf("mods: '%s' asks to load before '%s' but mods.ini lists it earlier", m.id.c_str(), b.c_str());
            }
        }
    }
    // ---- scan entries and merge, in load order
    size_t loaded = 0, modIndex = 0;
    std::map<std::string, std::string> textOwner; // "FILE|id.lang" -> mod id
    merged_->map.clear();
    files_.clear();
    newEntries_.clear();
    levelDefs_.clear();
    extraLevels_.clear();
    mapFiles_.clear();
    previewFiles_.clear();
    texts_ = std::make_shared<XdbTextOverride>();
    textCount_ = 0;
    for (auto& c : cands) {
        if (c.st.state != ModState::Loaded) {
            status_.push_back(std::move(c.st));
            continue;
        }
        const std::string mid = c.st.manifest.id;
        const std::string root = lower(c.prefix) + "overrides/";
        const std::string filesRoot = lower(c.prefix) + "files/", newRoot = lower(c.prefix) + "new/";
        size_t nEntries = 0, nTexts = 0, nRejected = 0, nFiles = 0, nNew = 0, nLevels = 0;
        ++modIndex;
        for (const std::string& ln : c.st.manifest.levels) {
            std::string why;
            if (!validLevelName(ln, why)) {
                ++nRejected;
                logf("mods: %s: ignoring level '%s': %s", mid.c_str(), ln.c_str(), why.c_str());
                continue;
            }
            bool dup = false;
            for (auto& d : levelDefs_)
                if (lower(d.name) == lower(ln)) {
                    dup = true;
                    logf("mods: conflict: level '%s' is registered by '%s' and '%s'; '%s' keeps it", ln.c_str(),
                         d.modId.c_str(), mid.c_str(), d.modId.c_str());
                    conflicts_.push_back({"level " + ln, d.modId, mid});
                }
            if (!dup) {
                levelDefs_.push_back({ln, mid, "\\Levels\\" + ln + "\\" + ln + ".lvl", -1});
                ++nLevels;
            }
        }
        for (auto& it : c.archive->list()) {
            const std::string lowPath = lower(it.path);
            const std::string mapsRoot = lower(c.prefix) + "maps/";
            if (lowPath.rfind(mapsRoot, 0) == 0 && lowPath.size() > mapsRoot.size() + 4 &&
                lowPath.compare(lowPath.size() - 4, 4, ".ini") == 0 && lowPath.find('/', mapsRoot.size()) == std::string::npos) {
                std::vector<u8> text;
                if (it.size <= (1u << 20) && c.archive->read(it.path, text))
                    mapFiles_.emplace_back("mod " + mid + ": " + it.path, std::string(text.begin(), text.end()));
                continue;
            }
            const std::string prevRoot = lower(c.prefix) + "previews/";
            if (lowPath.rfind(prevRoot, 0) == 0 && lowPath.size() > prevRoot.size() + 4 &&
                lowPath.compare(lowPath.size() - 4, 4, ".png") == 0 && lowPath.find('/', prevRoot.size()) == std::string::npos) {
                std::vector<u8> png;
                if (it.size <= (8u << 20) && c.archive->read(it.path, png))
                    previewFiles_.emplace_back(it.path.substr(prevRoot.size(), it.path.size() - prevRoot.size() - 4),
                                               std::move(png));
                continue;
            }
            if (lowPath.rfind(filesRoot, 0) == 0 || lowPath.rfind(newRoot, 0) == 0) {
                const bool isFile = lowPath.rfind(filesRoot, 0) == 0;
                const std::string rest = it.path.substr((isFile ? filesRoot : newRoot).size());
                if (rest.empty() || rest.back() == '/')
                    continue;
                const u64 cap = isFile ? limits_.maxWholeFile : limits_.maxFileBytes;
                if (it.size == 0 || it.size > cap) {
                    ++nRejected;
                    logf("mods: %s: rejecting %s: %llu bytes (allowed 1..%llu)", mid.c_str(), it.path.c_str(),
                         (unsigned long long)it.size, (unsigned long long)cap);
                    continue;
                }
                if (isFile) {
                    const std::string key = manglePath(rest);
                    auto old = files_.find(key);
                    if (old != files_.end() && old->second.modId != mid) {
                        conflicts_.push_back({"file " + key, mid, old->second.modId});
                        logf("mods: conflict: file %s from '%s' and '%s'; '%s' wins", key.c_str(),
                             old->second.modId.c_str(), mid.c_str(), mid.c_str());
                    }
                    files_[key] = PackSource{c.archive, it.path, modIndex, mid, it.size};
                    ++nFiles;
                } else {
                    const size_t sl = rest.find('/');
                    const std::string nm = sl == std::string::npos ? std::string() : rest.substr(sl + 1);
                    NewEntryKey k;
                    if (sl == std::string::npos || nm.find('/') != std::string::npos || nm.size() < 5 ||
                        lower(nm.substr(nm.size() - 4)) != ".bin" ||
                        !parseEntryName(nm.substr(0, nm.size() - 4), k.type, k.id, k.sub) || k.sub == 0xFFFF) {
                        ++nRejected;
                        logf("mods: %s: ignoring %s (want new/<FILE>/<type hex>_<id>_<sub>.bin with an exact sub)",
                             mid.c_str(), it.path.c_str());
                        continue;
                    }
                    k.file = upper(rest.substr(0, sl));
                    auto old = newEntries_.find(k);
                    if (old != newEntries_.end() && old->second.modId != mid) {
                        conflicts_.push_back({"new " + k.file, mid, old->second.modId});
                        logf("mods: conflict: new entry %s %04x/%u/%u from '%s' and '%s'; '%s' wins", k.file.c_str(),
                             k.type, k.id, k.sub, old->second.modId.c_str(), mid.c_str(), mid.c_str());
                    }
                    newEntries_[k] = PackSource{c.archive, it.path, modIndex, mid, it.size};
                    ++nNew;
                }
                continue;
            }
            if (lower(it.path).rfind(root, 0) != 0)
                continue;
            std::string rest = it.path.substr(root.size()); // <FILE>/<name>
            size_t sl = rest.find('/');
            if (sl == std::string::npos || sl == 0 || rest.find('/', sl + 1) != std::string::npos)
                continue;
            std::string file = upper(rest.substr(0, sl)), name = rest.substr(sl + 1);
            if (lower(name) == "text.ini") {
                std::vector<u8> b;
                if (!c.archive->read(it.path, b)) {
                    ++nRejected;
                    logf("mods: %s: cannot read %s", mid.c_str(), it.path.c_str());
                    continue;
                }
                std::string content(b.begin(), b.end());
                nTexts += texts_->loadIni(file, content);
                size_t p = 0;
                while (p < content.size()) {
                    size_t e = content.find('\n', p);
                    std::string ln = trim(content.substr(p, e == std::string::npos ? std::string::npos : e - p));
                    p = e == std::string::npos ? content.size() : e + 1;
                    size_t eq = ln.find('=');
                    if (ln.empty() || ln[0] == ';' || ln[0] == '#' || eq == std::string::npos)
                        continue;
                    std::string k = lower(trim(ln.substr(0, eq)));
                    if (k.find('.') == std::string::npos)
                        k += ".*";
                    auto r = textOwner.emplace(file + "|" + k, mid);
                    if (!r.second && r.first->second != mid) {
                        conflicts_.push_back({file + " text " + k, mid, r.first->second});
                        logf("mods: conflict: %s text %s set by '%s' and '%s'; '%s' wins", file.c_str(), k.c_str(),
                             r.first->second.c_str(), mid.c_str(), mid.c_str());
                        r.first->second = mid;
                    }
                }
                continue;
            }
            if (name.size() < 5 || lower(name.substr(name.size() - 4)) != ".bin")
                continue;
            ModOverride::Key k;
            k.file = file;
            if (!parseEntryName(name.substr(0, name.size() - 4), k.type, k.id, k.sub)) {
                ++nRejected;
                logf("mods: %s: ignoring file name %s (want <type hex>_<id>_<sub|x>.bin)", mid.c_str(),
                     it.path.c_str());
                continue;
            }
            if (it.size == 0 || it.size > limits_.maxFileBytes) {
                ++nRejected;
                logf("mods: %s: rejecting %s: %llu bytes (allowed 1..%llu)", mid.c_str(), it.path.c_str(),
                     (unsigned long long)it.size, (unsigned long long)limits_.maxFileBytes);
                continue;
            }
            // conflicts: same key, or a wildcard sub overlapping a specific one, from an earlier pack
            for (auto m = merged_->map.lower_bound(ModOverride::Key{k.file, k.type, k.id, 0});
                 m != merged_->map.end() && m->first.file == k.file && m->first.type == k.type && m->first.id == k.id;
                 ++m) {
                bool overlap = m->first.sub == k.sub || m->first.sub == 0xFFFF || k.sub == 0xFFFF;
                if (overlap && m->second.modId != mid) {
                    conflicts_.push_back({keyStr(k), mid, m->second.modId});
                    logf("mods: conflict: %s overlaps %s from '%s'; '%s' wins", keyStr(k).c_str(),
                         keyStr(m->first).c_str(), m->second.modId.c_str(), mid.c_str());
                }
            }
            merged_->map[k] = ModOverride::Src{c.archive, it.path, modIndex, mid, it.size};
            ++nEntries;
        }
        c.st.entries = nEntries;
        c.st.texts = nTexts;
        c.st.rejected = nRejected;
        c.st.files = nFiles;
        c.st.added = nNew;
        c.st.levels = nLevels;
        c.st.message = std::to_string(nEntries) + " entries, " + std::to_string(nTexts) + " text lines" +
                       (nFiles ? ", " + std::to_string(nFiles) + " files" : std::string()) +
                       (nNew ? ", " + std::to_string(nNew) + " new entries" : std::string()) +
                       (nLevels ? ", " + std::to_string(nLevels) + " levels" : std::string()) +
                       (nRejected ? ", " + std::to_string(nRejected) + " rejected" : std::string());
        logf("mods: loaded '%s' %s (%s): %s", mid.c_str(), c.st.manifest.version.c_str(), c.st.key.c_str(),
             c.st.message.c_str());
        textCount_ += nTexts;
        ++loaded;
        status_.push_back(std::move(c.st));
    }
    return loaded;
}

// ---- files, patched containers, levels -----------------------------------------------------------------------------
static bool isSrscFile(const std::string& name) {
    static const char* const kExt[] = {".LVL", ".MOD", ".TXD", ".ODB", ".ADB", ".SDB", ".XDB", ".VDB", ".SSD", ".RRC"};
    const std::string up = upper(name);
    for (const char* e : kExt)
        if (up.size() > strlen(e) && up.compare(up.size() - strlen(e), strlen(e), e) == 0)
            return true;
    return false;
}
// Containers the guest reads with plain 0x1201 file reads: entry overrides never reach the 0x1202 override layer, so
// they are applied by rebuilding the file. (Everything else is patched per request.)
static bool isPlainReadContainer(const std::string& name) {
    const std::string up = upper(name);
    // .LVL and .ODB are read by the guest with plain file reads (no 0x1202 entry requests are ever made for them: checked
    // with DRAKAN_HLE_LOG on SYSTEM.ODB), so a per-request override never reaches them; their entry patches rebuild the file.
    return up.size() > 4 && (up.compare(up.size() - 4, 4, ".LVL") == 0 || up.compare(up.size() - 4, 4, ".ODB") == 0);
}
static bool srscIndexOf(const std::vector<u8>& b, std::vector<SrscEntry>& out) {
    if (b.size() < 16 || memcmp(b.data(), "SRSC", 4) != 0)
        return false;
    const u32 idx = rd32(&b[6]), cnt = rd32(&b[10]);
    if (idx < 16 || idx > b.size() || (u64)cnt * 14 > b.size() - idx)
        return false;
    out.resize(cnt);
    for (u32 i = 0; i < cnt; ++i) {
        const u8* q = &b[(size_t)idx + (size_t)i * 14];
        out[i] = SrscEntry{(u16)rd16(q), (u16)rd16(q + 2), (u16)rd16(q + 4), rd32(q + 6), rd32(q + 10)};
        if ((u64)out[i].off + out[i].size > idx)
            return false;
    }
    return true;
}
static std::string baseOf(const std::string& key) {
    const size_t sl = key.find_last_of('\\');
    return sl == std::string::npos ? key : key.substr(sl + 1);
}

size_t ModManager::installFiles(IsoFs& fsys) {
    size_t served = 0;
    // 1. whole files: replacement or new
    for (auto& [pk, src] : files_) {
        std::vector<u8> b;
        if (!src.archive->read(src.path, b) || b.empty()) {
            logf("mods: %s: cannot read %s, file skipped", src.modId.c_str(), src.path.c_str());
            continue;
        }
        const std::string key = fsys.resolveKey(pk);
        const std::string name = baseOf(key);
        IsoFs::Node orig;
        const bool hasOrig = fsys.findKey(key, orig) && !orig.dir;
        bool ok = true;
        if (isSrscFile(name)) {
            std::vector<SrscEntry> ne;
            if (!srscIndexOf(b, ne)) {
                logf("mods: %s: %s is not a valid SRSC container (header or index), file rejected", src.modId.c_str(),
                     src.path.c_str());
                ok = false;
            } else {
                std::vector<SrscEntry> oe;
                if (hasOrig) {
                    // header + index of the original, without reading the whole file
                    u8 h[16];
                    if (fsys.read(orig, 0, h, 16) == 16 && memcmp(h, "SRSC", 4) == 0) {
                        const u32 idx = rd32(h + 6), cnt = rd32(h + 10);
                        std::vector<u8> ix((size_t)cnt * 14);
                        if (idx <= orig.size && ix.size() <= orig.size - idx &&
                            fsys.read(orig, idx, ix.data(), ix.size()) == ix.size())
                            for (u32 i = 0; i < cnt; ++i) {
                                const u8* q = &ix[(size_t)i * 14];
                                oe.push_back(SrscEntry{(u16)rd16(q), (u16)rd16(q + 2), (u16)rd16(q + 4), rd32(q + 6),
                                                       rd32(q + 10)});
                            }
                    }
                }
                for (const SrscEntry& e : ne) {
                    if (e.size <= limits_.maxWholeEntry)
                        continue;
                    const SrscEntry* o = nullptr;
                    for (const SrscEntry& x : oe)
                        if (x.type == e.type && x.id == e.id && x.sub == e.sub)
                            o = &x;
                    if (hasOrig && !(o && o->size > limits_.maxWholeEntry)) {
                        logf("mods: %s: %s entry %04x/%u/%u is %u bytes, past the %u byte whole-entry limit of the "
                             "resource loader (original %s); file rejected",
                             src.modId.c_str(), src.path.c_str(), e.type, e.id, e.sub, e.size, limits_.maxWholeEntry,
                             o ? (std::to_string(o->size) + " bytes").c_str() : "has no such entry");
                        ok = false;
                        break;
                    }
                    if (!hasOrig)
                        logf("mods: %s: %s entry %04x/%u/%u is %u bytes (past 0x%x; fine only if the engine streams "
                             "that type in chunks)",
                             src.modId.c_str(), src.path.c_str(), e.type, e.id, e.sub, e.size, limits_.maxWholeEntry);
                }
            }
        }
        if (!ok)
            continue;
        const u64 n = b.size();
        fsys.addMemoryFile(key, std::move(b));
        ++served;
        logf("mods: %s: %s %s (%llu bytes)", src.modId.c_str(), hasOrig ? "replaced" : "added", key.c_str(),
             (unsigned long long)n);
    }

    // 2. entry patches of containers read with plain file reads (.LVL overrides) and `new/` entries
    struct Item {
        size_t modIndex;
        int wild; // 0 = x sub (applied first), 1 = exact
        SrscPatch patch;
    };
    std::map<std::string, std::vector<Item>> byFile;
    for (auto& [k, src] : merged_->map) {
        if (!isPlainReadContainer(k.file))
            continue;
        SrscPatch p;
        p.type = k.type;
        p.id = k.id;
        p.sub = k.sub;
        if (!src.archive->read(src.path, p.data) || p.data.empty()) {
            logf("mods: %s: cannot read %s", src.modId.c_str(), src.path.c_str());
            continue;
        }
        byFile[k.file].push_back({src.modIndex, k.sub == 0xFFFF ? 0 : 1, std::move(p)});
    }
    for (auto& [k, src] : newEntries_) {
        SrscPatch p;
        p.type = k.type;
        p.id = k.id;
        p.sub = k.sub;
        p.add = true;
        if (!src.archive->read(src.path, p.data) || p.data.empty()) {
            logf("mods: %s: cannot read %s", src.modId.c_str(), src.path.c_str());
            continue;
        }
        byFile[k.file].push_back({src.modIndex, 1, std::move(p)});
    }
    for (auto& [file, items] : byFile) {
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return std::tie(a.modIndex, a.wild) < std::tie(b.modIndex, b.wild);
        });
        std::vector<SrscPatch> patches;
        for (Item& it : items)
            patches.push_back(std::move(it.patch));
        std::vector<std::string> targets;
        static const char* const kLangs[] = {"ENGLISH", "FRENCH", "GERMAN", "ITALIAN", "SPANISH"};
        const std::string lang = fsys.languageRoot();
        for (auto& [key, n] : fsys.entries()) {
            if (n.dir || !(key == file || (key.size() > file.size() && key[key.size() - file.size() - 1] == '\\' &&
                                           key.compare(key.size() - file.size(), file.size(), file) == 0)))
                continue;
            // only the running language tree (the other trees have their own entry sizes and are never read)
            const std::string first = key.substr(0, key.find('\\'));
            bool otherLang = false;
            for (const char* l : kLangs)
                otherLang = otherLang || (first == l && first != lang);
            if (!otherLang)
                targets.push_back(key);
        }
        if (targets.empty())
            logf("mods: no file named %s on the disc, %zu entry patch(es) dropped", file.c_str(), patches.size());
        for (const std::string& key : targets) {
            IsoFs::Node n;
            if (!fsys.findKey(key, n))
                continue;
            std::vector<u8> out, err_;
            std::string err;
            if (!patchSrsc(fsys.readAll(n), patches, out, err)) {
                logf("mods: cannot patch %s: %s", key.c_str(), err.c_str());
                continue;
            }
            const size_t osz = out.size();
            fsys.addMemoryFile(key, std::move(out));
            ++served;
            logf("mods: %s rebuilt with %zu entry patch(es) (%llu -> %zu bytes)", key.c_str(), patches.size(),
                 (unsigned long long)n.size, osz);
        }
    }

    // 3. LEVELLST.TXT
    if (!levelDefs_.empty()) {
        IsoFs::Node ln;
        const std::string lkey = fsys.resolveKey("LEVELLST.TXT");
        if (!fsys.findKey(lkey, ln) || ln.dir) {
            logf("mods: no LEVELLST.TXT on the disc (%s), %zu level(s) not registered", lkey.c_str(),
                 levelDefs_.size());
        } else {
            std::vector<u8> raw = fsys.readAll(ln);
            std::string text(raw.begin(), raw.end());
            std::vector<std::string> lines;
            for (size_t p = 0; p < text.size();) {
                size_t e = text.find('\n', p);
                std::string l = trim(text.substr(p, e == std::string::npos ? std::string::npos : e - p));
                p = e == std::string::npos ? text.size() : e + 1;
                if (!l.empty())
                    lines.push_back(l);
            }
            bool changed = false;
            for (ExtraLevel d : levelDefs_) {
                IsoFs::Node lvl;
                if (!fsys.lookup(d.path, lvl) || lvl.dir) {
                    logf(
                        "mods: %s: level '%s' not registered: no file %s on the disc (ship it as files/<ENGLISH/LEVELS/"
                        "...>)",
                        d.modId.c_str(), d.name.c_str(), fsys.resolveKey(d.path).c_str());
                    continue;
                }
                bool present = false;
                for (auto& l : lines)
                    if (lower(l) == lower(d.path))
                        present = true;
                if (present) {
                    logf("mods: %s: level '%s' is already in LEVELLST.TXT", d.modId.c_str(), d.name.c_str());
                    continue;
                }
                d.listIndex = (int)lines.size();
                lines.push_back(d.path);
                extraLevels_.push_back(d);
                changed = true;
                logf("mods: %s: level '%s' registered at list index %d", d.modId.c_str(), d.name.c_str(), d.listIndex);
            }
            if (changed) {
                std::string out;
                for (auto& l : lines)
                    out += l + "\r\n";
                fsys.addMemoryFile(lkey, std::vector<u8>(out.begin(), out.end()));
                ++served;
            }
        }
    }
    return served;
}

std::vector<std::string> ModManager::report() const {
    std::vector<std::string> v;
    for (auto& s : status_) {
        const char* st = s.state == ModState::Loaded ? "loaded" : s.state == ModState::Disabled ? "off" : "skipped";
        std::string name = s.hasManifest ? s.manifest.name + " " + s.manifest.version : s.key;
        v.push_back(std::string(st) + ": " + name + " - " + s.message);
    }
    for (auto& c : conflicts_)
        v.push_back("conflict: " + c.what + " ('" + c.winner + "' overrides '" + c.loser + "')");
    return v;
}

} // namespace hle::mods
