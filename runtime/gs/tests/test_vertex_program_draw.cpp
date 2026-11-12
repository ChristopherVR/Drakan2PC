// The backend-neutral rules of vertex-program draws (src/vertex_program_draw.h): which records take the path and
// why not, the stand-in vertices, the per-record motion tracking of a batch with its scope notes, and the history
// value against motion::History::track itself.
#include "vertex_program_draw.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace gsr;

namespace {
int g_checks = 0, g_failed = 0;
void expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("  [FAIL] %s\n", what);
    }
}

ObjectMotionScope scopeOf(uint64_t object, uint32_t flags = ObjectMotionScope::Begin | ObjectMotionScope::Valid |
                                                            ObjectMotionScope::TopologyStable) {
    ObjectMotionScope s;
    s.objectId = object;
    s.meshId = 7;
    s.partId = 1;
    s.flags = flags;
    s.topologyHash = 99;
    return s;
}

// A batch of `records` records of `perRecord` vertices; `seed` goes into the positions and the constants.
struct Batch {
    std::vector<vp::Constants> constants;
    std::vector<VertexProgramVertex> vertices;
    std::vector<VertexProgramRecord> records;
    std::vector<motion::Note> notes;
    VertexProgramBatch view() const {
        VertexProgramBatch b;
        b.constants = constants.data();
        b.constantCount = (uint32_t)constants.size();
        b.vertices = vertices.data();
        b.vertexCount = (uint32_t)vertices.size();
        b.records = records.data();
        b.recordCount = (uint32_t)records.size();
        b.notes = notes.data();
        b.noteCount = (uint32_t)notes.size();
        return b;
    }
    void add(uint32_t perRecord, uint32_t seed, uint32_t texture = 5) {
        VertexProgramRecord r;
        r.firstVertex = (uint32_t)vertices.size();
        r.vertexCount = perRecord;
        r.constants = (uint32_t)constants.size();
        r.program = vp::StripsLit;
        r.shape = 1234;
        vp::Constants k{};
        k.mvp[0][0] = seed;
        constants.push_back(k);
        for (uint32_t i = 0; i < perRecord; ++i) {
            VertexProgramVertex v{};
            v.in.pos[0] = seed * 1000u + i;
            v.in.st[0] = texture + i;
            v.constants = r.constants;
            v.program = r.program;
            vertices.push_back(v);
        }
        records.push_back(r);
    }
};

struct Tracker {
    motion::PairedHistory history{64ull * 1024 * 1024, true};
    ObjectMotionScope scope;
    std::vector<motion::ProgramPrevious> previous;
    std::vector<vp::Constants> previousConstants;
    uint32_t applied = 0;
    program_draw::MotionCounts track(const Batch& b, const GsDrawState& s) {
        const VertexProgramBatch view = b.view();
        return program_draw::trackBatch(
            history, view, s, view.constantCount, previous, previousConstants,
            [&](const ObjectMotionScope& next) {
                ++applied;
                scope = (next.flags & ObjectMotionScope::End) ? ObjectMotionScope{} : next;
            },
            [&]() -> const ObjectMotionScope& { return scope; });
    }
};

void testDecisions() {
    using program_draw::BackendState;
    vpath::Fallback why = vpath::Fallback::Count;
    BackendState ready;
    ready.shaderReady = true;
    ready.worldOpen = true;
    expect(program_draw::backendTakes(ready, &why) && why == vpath::Fallback::Count, "a ready backend in the world");
    BackendState b = ready;
    b.shaderReady = false;
    expect(!program_draw::backendTakes(b, &why) && why == vpath::Fallback::NotReady, "compiling: not ready");
    b = ready;
    b.shaderFailed = true;
    b.rayEffects = true;
    expect(!program_draw::backendTakes(b, &why) && why == vpath::Fallback::Internal, "a failure comes first");
    b = ready;
    b.arithmetic = false;
    expect(!program_draw::backendTakes(b, &why) && why == vpath::Fallback::AdapterNoSupport, "no arithmetic");
    for (int f = 0; f < 4; ++f) {
        b = ready;
        (f == 0 ? b.rayEffects : f == 1 ? b.terrainDetail : f == 2 ? b.lodStatistic : b.vertexDiagnosis) = true;
        expect(!program_draw::backendTakes(b, &why) && why == vpath::Fallback::Feature, "a vertex-reading feature");
    }
    b = ready;
    b.worldOpen = false;
    expect(program_draw::backendTakes(b, &why), "outside the world without a HUD map (a replay)");
    b.hudMapped = true;
    expect(!program_draw::backendTakes(b, &why) && why == vpath::Fallback::DrawState, "outside the world, HUD mapped");
    b.worldOpen = true;
    expect(program_draw::backendTakes(b, &why), "a HUD map does not act inside the world");
    expect(!program_draw::backendTakes(BackendState{}, nullptr), "no reason asked for");

    const uint64_t frame640 = uint64_t(10) << 16, frame128 = uint64_t(2) << 16, frame512 = uint64_t(8) << 16;
    const uint32_t strip = 4 | 8; // triangle strip, IIP in PRIM (not read here)
    expect(program_draw::stateTakes(DF_IIP, frame640, vp::StripsLit, strip), "a lit Gouraud strip");
    expect(program_draw::stateTakes(0, frame640, vp::StripsFlat, strip), "a constant-colour strip needs no IIP");
    expect(!program_draw::stateTakes(0, frame640, vp::StripsLit, strip), "flat shaded lit strip");
    expect(!program_draw::stateTakes(DF_IIP | DF_FST, frame640, vp::StripsLit, strip), "UV coordinates");
    expect(!program_draw::stateTakes(DF_IIP, frame128, vp::StripsLit, strip), "a 128 wide scratch target");
    expect(program_draw::stateTakes(DF_IIP, frame512, vp::StripsLit, strip), "a 512 wide target");
    expect(!program_draw::stateTakes(DF_IIP, frame640, vp::StripsLit, 3), "a triangle list record");
    expect(!program_draw::stateTakes(DF_IIP, frame640, vp::StripsLit, 5), "a triangle fan record");

    GsDrawState st{};
    st.scissor = 16ull | (623ull << 16) | (8ull << 32) | (455ull << 48);
    DrawVertex v[3];
    program_draw::standIns(st, v);
    expect(v[0].x == 16 && v[0].y == 8 && v[1].x == 623 && v[1].y == 8 && v[2].x == 16 && v[2].y == 455,
           "stand-ins span the scissor");
    expect(v[0].q == 0.5f && v[1].q == 0.5f && v[2].q == 0.5f && v[0].fog == 0, "stand-ins are perspective, fog 0");
}

void testTracking() {
    GsDrawState s{};
    s.tex0 = 0x1234;
    {
        // two objects in one batch, the scope note of the second between the records
        Tracker t;
        t.scope = scopeOf(1);
        Batch world1;
        world1.add(5, 1);
        world1.add(7, 2);
        world1.notes.push_back({1, scopeOf(2)});
        t.history.beginFrame(10);
        auto c = t.track(world1, s);
        expect(c.records == 2 && c.matched == 0 && c.scoped == 2 && t.applied == 1, "first world: nothing matched");
        expect(t.previous.size() == 12 && t.previousConstants.empty(), "one zeroed entry per vertex");
        t.history.commitFrame();
        t.history.beginFrame(11);
        t.scope = scopeOf(1);
        Batch world2;
        world2.add(5, 3);
        world2.add(7, 4);
        world2.notes.push_back({1, scopeOf(2)});
        c = t.track(world2, s);
        expect(c.matched == 2 && c.matchedVertices == 12 && t.previousConstants.size() == 2, "second world: both");
        bool ok = true;
        for (uint32_t i = 0; i < 5; ++i)
            ok = ok && t.previous[i].valid == 1 && t.previous[i].constants == 2 && t.previous[i].pos[0] == 1000u + i;
        for (uint32_t i = 0; i < 7; ++i)
            ok = ok && t.previous[5 + i].valid == 1 && t.previous[5 + i].constants == 3 &&
                 t.previous[5 + i].pos[0] == 2000u + i;
        expect(ok, "previous positions and constants indices after the batch's own constants");
        expect(t.previousConstants[0].mvp[0][0] == 1 && t.previousConstants[1].mvp[0][0] == 2, "previous constants");
        expect(t.scope.objectId == 2, "the scope in force after the batch is the last note's");
    }
    {
        // two records under one scope: the ordinal tells them apart, in order
        Tracker t;
        t.history.beginFrame(10);
        t.scope = scopeOf(1);
        Batch a;
        a.add(4, 1);
        a.add(4, 2);
        t.track(a, s);
        t.history.commitFrame();
        t.history.beginFrame(11);
        Batch b;
        b.add(4, 3);
        b.add(4, 4);
        const auto c = t.track(b, s);
        expect(c.matched == 2 && t.previous[0].pos[0] == 1000u && t.previous[4].pos[0] == 2000u, "ordinals");
        // the same scope again in the same world, in a second batch: the ordinals go on
        Batch more;
        more.add(4, 5);
        const auto d = t.track(more, s);
        expect(d.matched == 0, "a third record under the key has no previous");
    }
    {
        // a note at the end, an End note, an unknown scope, a changed texture coordinate, a changed GS state
        Tracker t;
        t.history.beginFrame(10);
        t.scope = scopeOf(1);
        Batch a;
        a.add(4, 1);
        a.notes.push_back({1, scopeOf(0, ObjectMotionScope::End)});
        t.track(a, s);
        expect(t.applied == 1 && t.scope.objectId == 0, "a trailing End note is applied after the last record");
        t.history.commitFrame();
        t.history.beginFrame(11);
        t.scope = scopeOf(1);
        Batch moved;
        moved.add(4, 2, 6);
        expect(t.track(moved, s).matched == 0, "other texture coordinates: no match");
        t.history.commitFrame();
        t.history.beginFrame(12);
        t.scope = scopeOf(1);
        Batch again;
        again.add(4, 3, 6);
        GsDrawState other = s;
        other.alpha = 0x44;
        expect(t.track(again, other).matched == 0, "another GS state: no match");
        t.history.commitFrame();
        t.history.beginFrame(13);
        t.scope = scopeOf(1, ObjectMotionScope::Begin | ObjectMotionScope::Valid);
        Batch unknown;
        unknown.add(4, 4, 6);
        const auto c = t.track(unknown, other);
        expect(c.matched == 0 && c.scoped == 0 && c.records == 1, "a scope without stable topology is not tracked");
        t.history.reset();
        expect(t.history.programOrdinals.empty() && t.history.program.bytes() == 0, "reset clears both histories");
    }
    {
        // the paired history drives the program history with the calls of motion::History
        motion::PairedHistory h(64ull * 1024 * 1024, false);
        h.beginFrame(5);
        h.programOrdinals[motion::Key{1, 0, 2, 0, 0, 0}] = 3;
        h.beginFrame(5);
        expect(h.programOrdinals.size() == 1, "the same epoch keeps the ordinals");
        h.beginFrame(6);
        expect(h.programOrdinals.empty(), "a new world starts the ordinals again");
    }
    {
        Batch a;
        a.add(3, 1);
        a.notes.push_back({0, scopeOf(4)});
        a.notes.push_back({1, scopeOf(5)});
        uint32_t n = 0;
        uint64_t last = 0;
        program_draw::applyNotes(a.view(), [&](const ObjectMotionScope& scope) {
            ++n;
            last = scope.objectId;
        });
        expect(n == 2 && last == 5, "applyNotes applies every note in order");
    }
}

// historyOf against motion::History::track: one vertex per draw, so the per-draw rule of History and the per-vertex
// rule of the shader coincide.
void testHistoryValue() {
    std::mt19937 rng(20261112u);
    std::uniform_real_distribution<float> xy(-300.0f, 900.0f), far(-9000.0f, 9000.0f), rs(0.5f, 8.0f);
    std::uniform_int_distribution<uint32_t> depth(0, 0xFFFFFFu), any(0, 0xFFFFFFFFu), pick(0, 15);
    uint64_t compared = 0, differ = 0, valid = 0;
    for (uint32_t i = 0; i < 200000; ++i) {
        const uint32_t kind = pick(rng);
        motion::Vertex before{xy(rng), xy(rng), (float)depth(rng), 0.25f, 0.5f};
        motion::Vertex now{before.x + xy(rng) * 0.01f, before.y + xy(rng) * 0.01f, (float)depth(rng), 0.25f, 0.5f};
        if (kind == 0)
            now.z = 0;
        if (kind == 1)
            before.z = (float)(any(rng) >> 30); // 0..3: zero and extreme ratios
        if (kind == 2)
            now.x = far(rng);
        if (kind == 3)
            now.z = (float)any(rng);
        if (kind >= 8)
            now.z = before.z + (float)(int32_t)(any(rng) % 4097u) - 2048.0f; // the common case: nearly the same depth
        const float sx = rs(rng), sy = rs(rng);
        motion::History h(1 << 20, false);
        const motion::Key key{1, 0, 2, 0, 0, 0};
        std::vector<motion::Previous> out;
        h.beginFrame(1);
        h.track(key, 5, &before, 1, sx, sy, true, out);
        h.beginFrame(2);
        const bool stored = before.z > 0; // History keeps nothing of a draw with a vertex at depth 0
        h.track(key, 5, &now, 1, sx, sy, true, out);
        const program_draw::HistoryValue got =
            program_draw::historyOf(now.x, now.y, now.z, before.x, before.y, before.z, sx, sy, stored);
        ++compared;
        valid += got.valid != 0;
        if (std::memcmp(&got, &out[0], sizeof(got)) != 0)
            ++differ;
    }
    std::printf("historyOf against History::track: %llu vertex pairs (%llu valid), %llu differ\n",
                (unsigned long long)compared, (unsigned long long)valid, (unsigned long long)differ);
    expect(differ == 0, "historyOf is History::track for one vertex");
    expect(valid > compared / 3 && valid < compared, "both outcomes are exercised");
}
} // namespace

int main() {
    testDecisions();
    testTracking();
    testHistoryValue();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
