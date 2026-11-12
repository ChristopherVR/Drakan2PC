// Object-motion scope notes inside one draw batch (motion_segments.h): the segments and note applications must come
// out in the order the separate draws and notes had, whatever the batching.
#include "motion_segments.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
static void expectEq(const std::string& got, const std::string& want, const char* what) {
    const bool ok = got == want;
    std::printf("  [%s] %s: %s\n", ok ? "ok" : "FAIL", what, got.c_str());
    if (!ok) {
        ++g_fail;
        std::printf("         want %s\n", want.c_str());
    }
}

// "n<object id>" for a note, "s<first>+<count>" for a segment
static std::string walk(const std::vector<gsr::motion::Note>& notes, uint32_t count) {
    std::string out;
    gsr::motion::forEachSegment(
        notes.data(), (uint32_t)notes.size(), count,
        [&](const gsr::ObjectMotionScope& scope) { out += "n" + std::to_string(scope.objectId) + " "; },
        [&](uint32_t first, uint32_t n) { out += "s" + std::to_string(first) + "+" + std::to_string(n) + " "; });
    return out;
}

static gsr::motion::Note note(uint32_t vertex, uint64_t object) {
    gsr::motion::Note n;
    n.vertex = vertex;
    n.scope.objectId = object;
    return n;
}

int main() {
    expectEq(walk({}, 12), "s0+12 ", "no notes: the whole batch is one segment");
    expectEq(walk({}, 0), "", "empty batch");
    // object 1 ends after 6 vertices, object 2 begins there, ends after 18
    expectEq(walk({note(6, 0), note(6, 2), note(18, 0)}, 18), "s0+6 n0 n2 s6+12 n0 ",
             "End and Begin at the same vertex leave no empty segment between them");
    expectEq(walk({note(6, 0), note(6, 2)}, 30), "s0+6 n0 n2 s6+24 ", "vertices after the last note");
    expectEq(walk({note(0, 5)}, 6), "n5 s0+6 ", "a note before the first vertex applies first");
    expectEq(walk({note(6, 1), note(12, 2), note(18, 3)}, 24), "s0+6 n1 s6+6 n2 s12+6 n3 s18+6 ",
             "three notes, four segments");
    expectEq(walk({note(40, 9)}, 24), "s0+24 n9 ", "a note past the batch still applies, after the vertices");
    expectEq(walk({note(6, 1), note(6, 2), note(6, 3)}, 6), "s0+6 n1 n2 n3 ", "notes at the end keep their order");

    gsr::motion::Deferred deferred;
    deferred.add(6, {});
    deferred.add(12, {});
    const bool held = !deferred.empty() && deferred.size() == 2 && deferred.data()[1].vertex == 12;
    deferred.clear();
    if (!held || !deferred.empty()) {
        ++g_fail;
        std::printf("  [FAIL] deferred note list\n");
    }
    std::printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
