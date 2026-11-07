// Tracker::freed (object_motion_tracker.h) uses ordered range lookups and a mesh/part address index instead of a scan
// of every life and topology per free. This differential test drives the tracker and a copy of the previous linear
// implementation with the same random object / part / free / pool-boundary / frame sequence and requires identical
// scopes (generation, Reset, TopologyStable, Ambiguous) after every step, plus a consistent address index.
#include "runtime/object_motion_tracker.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <tuple>

using drakan_object_motion::Scope;
using drakan_object_motion::Tracker;

namespace {
// The linear model the optimisation replaces (only the parts freed() interacts with).
class Reference {
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
    std::map<uint32_t, Life> lives;
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint32_t>, Topology> topologies;
    uint64_t frame = 0, scene = 0;

public:
    void nextFrame() { ++frame; }
    void freed(uint32_t base, uint32_t bytes) {
        const uint64_t end = (uint64_t)base + bytes;
        for (auto& x : lives)
            if (x.first == base || (bytes && x.first >= base && x.first < end))
                x.second.dead = true;
        for (auto it = topologies.begin(); it != topologies.end();) {
            auto inside = [&](uint64_t p) { return p == base || (bytes && p >= base && p < end); };
            if (inside(std::get<0>(it->first)) || inside(std::get<2>(it->first)) || inside(std::get<3>(it->first)))
                it = topologies.erase(it);
            else
                ++it;
        }
    }
    size_t topologyCount() const { return topologies.size(); }
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
               bool verified) {
        Scope p = owner;
        p.partId = address;
        p.partAddress = address;
        p.passKey = pass;
        p.topologyHash = hash;
        p.vertexCount = vertices;
        p.indexCount = indices;
        p.flags &= ~Scope::TopologyStable;
        if (!(p.flags & Scope::Valid) || !address || !hash || !verified) {
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
};

void same(const Scope& a, const Scope& b) {
    assert(a.flags == b.flags);
    assert(a.generation == b.generation);
    assert(a.objectId == b.objectId && a.meshId == b.meshId && a.partId == b.partId);
}
} // namespace

int main() {
    // Exact-address free (bytes 0) and range edges, deterministic.
    {
        Tracker t;
        Reference r;
        t.nextFrame();
        r.nextFrame();
        same(t.object(0x800010, 0x800000, 0x100, 7, 0x900000, 1), r.object(0x800010, 0x800000, 0x100, 7, 0x900000, 1));
        t.freed(0x80000f, 1); // ends just before the object: no effect
        r.freed(0x80000f, 1);
        same(t.object(0x800010, 0x800000, 0x100, 7, 0x900000, 1), r.object(0x800010, 0x800000, 0x100, 7, 0x900000, 1));
        t.freed(0x800010, 0); // exact address
        r.freed(0x800010, 0);
        const Scope a = t.object(0x800010, 0x800000, 0x100, 7, 0x900000, 1);
        same(a, r.object(0x800010, 0x800000, 0x100, 7, 0x900000, 1));
        assert((a.flags & Scope::Reset) && a.generation == 2);
    }

    // Random differential run over a small address space so that frees, objects, meshes and parts overlap often.
    std::mt19937 rng(12345);
    for (int run = 0; run < 40; ++run) {
        Tracker t;
        Reference r;
        t.nextFrame();
        r.nextFrame();
        auto addr = [&](uint32_t lo, uint32_t span) { return lo + (rng() % span) * 0x10u; };
        for (int step = 0; step < 4000; ++step) {
            const uint32_t op = rng() % 100;
            if (op < 45) {
                const uint32_t allocation = addr(0x800000, 64);
                const uint32_t bytes = 0x40 + (rng() % 4) * 0x40;
                const uint32_t address = allocation + (rng() % (bytes / 0x10)) * 0x10;
                const uint64_t signature = 1 + rng() % 3;
                const uint32_t mesh = addr(0x800000, 96); // meshes and parts share the object range on purpose
                const Scope a = t.object(address, allocation, bytes, signature, mesh, 0x1485c8);
                const Scope b = r.object(address, allocation, bytes, signature, mesh, 0x1485c8);
                same(a, b);
                const int parts = 1 + rng() % 3;
                for (int i = 0; i < parts; ++i) {
                    const uint32_t part = addr(0x800000, 96);
                    const uint32_t pass = rng() % 2;
                    const uint64_t hash = 1 + rng() % 2;
                    const uint32_t vertices = 3 + rng() % 2;
                    same(t.part(a, part, pass, hash, vertices, vertices, true),
                         r.part(b, part, pass, hash, vertices, vertices, true));
                }
            } else if (op < 65) {
                const uint32_t base = addr(0x7ffff0, 100);
                const uint32_t bytes = (rng() % 3 == 0) ? 0 : (rng() % 0x200);
                t.freed(base, bytes);
                r.freed(base, bytes);
            } else if (op < 80) {
                const uint32_t base = addr(0x7ffff0, 100);
                t.lifetimeBoundary(base);
                r.freed(base, 0x120);
            } else if (op < 98) {
                t.nextFrame();
                r.nextFrame();
            } else {
                // reset() keeps lives (marked dead) and drops every topology; the reference has no reset, so rebuild it
                // the same way: free everything.
                t.reset();
                r.freed(0, 0xffffffffu);
            }
            assert(t.topologyCount() == r.topologyCount());
            // Each topology has one index entry for its mesh and one for its part (one when they are equal).
            assert(t.topologyIndexCount() >= t.topologyCount() && t.topologyIndexCount() <= 2 * t.topologyCount());
        }
    }
    std::printf("object motion freed policy ok\n");
    return 0;
}
