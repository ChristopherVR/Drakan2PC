// The draw state cache (draw_state_cache.h) against a model device context, without a GPU. A random stream of draws
// (textures, render targets sampled as textures, several targets, a velocity pass with its own target and programs)
// and of other users of the context (which bind whatever they like after taking the context) is run twice, with the
// cache off and on. The model applies the device's rule for a resource bound both for reading and as a target (binding
// a target unbinds the texture; a texture of a bound target is not bound). Checked: at every draw call the bound state
// is the same both ways and is what the draw asked for, every user of the context finds the targets and textures as
// without the cache, and the cache leaves binds out.
#include "draw_state_cache.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what);
    }
}

using gsr::draw_state::Cache;
using gsr::draw_state::pair;
using gsr::draw_state::Slot;

// Resources: 1..8 plain textures, 100..103 render targets (as targets and as textures), 200 the motion target.
struct Context {
    uint64_t layout = 0, vb0 = 0, vb1 = 0, vs = 0, ps = 0, blend = 0, depth = 0, scissor = 0;
    uint64_t srv[2] = {0, 0};
    uint64_t rtv = 0;
    uint64_t calls = 0;
    void setTextures(uint64_t a, uint64_t b) {
        ++calls;
        srv[0] = (a && a == rtv) ? 0 : a; // a texture of a bound target is not bound
        srv[1] = (b && b == rtv) ? 0 : b;
    }
    void setTarget(uint64_t t) {
        ++calls;
        rtv = t;
        for (auto& s : srv) // binding a target unbinds it as a texture
            if (s && s == t)
                s = 0;
    }
    bool operator==(const Context& o) const {
        return layout == o.layout && vb0 == o.vb0 && vb1 == o.vb1 && vs == o.vs && ps == o.ps && blend == o.blend &&
               depth == o.depth && scissor == o.scissor && srv[0] == o.srv[0] && srv[1] == o.srv[1] && rtv == o.rtv;
    }
};

struct Draw {
    uint64_t target, texture, scissor, blend, depth, ps, vbOffset;
    bool samplesTarget, velocity;
};

struct Backend {
    Context c;
    Cache cache;
    std::vector<Context> atDraws; // the state at every draw call
    std::vector<Context> atForeign;
    explicit Backend(bool on) { cache.enable(on); }
    void release() {
        c.setTextures(0, 0);
        c.setTarget(0);
    }
    void foreign(std::mt19937& rng) {
        if (cache.enabled() && cache.take())
            release();
        atForeign.push_back(c);
        // another pass binds whatever it likes
        c.layout = rng() % 3;
        c.vs = rng() % 3;
        c.ps = 10 + rng() % 3;
        c.blend = rng() % 4;
        c.scissor = rng() % 5;
        c.setTextures(rng() % 2 ? 0 : 1 + rng() % 8, 0);
        c.setTarget(rng() % 2 ? 0 : 100 + rng() % 4);
        ++c.calls;
    }
    void draw(const Draw& d) {
        cache.beginDraw();
        if (d.samplesTarget && cache.enabled() && cache.take())
            release();
        if (cache.set(Slot::InputLayout, 1))
            c.layout = 1, ++c.calls;
        if (cache.set(Slot::VertexBuffer0, d.vbOffset))
            c.vb0 = d.vbOffset, ++c.calls;
        if (cache.set(Slot::VertexShader, 1))
            c.vs = 1, ++c.calls;
        if (cache.set(Slot::PixelShader, d.ps))
            c.ps = d.ps, ++c.calls;
        if (cache.set(Slot::Textures, pair(d.texture, 0)))
            c.setTextures(d.texture, 0);
        if (cache.set(Slot::Scissor, d.scissor))
            c.scissor = d.scissor, ++c.calls;
        if (cache.set(Slot::Targets, d.target))
            c.setTarget(d.target);
        if (cache.set(Slot::Blend, d.blend))
            c.blend = d.blend, ++c.calls;
        if (cache.set(Slot::Depth, d.depth))
            c.depth = d.depth, ++c.calls;
        atDraws.push_back(c);
        if (d.velocity) {
            if (!cache.enabled())
                c.setTarget(0);
            if (cache.set(Slot::Targets, 200))
                c.setTarget(200);
            if (cache.set(Slot::Blend, 99))
                c.blend = 99, ++c.calls;
            if (cache.set(Slot::InputLayout, 2))
                c.layout = 2, ++c.calls;
            if (cache.set(Slot::VertexBuffer1, d.vbOffset))
                c.vb1 = d.vbOffset, ++c.calls;
            if (cache.set(Slot::VertexShader, 2))
                c.vs = 2, ++c.calls;
            if (cache.set(Slot::PixelShader, 20))
                c.ps = 20, ++c.calls;
            atDraws.push_back(c);
        }
        if (cache.endDraw(d.samplesTarget)) {
            release();
            cache.released(pair(0, 0), 0);
        }
    }
};

void randomStream(unsigned seed, bool velocity) {
    std::mt19937 rng(seed);
    Backend off(false), on(true);
    std::mt19937 foreignOff(seed * 7 + 1), foreignOn(seed * 7 + 1);
    uint64_t offset = 0;
    unsigned draws = 0;
    for (int step = 0; step < 4000; ++step) {
        if (rng() % 23 == 0) {
            off.foreign(foreignOff);
            on.foreign(foreignOn);
            continue;
        }
        Draw d{};
        d.target = 100 + (rng() % 9 == 0 ? rng() % 4 : 0);
        d.samplesTarget = rng() % 13 == 0;
        d.texture = d.samplesTarget ? 100 + rng() % 4 : 1 + rng() % 8;
        if (d.samplesTarget && d.texture == d.target)
            d.texture = 0; // feedback reads a copy, never the target itself
        d.scissor = rng() % 17 == 0 ? 1 + rng() % 3 : 1;
        d.blend = 1 + rng() % 3;
        d.depth = 1 + rng() % 2;
        d.ps = 10 + rng() % 2;
        d.vbOffset = offset += 1 + rng() % 16;
        d.velocity = velocity && rng() % 5 != 0;
        off.draw(d);
        on.draw(d);
        ++draws;
        // what the draw asked for is what is bound at its call (whenever it is without the cache: another user of
        // the context may leave a target bound that the draw then cannot read, with or without the cache)
        const auto asked = [&](const Context& got) {
            return got.rtv == d.target && got.srv[0] == d.texture && got.ps == d.ps && got.blend == d.blend &&
                   got.vb0 == d.vbOffset && got.scissor == d.scissor && got.layout == 1 && got.vs == 1;
        };
        const size_t at = on.atDraws.size() - (d.velocity ? 2 : 1);
        if (asked(off.atDraws[at]))
            check(asked(on.atDraws[at]), "the bound state is the draw's");
    }
    check(off.atDraws.size() == on.atDraws.size(), "same number of draw calls");
    unsigned differ = 0;
    for (size_t i = 0; i < off.atDraws.size() && i < on.atDraws.size(); ++i)
        differ += !(off.atDraws[i] == on.atDraws[i]);
    check(differ == 0, "every draw call sees the same bound state with the cache on");
    unsigned unreleased = 0;
    for (size_t i = 0; i < on.atForeign.size() && i < off.atForeign.size(); ++i)
        unreleased += on.atForeign[i].rtv != off.atForeign[i].rtv ||
                      on.atForeign[i].srv[0] != off.atForeign[i].srv[0] ||
                      on.atForeign[i].srv[1] != off.atForeign[i].srv[1];
    check(unreleased == 0, "every other user of the context finds the targets and textures as without the cache");
    check(on.c.calls < off.c.calls, "the cache leaves binds out");
    const auto& n = on.cache.counters();
    std::printf("  seed %u%s: %u draws, calls %llu -> %llu, binds left out %llu, held %llu, released later %llu, "
                "%u differ\n",
                seed, velocity ? " (velocity passes)" : "", draws, (unsigned long long)off.c.calls,
                (unsigned long long)on.c.calls, (unsigned long long)n.skipped, (unsigned long long)n.holds,
                (unsigned long long)n.releases, differ);
}
} // namespace

int main() {
    std::printf("draw state cache\n");
    {
        Cache c; // off: every set is made
        check(c.set(Slot::Blend, 1) && c.set(Slot::Blend, 1), "off: always set");
        check(c.endDraw(false), "off: the release is never held");
    }
    {
        Cache c;
        c.enable(true);
        check(c.set(Slot::Blend, 1), "first set is made");
        check(!c.set(Slot::Blend, 1), "same value is left out");
        check(c.set(Slot::Blend, 2), "a new value is made");
        c.forget();
        check(c.set(Slot::Blend, 2), "after forget the value is set again");
        check(!c.endDraw(false), "a plain draw holds its release");
        check(c.holding() && c.take() && !c.holding(), "the next user carries the release out");
        check(!c.take(), "only once");
        check(c.endDraw(true), "a draw that sampled a target releases at once");
        check(!c.holding(), "and holds nothing");
    }
    for (unsigned seed = 1; seed <= 6; ++seed)
        randomStream(seed, seed % 2 == 0);
    std::printf(g_fail ? "FAILED: %d\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
