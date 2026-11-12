#include "hle/ResourceService.h"
#include "hle/ResourceOverride.h"
#include <algorithm>
#include <chrono>
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

// What prepare() found for one request: the payload ready to deliver, or the status that ends the request.
struct ResourceService::Prepared {
    ResourceRequestWire r{};
    s32 status = ST_OK; // not ST_OK: nothing to deliver, the status is reported
    std::vector<u8> data;
    u32 len = 0;   // payload bytes (data holds more for an SPU2 destination: padded to 64)
    u32 dtype = 0; // destination type, flags & 0xf
};

// Everything of a request that needs neither the EE nor the SPU2: index lookup, redirect, override layer, the read from
// the disc and the ADPCM fix-ups.
void ResourceService::prepare(const FileService::Open& f, const ResourceRequestWire& r, Prepared& out) {
    out.r = r;
    out.dtype = r.flags & 0xf;
    auto fail = [&](s32 status) { out.status = status; };
    if (!f.srsc.valid) {
        logf("resource: file %s has no SRSC index", f.node.key.c_str());
        return fail(ST_NOT_FOUND);
    }
    u16 id = r.id;
    const SrscEntry* e = nullptr;
    if (redirect_ && (id = redirect_(f.node.key, r.type, r.id)) != r.id) {
        e = f.srsc.find(r.type, id, r.sub);
        if (!e)
            e = f.srsc.find(r.type, id, 0xFFFF);
        if (e && r.length != 0xFFFFFFFFu && (u64)r.offset + r.length > e->size)
            e = nullptr; // the target cannot serve this request (a shorter track): the original entry does
        if (!e) {
            logf("resource: %s: redirect %04x/%u -> %u not found (+%u len %u), serving the original",
                 f.node.key.c_str(), r.type, r.id, id, (unsigned)r.offset, (unsigned)r.length);
            id = r.id;
        }
    }
    if (!e)
        e = f.srsc.find(r.type, r.id, r.sub);
    const bool duck = e && duck_ && duck_(f.node.key, r.type, id, r.offset);
    if (!e) {
        logf("resource: %s: %04x/%u/%d not found", f.node.key.c_str(), r.type, r.id, (s16)r.sub);
        return fail(ST_NOT_FOUND);
    }
    if (e->size == 0) {
        logf("resource: %04x/%u has size ZERO", r.type, r.id);
        return fail(ST_ZERO);
    }

    if (observer_) {
        ResourceId oid;
        oid.path = f.node.key;
        size_t osl = oid.path.find_last_of("\\/");
        oid.file = osl == std::string::npos ? oid.path : oid.path.substr(osl + 1);
        oid.type = r.type;
        oid.id = id;
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
        rid.id = id;
        rid.sub = e->sub;
        for (auto& o : overrides_) {
            if (!o->wants(rid))
                continue;
            if (!overridden) {
                full.resize(e->size);
                if (files_.fs().read(f.node, (u64)e->off, full.data(), e->size) != e->size) {
                    logf("resource: short read in %s", f.node.key.c_str());
                    return fail(ST_IO);
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
        return fail(ST_ZERO);
    }
    if (r.offset >= esize) {
        logf("resource: offset %u beyond entry size %u", r.offset, esize);
        return fail(ST_NOT_FOUND);
    }
    u32 avail = esize - r.offset;
    // TODO: length==0 semantics unverified; treated as "whole remainder".
    u32 len = r.length == 0 ? avail : std::min(r.length, avail);
    if (len > MAX_LEN) {
        logf("resource: len %u > 0x%x", len, MAX_LEN);
        return fail(ST_TOO_BIG);
    }

    statRequests_.fetch_add(1, std::memory_order_relaxed);
    statBytes_.fetch_add(len, std::memory_order_relaxed);
    if (overridden)
        statOverridden_.fetch_add(1, std::memory_order_relaxed);
    std::vector<u8>& data = out.data;
    data.resize(len);
    if (overridden)
        memcpy(data.data(), full.data() + r.offset, len);
    else if (files_.fs().read(f.node, (u64)e->off + r.offset, data.data(), len) != len) {
        logf("resource: short read in %s", f.node.key.c_str());
        return fail(ST_IO);
    }
    out.len = len;
    if (out.dtype == 1) {
        adpcmFixup(data, r.flags);
        if (duck)
            CampaignMusicDucker::silenceAdpcm(data);
        data.resize((len + 0x3f) & ~0x3fu, 0); // IOP transfers len rounded up to 64
    }
}

// The EE side of a batch: per request the calls of the IOP loader (buffer allocation 0x1303, delivery 0x1301) with
// the writes into EE and SPU2 memory between them, then the completions (0x1302). Written as a call sequence so the
// host can run a whole batch on the EE side without a thread hand-over per call (IEeRpcPoster::run); the calls, their
// payloads and their order are those of the loader thread either way.
class ResourceService::BatchCalls final : public IEeCallSequence {
public:
    BatchCalls(ResourceService& s, const FileService::Open& f, std::vector<Prepared>& items)
        : s_(s), f_(f), items_(items) {}

    bool next(EeCall& call) override {
        while (at_ < items_.size()) {
            Prepared& p = items_[at_];
            const ResourceRequestWire& r = p.r;
            if (step_ == Step::Start) {
                if (p.status != ST_OK) {
                    finish(p.status);
                    continue;
                }
                dst_ = r.dest;
                allocOffset_ = 0;
                if (p.dtype == 0) {
                    if (r.flags & 0x80) {
                        // EE allocates the destination: IOP -> EE 0x1303 (blocking), fno = request ptr (cookie),
                        // send/recv 16 bytes; send word0 = len rounded up to 4; reply word1 = EE address (0 = fail).
                        const u32 q[4] = {(p.len + 3) & ~3u, 0, 0, 0};
                        set(call, SID_EE_ALLOC, r.cookie, q, 16, 16, true);
                        step_ = Step::Allocated;
                        return true;
                    }
                    step_ = Step::Deliver;
                } else if (p.dtype == 1) {
                    s_.spu_.ramWrite(r.dest, p.data.data(), (u32)p.data.size());
                    if (s_.delivery_ == Delivery::Rpc1301) {
                        const u32 hdr[4] = {0x10, p.len, 0, 0};
                        set(call, SID_EE_STREAM, r.cookie, hdr, 16, 0, false);
                        step_ = Step::Last;
                        return true;
                    }
                    finish(ST_OK);
                    continue;
                } else if (p.dtype == 2) {
                    // IOP-internal buffer; no EE-visible data. TODO: never seen in traced paths.
                    logf("resource: dest type 2 (IOP buffer) requested, cookie %08x - no-op", r.cookie);
                    finish(ST_OK);
                    continue;
                } else {
                    logf("resource: unknown dest type %u (flags %04x)", p.dtype, r.flags);
                    finish(ST_IO);
                    continue;
                }
            }
            if (step_ == Step::Deliver) {
                if (s_.delivery_ == Delivery::Rpc1301 && (r.flags & 0x80) && dst_ != 0) {
                    // Alloc flag (measured with the movie player): the data is DMA'd straight into the allocated
                    // buffer, the 0x1301 call only carries the header and completes the request; the requester's slot
                    // pointer is (re)written after the completion ran.
                    if (!s_.mem_.write(dst_, p.data.data(), p.len)) {
                        logf("resource: bad EE dest %08x", dst_);
                        finish(ST_IO);
                        continue;
                    }
                    // header word 0 = offset from the 0x1301 receive buffer to the data: the EE handler (0x1f0110)
                    // computes data = buf + word0. Measured/disassembled (RSRCIOP 0x35a0-0x35f4: s3[0] = reply word 2
                    // of the 0x1303 call). Inline data uses 0x10.
                    const u32 hdr[4] = {allocOffset_, p.len, allocOffset_, 0};
                    set(call, SID_EE_STREAM, r.cookie, hdr, 16, 0, false);
                    step_ = Step::Rewrite;
                    return true;
                }
                if (s_.delivery_ == Delivery::Direct &&
                    dst_ != 0) { // dst == 0: the data can only reach the request object through 0x1301
                    if (!s_.mem_.write(dst_, p.data.data(), p.len)) {
                        logf("resource: bad EE dest %08x", dst_);
                        finish(ST_IO);
                    } else
                        finish(ST_OK);
                    continue;
                }
                call.sid = SID_EE_STREAM;
                call.fno = r.cookie;
                call.replyCap = 0;
                call.blocking = false;
                call.payload.resize(16 + p.len);
                put32(&call.payload[0], 0x10);
                put32(&call.payload[4], p.len);
                put32(&call.payload[8], allocOffset_);
                put32(&call.payload[12], 0);
                memcpy(&call.payload[16], p.data.data(), p.len);
                step_ = Step::Last;
                return true;
            }
            return false; // not reached: reply() moves every waiting step on
        }
        // Completion order == submission order, <= 64 per 0x1302 call (fno = count, payload = count x
        // {cookie,status}).
        if (doneAt_ < done_.size()) {
            const u32 n = (u32)std::min<size_t>(MAX_BATCH, done_.size() - doneAt_);
            call.sid = SID_EE_DONE;
            call.fno = n;
            call.replyCap = 0;
            call.blocking = true;
            call.payload.resize(n * 8);
            for (u32 k = 0; k < n; ++k) {
                put32(&call.payload[k * 8], done_[doneAt_ + k].first);
                put32(&call.payload[k * 8 + 4], (u32)done_[doneAt_ + k].second);
            }
            doneAt_ += n;
            return true;
        }
        return false;
    }

    void reply(int result, const void* reply, u32 size) override {
        if (at_ >= items_.size())
            return; // a 0x1302 completion call: no reply is used
        Prepared& p = items_[at_];
        const ResourceRequestWire& r = p.r;
        if (step_ == Step::Allocated) {
            u32 a[4] = {};
            if (reply && size)
                memcpy(a, reply, std::min<u32>(size, 16));
            if (result < 8 || a[1] == 0) {
                logf("resource: EE alloc (0x1303) failed for cookie %08x", r.cookie);
                finish(ST_ALLOC);
                return;
            }
            dst_ = a[1];
            allocOffset_ = a[2]; // reply word 2 = allocation offset, echoed in the 0x1301 header (word 2)
            // The request's dest field is where the allocated EE address is stored for the requester (the player reads
            // it back, e.g. as the DMA source of a movie frame slot). Measured: dest = slot descriptor with alloc flag
            // 0x80.
            if (r.dest != 0)
                s_.mem_.write(r.dest, &dst_, 4);
            if (verbose())
                logf("resource: alloc ptr %08x (offset %08x) stored at dest %08x", dst_, allocOffset_, r.dest);
            // TODO: reply word2 is stored by the IOP in its per-request header (offset field of the 0x1301 header);
            // unknown use.
            step_ = Step::Deliver;
        } else if (step_ == Step::Rewrite) {
            if (r.dest != 0)
                s_.mem_.write(r.dest, &dst_, 4);
            finish(ST_OK);
        } else if (step_ == Step::Last) {
            finish(ST_OK);
        }
    }

private:
    enum class Step {
        Start,
        Allocated, // waiting for the 0x1303 reply
        Deliver,   // EE destination known
        Rewrite,   // waiting for the 0x1301 call of an allocated request; the slot pointer is written again after it
        Last,      // waiting for the request's last call
    };
    static void set(EeCall& call, u32 sid, u32 fno, const u32* words, u32 bytes, u32 replyCap, bool blocking) {
        call.sid = sid;
        call.fno = fno;
        call.replyCap = replyCap;
        call.blocking = blocking;
        call.payload.resize(bytes);
        memcpy(call.payload.data(), words, bytes);
    }
    // The request at_ is over with `st`.
    void finish(s32 st) {
        const Prepared& p = items_[at_];
        const ResourceRequestWire& r = p.r;
        if (st == ST_OK && verbose())
            logf("resource: %s %04x/%u/%d +%u len %u -> type%u dest %08x", f_.node.key.c_str(), r.type, r.id,
                 (s16)r.sub, r.offset, p.len, p.dtype, r.dest);
        // In Rpc1301 mode, successful requests were already completed by their 0x1301 call.
        if (!(st != ST_OK && s_.skipFailureCompletions) &&
            !(s_.delivery_ == Delivery::Rpc1301 && st == ST_OK && (r.flags & 0xf) != 2))
            done_.emplace_back(r.cookie, st);
        ++at_;
        step_ = Step::Start;
    }
    ResourceService& s_;
    const FileService::Open& f_;
    std::vector<Prepared>& items_;
    size_t at_ = 0;
    Step step_ = Step::Start;
    u32 dst_ = 0, allocOffset_ = 0;
    std::vector<std::pair<u32, s32>> done_;
    size_t doneAt_ = 0;
};

void ResourceService::handle(u32 handle, const void* send, u32 sendSize) {
    FileService::Open f;
    bool ok = files_.get((s32)handle, f);
    if (!ok)
        logf("resource: request for bad file handle %u", handle);
    u32 count = sendSize / sizeof(ResourceRequestWire);
    if (sendSize % sizeof(ResourceRequestWire))
        logf("resource: odd payload size %u", sendSize);
    const u8* in = (const u8*)send;
    // The disc work of the whole batch first, then its EE side as one sequence.
    std::vector<Prepared> items(count);
    for (u32 i = 0; i < count; ++i) {
        ResourceRequestWire r;
        memcpy(&r, in + i * sizeof r, sizeof r);
        if (ok)
            prepare(f, r, items[i]);
        else {
            items[i].r = r;
            items[i].dtype = r.flags & 0xf;
            items[i].status = ST_BAD_HANDLE;
        }
    }
    BatchCalls sequence(*this, f, items);
    const auto start = std::chrono::steady_clock::now();
    ee_.run(sequence);
    statEeNanos_.fetch_add(
        (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count(),
        std::memory_order_relaxed);
}

} // namespace hle
