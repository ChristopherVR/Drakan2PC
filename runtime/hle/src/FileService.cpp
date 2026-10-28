#include "hle/FileService.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace hle {

static void put32(void* p, u32 v) {
    memcpy(p, &v, 4);
}
static u32 get32(const void* p) {
    u32 v;
    memcpy(&v, p, 4);
    return v;
}

static Srsc parseSrsc(const IsoFs& fs, const IsoFs::Node& n) {
    Srsc s;
    u8 h[16];
    if (n.size < 16 || fs.read(n, 0, h, 16) != 16 || memcmp(h, "SRSC", 4) != 0)
        return s;
    u32 idx = get32(h + 6), cnt = get32(h + 10);
    if (idx > n.size || (u64)cnt * 14 > n.size - idx) {
        logf("SRSC %s: bad index (off %u cnt %u)", n.key.c_str(), idx, cnt);
        return s;
    }
    std::vector<u8> b((size_t)cnt * 14);
    if (fs.read(n, idx, b.data(), b.size()) != b.size())
        return s;
    s.indexOffset = idx;
    s.count = cnt;
    s.entries.resize(cnt);
    for (u32 i = 0; i < cnt; ++i) {
        const u8* p = &b[(size_t)i * 14];
        SrscEntry& e = s.entries[i];
        memcpy(&e.type, p, 2);
        memcpy(&e.id, p + 2, 2);
        memcpy(&e.sub, p + 4, 2);
        memcpy(&e.off, p + 6, 4);
        memcpy(&e.size, p + 10, 4);
    }
    s.valid = true;
    return s;
}

void FileService::setLevelRedirect(std::string from, std::string to) {
    std::lock_guard<std::mutex> lk(mu_);
    redirectFrom_ = std::move(from);
    redirectTo_ = std::move(to);
}

std::string FileService::redirectPath(const std::string& path) const {
    if (redirectFrom_.empty() || redirectTo_.empty())
        return path;
    auto lower = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return v;
    };
    const std::string from = lower(redirectFrom_);
    std::string out = path, low = lower(path);
    for (size_t at = low.find(from); at != std::string::npos; at = low.find(from, at + redirectTo_.size())) {
        out.replace(at, from.size(), redirectTo_);
        low.replace(at, from.size(), lower(redirectTo_));
    }
    return out;
}

bool FileService::get(s32 h, Open& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (h < 0 || (size_t)h >= files_.size() || !files_[h].used)
        return false;
    out = files_[h];
    return true;
}

int FileService::handle(u32 fno, const void* send, u32 sendSize, void* reply, u32 replyCap) {
    const u8* in = (const u8*)send;
    auto rep32 = [&](s32 v) -> int {
        if (replyCap < 4)
            return -1;
        memcpy(reply, &v, 4);
        return 4;
    };
    std::lock_guard<std::mutex> lk(mu_);
    switch (fno) {
        case 0: { // open: {u32 isWrite, u32 0, char path[0x100]} -> {s32 handle, u32 size}
            // The game sends short records (8 + strlen + 1) for some opens and only asks for the first 4 reply bytes
            // (the handle).
            if (sendSize < 8 || replyCap < 4)
                return -1;
            u32 isWrite = get32(in);
            char requested[0x101] = {};
            memcpy(requested, in + 8, std::min<u32>(0xff, sendSize - 8));
            const std::string redirected = redirectPath(requested);
            if (redirected != requested)
                logf("file open '%s' redirected to '%s'", requested, redirected.c_str());
            const char* path = redirected.c_str();
            s32 h = ERR_NOT_FOUND;
            u32 size = 0;
            if (isWrite) {
                logf("file open(write) '%s' unsupported", path); // TODO: saves go through libmc, not here
            } else {
                IsoFs::Node n;
                if (!fs_.lookup(path, n) || n.dir) {
                    logf("file open '%s' -> not found (key %s)", path, fs_.resolveKey(path).c_str());
                } else {
                    int slot = -1;
                    for (size_t i = 0; i < files_.size(); ++i)
                        if (!files_[i].used) {
                            slot = (int)i;
                            break;
                        }
                    if (slot < 0)
                        h = ERR_NO_HANDLE;
                    else {
                        Open& o = files_[slot];
                        o.used = true;
                        o.node = n;
                        o.pos = 0;
                        o.srsc = parseSrsc(fs_, n);
                        h = slot;
                        size = (u32)n.size;
                        if (verbose())
                            logf("file open '%s' -> %s handle %d size %u srsc=%d/%u", path, n.key.c_str(), h, size,
                                 (int)o.srsc.valid, o.srsc.count);
                    }
                }
            }
            memcpy(reply, &h, 4);
            if (replyCap < 8)
                return 4;
            put32((u8*)reply + 4, size);
            return 8;
        }
        case 1: { // close
            if (sendSize < 4)
                return -1;
            s32 h = (s32)get32(in);
            if (h < 0 || (size_t)h >= files_.size() || !files_[h].used)
                return rep32(ERR_BAD_HANDLE);
            files_[h] = Open{};
            return rep32(0);
        }
        case 2: { // read {handle, eeDest, size} -> bytesRead
            if (sendSize < 12)
                return -1;
            s32 h = (s32)get32(in);
            u32 dst = get32(in + 4), size = get32(in + 8);
            if (h < 0 || (size_t)h >= files_.size() || !files_[h].used)
                return rep32(ERR_BAD_HANDLE);
            Open& o = files_[h];
            u64 remain = o.pos < o.node.size ? o.node.size - o.pos : 0;
            u32 want = (u32)std::min<u64>(size, remain);
            u32 done = 0;
            std::vector<u8> buf(std::min<u32>(want, 1u << 20));
            while (done < want) {
                u32 n = std::min<u32>(want - done, (u32)buf.size());
                size_t got = fs_.read(o.node, o.pos + done, buf.data(), n);
                if (got != n) {
                    logf("file read: short read on %s", o.node.key.c_str());
                    want = done + (u32)got;
                    if (!got)
                        break;
                }
                if (!mem_.write(dst + done, buf.data(), got)) {
                    logf("file read: bad EE addr %08x", dst + done);
                    return rep32(-5);
                }
                done += (u32)got;
            }
            o.pos += done;
            return rep32((s32)done);
        }
        case 3:
            return rep32(0); // no-op
        case 4: {            // lseek {handle, off, whence} -> newPos
            if (sendSize < 12)
                return -1;
            s32 h = (s32)get32(in);
            s32 off = (s32)get32(in + 4);
            s32 whence = (s32)get32(in + 8);
            if (h < 0 || (size_t)h >= files_.size() || !files_[h].used)
                return rep32(ERR_BAD_HANDLE);
            Open& o = files_[h];
            int64_t np = (int64_t)o.pos;
            if (whence == 0)
                np = off;
            else if (whence == 1)
                np = (int64_t)o.pos + off;
            else if (whence == 2)
                np = (int64_t)o.node.size - off; // documented IOP quirk: offset SUBTRACTED
            else
                logf("file lseek: unknown whence %d", whence);
            if (np < 0)
                np = 0;
            o.pos = (u64)np;
            return rep32((s32)np);
        }
        case 5:
            return rep32(0x400000); // mem info, diagnostic only
        default:
            logf("file: unknown fno %u", fno);
            return rep32(-1);
    }
}

} // namespace hle
