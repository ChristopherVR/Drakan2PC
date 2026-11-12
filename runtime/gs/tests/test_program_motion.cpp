// Previous-frame inputs of vertex-program draws (src/program_motion.h): the rules of motion::History applied to
// records.
#include "program_motion.h"

#include <cstdio>
#include <vector>

namespace {
int g_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

using gsr::motion::Key;
using gsr::motion::ProgramHistory;
using gsr::motion::ProgramPrevious;
namespace vp = gsr::vp;

struct Record {
    vp::Header header{vp::StripsLit, 0x5C, 2, 7};
    uint32_t strips[2] = {4, 3};
    vp::Constants constants{};
    vp::Vertex vertices[7]{};
    explicit Record(uint32_t seed) {
        constants.mvp[0][0] = seed;
        for (uint32_t i = 0; i < 7; ++i) {
            vertices[i].pos[0] = seed * 16u + i;
            vertices[i].pos[3] = 0x3F800000u;
            vertices[i].st[0] = 100u + i; // the shape of the object: the same in every world
            vertices[i].st[1] = 200u + i;
        }
    }
    uint64_t shape() const { return ProgramHistory::shapeOf(header, strips, vertices); }
};

struct Tracked {
    bool matched = false;
    ProgramPrevious out[7];
    const vp::Constants* previous = nullptr;
};
Tracked track(ProgramHistory& h, const Key& key, const Record& r, bool known = true, uint32_t index = 5) {
    Tracked t;
    t.matched = h.track(key, r.shape(), r.constants, r.vertices, 7, known, index, t.out, &t.previous);
    return t;
}
bool allInvalid(const Tracked& t) {
    for (const ProgramPrevious& p : t.out)
        if (p.valid || p.constants || p.pos[0])
            return false;
    return !t.previous;
}
} // namespace

int main() {
    const Key key{0x1000, 1, 0x2000, 0, 0, 0};
    {
        // first world: nothing to match; second world: the first world's constants and positions
        ProgramHistory h;
        h.beginFrame(10);
        const Record a(1), b(2);
        expect(!track(h, key, a).matched, "no history in the first world");
        h.beginFrame(11);
        const Tracked t = track(h, key, b);
        expect(t.matched && t.previous && t.previous->mvp[0][0] == 1u, "the previous constants");
        bool ok = true;
        for (uint32_t i = 0; i < 7; ++i)
            ok = ok && t.out[i].valid == 1u && t.out[i].constants == 5u && t.out[i].pos[0] == 16u + i &&
                 t.out[i].pos[3] == 0x3F800000u;
        expect(ok, "the previous positions, with the caller's constants index");
        h.beginFrame(12);
        const Tracked u = track(h, key, a);
        expect(u.matched && u.previous->mvp[0][0] == 2u && u.out[3].pos[0] == 32u + 3u, "the world after");
        expect(h.bytes() > 0, "bytes are counted");
    }
    {
        // an object that was not drawn in the previous world has no history, and a skipped epoch resets
        ProgramHistory h;
        h.beginFrame(10);
        track(h, key, Record(1));
        h.beginFrame(11);
        h.beginFrame(12);
        expect(allInvalid(track(h, key, Record(2))), "a world without the object in between");
        ProgramHistory g;
        g.beginFrame(10);
        track(g, key, Record(1));
        g.beginFrame(13);
        expect(allInvalid(track(g, key, Record(2))), "a jump in the epoch drops the history");
        ProgramHistory c;
        c.beginFrame(10);
        track(c, key, Record(1));
        c.beginFrame(11, true);
        expect(allInvalid(track(c, key, Record(2))), "a camera cut drops the history");
    }
    {
        // the same key twice in one world is ambiguous: neither is matched, and nothing is kept for the next world
        ProgramHistory h;
        h.beginFrame(10);
        track(h, key, Record(1));
        h.beginFrame(11);
        expect(track(h, key, Record(2)).matched, "the first use matches");
        expect(allInvalid(track(h, key, Record(3))), "the second use of the key does not");
        expect(allInvalid(track(h, key, Record(4))), "nor the third");
        h.beginFrame(12);
        expect(allInvalid(track(h, key, Record(5))), "an ambiguous key leaves no history");
    }
    {
        // a changed shape (texture coordinates, strip lengths, program) or an unknown scope is never matched
        ProgramHistory h;
        h.beginFrame(10);
        track(h, key, Record(1));
        h.beginFrame(11);
        Record moved(2);
        moved.vertices[4].st[0] ^= 1u;
        expect(allInvalid(track(h, key, moved)), "another texture coordinate");
        h.beginFrame(12);
        Record strips(3);
        strips.strips[0] = 3;
        strips.strips[1] = 4;
        expect(allInvalid(track(h, key, strips)), "other strip lengths (and the changed record before it)");
        ProgramHistory g;
        g.beginFrame(10);
        track(g, key, Record(1));
        g.beginFrame(11);
        expect(allInvalid(track(g, key, Record(2), false)), "a scope that does not certify the object");
        g.beginFrame(12);
        expect(allInvalid(track(g, key, Record(3))), "and it stored nothing");
        Key noObject = key;
        noObject.object = 0;
        expect(allInvalid(track(g, noObject, Record(3))), "no object id");
    }
    {
        // manual reference: the previous world only advances when the temporal consumer accepted one
        ProgramHistory h(64ull * 1024 * 1024, true);
        h.beginFrame(10);
        track(h, key, Record(1));
        h.commitFrame();
        h.beginFrame(11);
        expect(track(h, key, Record(2)).previous->mvp[0][0] == 1u, "against the committed world");
        h.beginFrame(12); // world 11 was not committed
        const Tracked t = track(h, key, Record(3));
        expect(t.matched && t.previous->mvp[0][0] == 1u, "a rejected world never becomes the previous one");
        h.commitFrame();
        h.beginFrame(13);
        expect(track(h, key, Record(4)).previous->mvp[0][0] == 3u, "after the commit");
    }
    {
        // the budget: an entry that does not fit is not stored, so the next world has no history for it
        ProgramHistory h(sizeof(vp::Constants) + 7 * 16, false);
        const Key other{0x1001, 1, 0x2000, 0, 0, 0};
        h.beginFrame(10);
        track(h, key, Record(1));
        track(h, other, Record(1));
        h.beginFrame(11);
        expect(track(h, key, Record(2)).matched, "the entry inside the budget");
        expect(allInvalid(track(h, other, Record(2))), "the entry beyond the budget");
    }
    {
        // different ordinals of one scope are different entries
        ProgramHistory h;
        Key second = key;
        second.batch = 1;
        h.beginFrame(10);
        track(h, key, Record(1));
        track(h, second, Record(7));
        h.beginFrame(11);
        expect(track(h, key, Record(2)).previous->mvp[0][0] == 1u, "ordinal 0");
        expect(track(h, second, Record(8)).previous->mvp[0][0] == 7u, "ordinal 1");
    }
    if (g_failures)
        std::printf("%d failure(s)\n", g_failures);
    else
        std::printf("program_motion: ok\n");
    return g_failures ? 1 : 0;
}
