#include "hle/ResourceService.h"
#include <algorithm>
#include <cstring>

namespace hle {

static void put32(u8* p, u32 v) {
    memcpy(p, &v, 4);
}

// RSRCIOP (disc build) 0x4fe8 / 0x4ffc / 0x5048, called from the loader thread when dest type == 1 (SPU2):
//   0x20: u16@0 |= 0x0600                      (first 16-byte block: flags |= 6  -> loop start+loop)
//   0x10: fix(len, set):   L=len; if u16@(L-2)==0x7777 then L-=16; u16@(L-16) |= 0x0300   (last block: loop end/repeat)
//   0x40: fix(len, clear): same L logic, u16@(L-16) &= 0xfcff;  then if u16@(len-2)==0x7777 zero the last 16 bytes
void ResourceService::adpcmFixup(std::vector<u8>& d, u16 flags) {
    auto rd16 = [&](size_t o) {
        u16 v;
        memcpy(&v, &d[o], 2);
        return v;
    };
    auto wr16 = [&](size_t o, u16 v) { memcpy(&d[o], &v, 2); };
    size_t len = d.size();
    if (len < 16)
        return;
    if (flags & 0x20)
        wr16(0, rd16(0) | 0x600);
    auto fix = [&](bool set) {
        size_t L = len;
        if (rd16(L - 2) == 0x7777) {
            if (L < 32)
                return;
            L -= 16;
        }
        wr16(L - 16, set ? (u16)(rd16(L - 16) | 0x300) : (u16)(rd16(L - 16) & 0xfcff));
    };
    if (flags & 0x10)
        fix(true);
    if (flags & 0x40) {
        fix(false);
        if (rd16(len - 2) == 0x7777)
            memset(&d[len - 16], 0, 16);
    }
}

s32 ResourceService::process(const FileService::Open& f, const ResourceRequestWire& r) {
    if (!f.srsc.valid) {
        logf("resource: file %s has no SRSC index", f.node.key.c_str());
        return ST_NOT_FOUND;
    }
    const SrscEntry* e = f.srsc.find(r.type, r.id, r.sub);
    if (!e) {
        logf("resource: %s: %04x/%u/%d not found", f.node.key.c_str(), r.type, r.id, (s16)r.sub);
        return ST_NOT_FOUND;
    }
    if (e->size == 0) {
        logf("resource: %04x/%u has size ZERO", r.type, r.id);
        return ST_ZERO;
    }

    if (observer_) {
        ResourceId oid;
        oid.path = f.node.key;
        size_t osl = oid.path.find_last_of("\\/");
        oid.file = osl == std::string::npos ? oid.path : oid.path.substr(osl + 1);
        oid.type = r.type;
        oid.id = r.id;
        oid.sub = e->sub;
        observer_(oid, r.offset);
    }

    // Override layer: when an override wants this entry, the complete payload is read, modified and then sliced like
    // the original.
    std::vector<u8> full;
    bool overridden = false;
    if (!overrides_.empty()) {
        ResourceId rid;
        rid.path = f.node.key;
        size_t sl = rid.path.find_last_of("\\/");
        rid.file = sl == std::string::npos ? rid.path : rid.path.substr(sl + 1);
        rid.type = r.type;
        rid.id = r.id;
        rid.sub = e->sub;
        for (auto& o : overrides_) {
            if (!o->wants(rid))
                continue;
            if (!overridden) {
                full.resize(e->size);
                if (files_.fs().read(f.node, (u64)e->off, full.data(), e->size) != e->size) {
                    logf("resource: short read in %s", f.node.key.c_str());
                    return ST_IO;
                }
                overridden = true;
            }
            if (o->apply(rid, full) && verbose())
                logf("resource: %s %04x/%u/%u overridden (%zu bytes)", rid.file.c_str(), r.type, r.id, e->sub,
                     full.size());
        }
    }
    const u32 esize = overridden ? (u32)full.size() : e->size;
    if (esize == 0) {
        logf("resource: %04x/%u override has size ZERO", r.type, r.id);
        return ST_ZERO;
    }
    if (r.offset >= esize) {
        logf("resource: offset %u beyond entry size %u", r.offset, esize);
        return ST_NOT_FOUND;
    }
    u32 avail = esize - r.offset;
    // TODO: length==0 semantics unverified; treated as "whole remainder".
    u32 len = r.length == 0 ? avail : std::min(r.length, avail);
    if (len > MAX_LEN) {
        logf("resource: len %u > 0x%x", len, MAX_LEN);
        return ST_TOO_BIG;
    }

    std::vector<u8> data(len);
    if (overridden)
        memcpy(data.data(), full.data() + r.offset, len);
    else if (files_.fs().read(f.node, (u64)e->off + r.offset, data.data(), len) != len) {
        logf("resource: short read in %s", f.node.key.c_str());
        return ST_IO;
    }

    u32 dtype = r.flags & 0xf;
    if (dtype == 0) {
        u32 dst = r.dest, allocOffset = 0;
        if (r.flags & 0x80) {
            // EE allocates the destination: IOP -> EE 0x1303 (blocking), fno = request ptr (cookie),
            // send/recv 16 bytes; send word0 = len rounded up to 4; reply word1 = EE address (0 = fail).
            u32 q[4] = {(len + 3) & ~3u, 0, 0, 0}, a[4] = {};
            int n = ee_.post(SID_EE_ALLOC, r.cookie, q, 16, a, 16, true);
            if (n < 8 || a[1] == 0) {
                logf("resource: EE alloc (0x1303) failed for cookie %08x", r.cookie);
                return ST_ALLOC;
            }
            dst = a[1];
            allocOffset = a[2]; // reply word 2 = allocation offset, echoed in the 0x1301 header (word 2)
            // The request's dest field is where the allocated EE address is stored for the requester (the player reads
            // it back, e.g. as the DMA source of a movie frame slot). Measured: dest = slot descriptor with alloc flag
            // 0x80.
            if (r.dest != 0)
                mem_.write(r.dest, &dst, 4);
            if (verbose())
                logf("resource: alloc ptr %08x (offset %08x) stored at dest %08x", dst, allocOffset, r.dest);
            // TODO: reply word2 is stored by the IOP in its per-request header (offset field of the 0x1301 header);
            // unknown use.
        }
        if (delivery_ == Delivery::Rpc1301 && (r.flags & 0x80) && dst != 0) {
            // Alloc flag (measured with the movie player): the data is DMA'd straight into the allocated buffer, the
            // 0x1301 call only carries the header and completes the request; the requester's slot pointer is
            // (re)written after the completion ran.
            if (!mem_.write(dst, data.data(), len)) {
                logf("resource: bad EE dest %08x", dst);
                return ST_IO;
            }
            // header word 0 = offset from the 0x1301 receive buffer to the data: the EE handler (0x1f0110) computes
            // data = buf + word0. Measured/disassembled (RSRCIOP 0x35a0-0x35f4: s3[0] = reply word 2 of the 0x1303
            // call). Inline data uses 0x10.
            u32 hdr[4] = {allocOffset, len, allocOffset, 0};
            ee_.post(SID_EE_STREAM, r.cookie, hdr, 16, nullptr, 0, false);
            if (r.dest != 0)
                mem_.write(r.dest, &dst, 4);
        } else if (delivery_ == Delivery::Direct &&
                   dst != 0) { // dst == 0: the data can only reach the request object through 0x1301
            if (!mem_.write(dst, data.data(), len)) {
                logf("resource: bad EE dest %08x", dst);
                return ST_IO;
            }
        } else {
            std::vector<u8> msg(16 + len);
            put32(&msg[0], 0x10);
            put32(&msg[4], len);
            put32(&msg[8], allocOffset);
            put32(&msg[12], 0);
            memcpy(&msg[16], data.data(), len);
            ee_.post(SID_EE_STREAM, r.cookie, msg.data(), (u32)msg.size(), nullptr, 0, false);
        }
    } else if (dtype == 1) {
        adpcmFixup(data, r.flags);
        u32 padded = (len + 0x3f) & ~0x3fu; // IOP transfers len rounded up to 64
        data.resize(padded, 0);
        spu_.ramWrite(r.dest, data.data(), padded);
        if (delivery_ == Delivery::Rpc1301) {
            u32 hdr[4] = {0x10, len, 0, 0};
            ee_.post(SID_EE_STREAM, r.cookie, hdr, 16, nullptr, 0, false);
        }
    } else if (dtype == 2) {
        // IOP-internal buffer; no EE-visible data. TODO: never seen in traced paths.
        logf("resource: dest type 2 (IOP buffer) requested, cookie %08x - no-op", r.cookie);
    } else {
        logf("resource: unknown dest type %u (flags %04x)", dtype, r.flags);
        return ST_IO;
    }
    if (verbose())
        logf("resource: %s %04x/%u/%d +%u len %u -> type%u dest %08x", f.node.key.c_str(), r.type, r.id, (s16)r.sub,
             r.offset, len, dtype, r.dest);
    return ST_OK;
}

void ResourceService::handle(u32 handle, const void* send, u32 sendSize) {
    FileService::Open f;
    bool ok = files_.get((s32)handle, f);
    if (!ok)
        logf("resource: request for bad file handle %u", handle);
    u32 count = sendSize / sizeof(ResourceRequestWire);
    if (sendSize % sizeof(ResourceRequestWire))
        logf("resource: odd payload size %u", sendSize);
    const u8* in = (const u8*)send;
    std::vector<std::pair<u32, s32>> done;
    for (u32 i = 0; i < count; ++i) {
        ResourceRequestWire r;
        memcpy(&r, in + i * sizeof r, sizeof r);
        bool via1301 = false;
        s32 st = ok ? process(f, r) : ST_BAD_HANDLE;
        (void)via1301;
        // In Rpc1301 mode, successful requests were already completed by their 0x1301 call.
        if (st != ST_OK && skipFailureCompletions)
            continue;
        if (!(delivery_ == Delivery::Rpc1301 && st == ST_OK && (r.flags & 0xf) != 2))
            done.emplace_back(r.cookie, st);
    }
    // Completion order == submission order, <= 64 per 0x1302 call (fno = count, payload = count x {cookie,status}).
    for (size_t i = 0; i < done.size(); i += MAX_BATCH) {
        u32 n = (u32)std::min<size_t>(MAX_BATCH, done.size() - i);
        std::vector<u8> p(n * 8);
        for (u32 k = 0; k < n; ++k) {
            put32(&p[k * 8], done[i + k].first);
            put32(&p[k * 8 + 4], (u32)done[i + k].second);
        }
        ee_.post(SID_EE_DONE, n, p.data(), (u32)p.size(), nullptr, 0, true);
    }
}

} // namespace hle
