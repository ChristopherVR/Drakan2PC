#include "motion_history.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace gsr::motion;
int main() {
    unsigned checks = 0, failures = 0;
    auto check = [&](bool value, const char* name) {
        ++checks;
        if (!value) {
            ++failures;
            std::printf("FAIL %s\n", name);
        }
    };
    History h;
    Key key{1, 7, 3, 0, 0};
    std::vector<Previous> result;
    Vertex old[] = {{10, 20, 100, 0, 0}, {30, 20, 100, 1, 0}, {10, 40, 100, 0, 1}};
    h.beginFrame(1);
    check(!h.track(key, 5, old, 3, 2, 2, true, result).matched, "new object has no history");
    Vertex moved[] = {{13, 18, 200, 0, 0}, {33, 18, 100, 1, 0}, {13, 38, 50, 0, 1}};
    h.beginFrame(2);
    check(h.track(key, 5, moved, 3, 2, 2, true, result).matched, "moving and deforming vertices match");
    check(result[0].x / result[0].ratio == 20 && result[0].y / result[0].ratio == 40,
          "previous projected position recovered");
    check(result[0].ratio == 2 && result[2].ratio == 0.5f, "perspective depth ratios retained");
    const float x = (result[0].x + result[1].x + result[2].x) / (result[0].ratio + result[1].ratio + result[2].ratio);
    check(std::abs(x - (40 + 60 + 10) / 3.5f) < 1e-5f, "perspective-correct interpolated previous position");
    check(!h.track(key, 5, moved, 3, 2, 2, true, result).matched, "duplicate identity rejected");
    h.beginFrame(3);
    check(!h.track(key, 5, moved, 3, 2, 2, true, result).matched, "ambiguous previous frame discarded");
    h.beginFrame(4);
    auto clipped = moved[0];
    moved[0].u = .1f;
    check(!h.track(key, 5, moved, 3, 2, 2, true, result).matched, "clipped or reordered UV rejected");
    moved[0] = clipped;
    h.beginFrame(5);
    check(!h.track(key, 6, moved, 3, 2, 2, true, result).matched, "topology generation mismatch rejected");
    h.beginFrame(6);
    Key respawn = key;
    ++respawn.generation;
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "reused address generation rejected");
    h.beginFrame(8);
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "missing engine frame resets history");
    h.beginFrame(9, true);
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "explicit camera cut resets history");
    h.beginFrame(10);
    moved[0].x = std::numeric_limits<float>::infinity();
    check(!h.track(respawn, 6, moved, 3, 2, 2, true, result).matched, "nonfinite source rejected");
    moved[0] = clipped;
    check(!h.track(respawn, 6, moved, 3, 2, 2, false, result).matched, "unproven correspondences rejected");
    History limited(1);
    limited.beginFrame(1);
    limited.track(key, 6, moved, 3, 2, 2, true, result);
    check(limited.bytes() == 0, "memory budget enforced");
    History sampled(1024 * 1024, true);
    sampled.beginFrame(1);
    sampled.track(key, 5, old, 3, 2, 2, true, result);
    sampled.commitFrame();
    sampled.beginFrame(2);
    sampled.track(key, 5, moved, 3, 2, 2, true, result); // submitted but not consumed
    sampled.beginFrame(4);
    check(sampled.track(key, 5, moved, 3, 2, 2, true, result).matched,
          "manual history keeps last accepted frame across skipped submissions");
    check(result[0].x / result[0].ratio == 20, "manual reference uses accepted position, not skipped frame");
    // The loops without early exits (setFast(true), the default) against the first implementation: random worlds of
    // tracked batches with moving, unchanged, re-textured, clipped, non-finite and repeated ones, every result, every
    // entry and the kept history compared, through both output forms and through the built-in verification.
    {
        uint64_t seed = 0x9e3779b97f4a7c15ull;
        auto next = [&]() {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            return seed;
        };
        auto unit = [&]() { return float(next() % 100000) / 100000.0f; };
        History fastHistory, referenceHistory, verified;
        referenceHistory.setFast(false);
        verified.setVerify(true);
        constexpr uint32_t kObjects = 24;
        std::vector<std::vector<Vertex>> shapes(kObjects);
        for (auto& shape : shapes) {
            shape.resize(1 + next() % 400);
            for (auto& v : shape)
                v = {unit() * 640, unit() * 448, 1 + unit() * 60000, unit(), unit()};
        }
        uint64_t compared = 0, matched = 0, differ = 0;
        std::vector<Previous> a, b, c;
        std::vector<Previous> direct;
        uint64_t world = 0;
        for (uint64_t epoch = 1; epoch <= 300; ++epoch) {
            const bool cut = next() % 97 == 0;
            world += next() % 53 == 0 ? 2 : 1; // a missed engine world now and then
            fastHistory.beginFrame(world, cut);
            referenceHistory.beginFrame(world, cut);
            verified.beginFrame(world, cut);
            for (uint32_t o = 0; o < kObjects; ++o) {
                auto& shape = shapes[o];
                const uint64_t r = next() % 100;
                std::vector<Vertex> now = shape;
                for (auto& v : now) { // the object moves a little and the camera changes depth
                    v.x += (unit() - 0.5f) * 8;
                    v.y += (unit() - 0.5f) * 8;
                    v.z *= 0.9f + unit() * 0.2f;
                }
                if (r < 5)
                    now[next() % now.size()].u += 0.01f; // clipped or reordered
                else if (r < 8)
                    now[next() % now.size()].x = std::numeric_limits<float>::quiet_NaN();
                else if (r < 10)
                    now[next() % now.size()].z = -1;
                else if (r < 12)
                    now[next() % now.size()].x += 5000; // jumped
                else if (r < 14)
                    now[next() % now.size()].z = now[0].z * 1000; // depth ratio out of range
                else if (r < 16)
                    now.pop_back(); // vertex count changed
                if (now.empty())
                    now.push_back(shape[0]);
                const Key id{o + 1, 1, 7, 0, 0, uint32_t(next() % 8 == 0)};
                const bool known = next() % 23 != 0;
                const uint64_t topology = next() % 31 == 0 ? 2 : 1;
                const uint32_t n = uint32_t(now.size());
                const float sx = o % 5 == 0 ? 2.5f : 3.0f, sy = 3.0f;
                const Result ra = fastHistory.track(id, topology, now.data(), n, sx, sy, known, a);
                const Result rb = referenceHistory.track(id, topology, now.data(), n, sx, sy, known, b);
                direct.assign(n, Previous{7, 7, 7, 7}); // overwritten in full
                const Result rc = verified.track(id, topology, now.data(), n, sx, sy, known, direct.data());
                ++compared;
                matched += rb.matched;
                const bool same = ra.matched == rb.matched && ra.vertices == rb.vertices && rc.matched == rb.matched &&
                                  a.size() == b.size() && direct.size() == b.size() &&
                                  std::memcmp(a.data(), b.data(), b.size() * sizeof(Previous)) == 0 &&
                                  std::memcmp(direct.data(), b.data(), b.size() * sizeof(Previous)) == 0 &&
                                  fastHistory.bytes() == referenceHistory.bytes();
                differ += !same;
                if (r >= 16 && next() % 3) // a broken batch is not where the object stands next
                    shape = now;           // the next world moves on from here
                if (next() % 41 == 0) {    // a repeated identity in this world: ambiguous from here on
                    fastHistory.track(id, topology, now.data(), n, sx, sy, known, a);
                    referenceHistory.track(id, topology, now.data(), n, sx, sy, known, b);
                    verified.track(id, topology, now.data(), n, sx, sy, known, direct.data());
                }
            }
        }
        check(differ == 0, "fast track equals the first implementation in every random batch");
        check(matched > compared / 4, "random worlds match often enough to compare entries");
        check(verified.verifyChecked() > compared && verified.verifyDiffer() == 0,
              "built-in verification finds no difference");
        std::printf("motion history random: %llu batches, %llu matched, %llu differ, verified %llu / %llu\n",
                    (unsigned long long)compared, (unsigned long long)matched, (unsigned long long)differ,
                    (unsigned long long)verified.verifyChecked(), (unsigned long long)verified.verifyDiffer());
    }
    std::printf("motion history %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
