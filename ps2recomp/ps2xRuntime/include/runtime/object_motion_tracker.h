#pragma once
#include "runtime/gs/object_motion_scope.h"
#include "runtime/object_motion_job_fwd.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace drakan_object_motion {
using Scope = gsr::ObjectMotionScope;
struct Marker {
    uint32_t byteOffset = 0;
    Scope scope;
};
struct Job {
    uint32_t byteCount = 0;
    std::vector<Marker> markers;
};

// Pure identity/topology/provenance model. EE executor owns mutation; immutable
// Job copies cross to VIF worker. No guest pointers are dereferenced here.
class Tracker {
    struct Life {
        uint64_t signature = 0, generation = 0;
        uint32_t allocation = 0, bytes = 0;
        bool dead = true;
    };
    struct Topology {
        uint64_t frame = 0, hash = 0;
        uint32_t vertices = 0, indices = 0;
        bool duplicate = false;
    };
    struct Mark {
        uint32_t base = 0, end = 0;
        Scope scope;
    };
    std::map<uint32_t, Life> lives;
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint32_t>, Topology> topologies;
    std::map<uint32_t, Mark> tags;
    std::map<std::pair<uint64_t, uint32_t>, Scope> references;
    std::set<uint64_t> referenceScenes;
    uint64_t frame = 0, scene = 0, submission = 0;

public:
    uint64_t frameEpoch() const { return frame; }
    void nextFrame() {
        ++frame;
        tags.clear();
        references.clear();
        referenceScenes.clear();
    }
    void invalidateTags() { tags.clear(); }
    // Constructor/destructor hooks cover fixed-size pool slots, independently
    // of the allocator chunk containing the pool.
    void lifetimeBoundary(uint32_t address) { freed(address, 0x120); }
    void reset() {
        tags.clear();
        references.clear();
        referenceScenes.clear();
        topologies.clear();
        for (auto& x : lives)
            x.second.dead = true;
    }
    void freed(uint32_t base, uint32_t bytes) {
        const uint64_t end = (uint64_t)base + bytes;
        for (auto& x : lives)
            if (x.first == base || (bytes && x.first >= base && x.first < end))
                x.second.dead = true;
        // Invalidate certificates whose owning object/mesh/part is freed.
        // Index content is hashed on every use, independently of its address.
        for (auto it = topologies.begin(); it != topologies.end();) {
            auto inside = [&](uint64_t p) { return p == base || (bytes && p >= base && p < end); };
            if (inside(std::get<0>(it->first)) || inside(std::get<2>(it->first)) || inside(std::get<3>(it->first)))
                it = topologies.erase(it);
            else
                ++it;
        }
    }
    Scope object(uint32_t address, uint32_t allocation, uint32_t bytes, uint64_t signature, uint32_t mesh,
                 uint32_t pc) {
        Scope p;
        p.frameEpoch = frame;
        p.sceneSerial = ++scene;
        p.sourcePc = pc;
        if (!frame || !address || !allocation || !bytes || address < allocation ||
            (uint64_t)address >= (uint64_t)allocation + bytes || !signature || !mesh) {
            p.flags = Scope::Rejected | Scope::Reset;
            return p;
        }
        auto& l = lives[address];
        if (l.dead || l.signature != signature || l.allocation != allocation || l.bytes != bytes) {
            ++l.generation;
            l.signature = signature;
            l.allocation = allocation;
            l.bytes = bytes;
            l.dead = false;
            p.flags |= Scope::Reset;
        }
        p.objectId = address;
        p.objectAddress = address;
        p.generation = l.generation;
        p.meshId = mesh;
        p.meshAddress = mesh;
        p.flags |= Scope::Valid;
        return p;
    }
    Scope part(const Scope& owner, uint32_t address, uint32_t pass, uint64_t hash, uint32_t vertices, uint32_t indices,
               bool inputTopologyVerified) {
        Scope p = owner;
        p.partId = address;
        p.partAddress = address;
        p.passKey = pass;
        p.topologyHash = hash;
        p.vertexCount = vertices;
        p.indexCount = indices;
        p.flags &= ~Scope::TopologyStable;
        if (!(p.flags & Scope::Valid) || !address || !hash || !inputTopologyVerified) {
            p.flags |= Scope::Rejected;
            return p;
        }
        auto key = std::make_tuple(p.objectId, p.generation, p.meshId, p.partId, pass);
        auto& t = topologies[key];
        const bool duplicate = t.frame == frame;
        if (!duplicate && !t.duplicate && t.frame + 1 == frame && t.hash == hash && t.vertices == vertices &&
            t.indices == indices && !(p.flags & Scope::Reset))
            p.flags |= Scope::TopologyStable;
        else
            p.flags |= Scope::Reset;
        if (duplicate) {
            p.flags |= Scope::Ambiguous | Scope::Rejected;
            p.flags &= ~Scope::TopologyStable;
        }
        t = {frame, hash, vertices, indices, duplicate};
        return p;
    }
    void mark(uint32_t tag, uint32_t base, uint32_t capacity, const Scope& p) {
        if (!capacity || tag < base || (uint64_t)tag + 16 > (uint64_t)base + capacity)
            return;
        tags[tag] = {base, base + capacity, p};
    }
    void enableReferences(uint64_t serial) { referenceScenes.insert(serial); }
    void reference(uint32_t source, const Scope& p) {
        auto [it, inserted] = references.emplace(std::make_pair(p.sceneSerial, source), p);
        if (!inserted) {
            // Shared source arrays cannot distinguish two group descriptors.
            it->second.flags |= Scope::Ambiguous | Scope::Rejected | Scope::Reset;
            it->second.flags &= ~Scope::TopologyStable;
        }
    }
    Scope atReference(uint32_t source, uint64_t expectedScene) const {
        auto it = references.find({expectedScene, source});
        if (it != references.end())
            return it->second;
        Scope p;
        p.flags = Scope::Rejected | Scope::Reset;
        return p;
    }
    Scope atTag(uint32_t address, uint64_t expectedScene) const {
        Scope p;
        p.flags = Scope::Rejected | Scope::Reset;
        auto it = tags.upper_bound(address);
        if (it == tags.begin())
            return p;
        --it;
        const auto& m = it->second;
        if (address < m.base || address >= m.end || m.scope.sceneSerial != expectedScene)
            return p;
        return m.scope;
    }
    class Chain {
        Tracker* tracker;
        uint64_t expectedScene;
        Job result;
        bool referenceScope = false;

    public:
        Chain(Tracker& t, uint64_t scene) : tracker(&t), expectedScene(scene) {}
        void tag(uint32_t address, uint32_t offset, uint32_t refAddress = 0, uint32_t vifCode = 0) {
            if (!expectedScene)
                return;
            // 1edddc..1eddf0 emits REF + V3-32 UNPACK, unsigned/non-relative
            // destination 0x36. NUM varies; source is the actual group position
            // array. Following UV/color REF commands retain this group scope.
            const bool positions =
                tracker->referenceScenes.count(expectedScene) && (vifCode & 0xff00ffffu) == 0x78000036u;
            if (referenceScope && !positions)
                return;
            if (positions)
                referenceScope = true;
            Scope p =
                positions ? tracker->atReference(refAddress, expectedScene) : tracker->atTag(address, expectedScene);
            if (positions) {
                const uint32_t encoded = (vifCode >> 16u) & 255u, vertices = encoded ? encoded : 256u;
                if (p.vertexCount != vertices) {
                    p.flags |= Scope::Rejected | Scope::Reset;
                    p.flags &= ~Scope::TopologyStable;
                }
            }
            if (!result.markers.empty()) {
                const auto& q = result.markers.back().scope;
                if (q.objectId == p.objectId && q.generation == p.generation && q.partId == p.partId &&
                    q.passKey == p.passKey && q.flags == p.flags && q.sceneSerial == p.sceneSerial)
                    return;
            }
            result.markers.push_back({offset, p});
        }
        JobPtr finish(uint32_t bytes) {
            if (!bytes || !expectedScene)
                return {};
            result.byteCount = bytes;
            if (result.markers.empty() || result.markers.front().byteOffset != 0) {
                Scope rejected;
                rejected.flags = Scope::Rejected | Scope::Reset;
                result.markers.insert(result.markers.begin(), {0, rejected});
            }
            const auto serial = ++tracker->submission;
            for (auto& m : result.markers)
                m.scope.submissionSerial = serial;
            return std::make_shared<const Job>(std::move(result));
        }
    };
};

// Command cursor rejects a provenance boundary which falls inside VIF payload.
// Sink must flush the GS primitive batch before changing the identity key.
class Cursor {
    const Job* job = nullptr;
    size_t next = 0;
    Scope active;
    bool open = false;

public:
    using Sink = void (*)(const Scope&, void*);
    bool same(const Cursor& c) const {
        return job == c.job && next == c.next && open == c.open &&
               (!open || !std::memcmp(&active, &c.active, sizeof active));
    }
    bool tracking() const { return job != nullptr; }
    bool boundary(uint32_t offset) const {
        return job && next < job->markers.size() && job->markers[next].byteOffset <= offset;
    }
    void start(const Job* j) {
        job = j;
        next = 0;
        open = false;
        active = {};
    }
    void close(Sink sink, void* user) {
        if (open) {
            Scope p = active;
            p.flags = (p.flags & ~Scope::Begin) | Scope::End;
            if (sink)
                sink(p, user);
            open = false;
        }
    }
    void command(uint32_t offset, Sink sink, void* user) {
        if (!job)
            return;
        while (next < job->markers.size() && job->markers[next].byteOffset <= offset) {
            close(sink, user);
            const auto& marker = job->markers[next++];
            active = marker.scope;
            if (marker.byteOffset != offset) {
                active.flags |= Scope::Ambiguous | Scope::Rejected | Scope::Reset;
                active.flags &= ~(Scope::Valid | Scope::TopologyStable);
            }
            active.flags = (active.flags & ~Scope::End) | Scope::Begin;
            open = true;
            if (sink)
                sink(active, user);
        }
    }
    void clipping(bool possible, Sink sink, void* user) {
        if (!possible || !open || (active.flags & Scope::Clipped))
            return;
        Scope p = active;
        close(sink, user);
        active = p;
        active.flags =
            (active.flags & ~(Scope::TopologyStable | Scope::End)) | Scope::Clipped | Scope::Rejected | Scope::Begin;
        open = true;
        if (sink)
            sink(active, user);
    }
};
} // namespace drakan_object_motion
