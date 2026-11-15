// Far land rules (world/FarLand.h): synthetic layers only.
#include <cstdio>
#include <vector>

#include "world/FarLand.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world::far_land;

Layer layer(int type, int x0, int z0, int w, int h, float y = 10000.0f, bool drawn = true) {
    Layer L;
    L.type = type;
    L.x0 = x0;
    L.z0 = z0;
    L.w = w;
    L.h = h;
    L.heights.assign(size_t(w + 1) * (h + 1), y);
    L.drawn.assign(size_t(w) * h, drawn ? 1 : 0);
    return L;
}

std::vector<uint16_t> far(size_t view, const std::vector<Layer>& layers, std::vector<uint16_t> listed,
                          float reach = 77.0f) {
    const Ground ground(layers);
    std::vector<std::vector<Point>> cache;
    return farLayers(view, layers, ground, listed, reach, cache);
}

} // namespace

int main() {
    {
        // floors and ceilings in sight are added, nearest first; listed and out-of-reach layers are not
        const std::vector<Layer> layers = {
            layer(kFloor, 0, 0, 10, 10),          // 0: the view layer
            layer(kFloor, 50, 0, 10, 10),         // 1: 40 cells away over flat ground
            layer(kCeiling, 50, 20, 4, 4, 12000), // 2: a ceiling 40 cells away
            layer(kFloor, 15, 0, 5, 5),           // 3: 5 cells away
            layer(kFloor, 200, 0, 5, 5),          // 4: beyond the reach
            layer(kWater, 0, 20, 10, 10),         // 5: water observes too, but is never added
            layer(kFloor, 40, 0, 5, 5),           // 6: 30 cells away, already listed
        };
        CHECK((far(0, layers, {0, 6}) == std::vector<uint16_t>{3, 1, 2}));
        CHECK((far(5, layers, {5}) == std::vector<uint16_t>{0, 3, 6, 1, 2}));
        CHECK(far(2, layers, {2}).empty()); // a ceiling view layer keeps its own list
        CHECK(far(4, layers, {4}).empty());
        CHECK(far(9, layers, {}).empty()); // no such layer
        CHECK((far(0, layers, {0}, 35.0f) == std::vector<uint16_t>{3, 6}));
    }
    {
        // a ridge 50,000 units high between the view layer and the far layers hides them
        std::vector<Layer> layers = {
            layer(kFloor, 0, 0, 4, 4),
            layer(kFloor, 4, 0, 30, 4),
            layer(kFloor, 40, 0, 4, 4),
            layer(kFloor, 70, 0, 4, 4),
        };
        for (int z = 0; z <= 4; ++z)
            for (int x : {14, 15, 16})
                layers[1].heights[size_t(z) * 31 + x] = 50000.0f;
        CHECK((far(0, layers, {0}) == std::vector<uint16_t>{1})); // only the ridge layer itself
        // from layer 2 the far layer 3 is in sight, layer 0 is behind the ridge
        CHECK((far(2, layers, {2}) == std::vector<uint16_t>{1, 3}));
        // a ridge of holes (or invisible cells) hides nothing
        for (auto& d : layers[1].drawn)
            d = 0;
        CHECK((far(0, layers, {0}) == std::vector<uint16_t>{2, 3}));
    }
    {
        // the ground's far edge row still counts as ground
        const std::vector<Layer> layers = {layer(kFloor, 0, 0, 4, 4)};
        const Ground g(layers);
        CHECK(g.at(4.0f, 4.0f) == 10000.0f && g.at(0.0f, 0.0f) == 10000.0f);
        CHECK(g.at(4.5f, 2.0f) < -1e30f && g.at(-0.5f, 2.0f) < -1e30f);
        // a layer with no drawn cell has no sample points
        CHECK(samplePoints(layer(kFloor, 0, 0, 3, 3, 0.0f, false), 3, 12).empty());
        CHECK(!Layer{}.usable());
    }
    {
        // observers at the camera: the camera alone by default; with a cushion and a rise, four points to its sides
        // and all of them again higher up
        CHECK(cameraObservers(4096.0f, 11000.0f, 2048.0f).size() == 1);
        const std::vector<Point> obs = cameraObservers(4096.0f, 11000.0f, 2048.0f, 1.5f, 1500.0f);
        CHECK(obs.size() == 10);
        CHECK(obs[0].x == 2.0f && obs[0].z == 1.0f && obs[0].y == 11000.0f);
        CHECK(obs[1].x == 3.5f && obs[5].y == 12500.0f);
        CHECK(cameraObservers(0.0f, 0.0f, 0.0f, 0.0f, 1500.0f).size() == 2);
        // from a camera low behind the ridge only the ridge layer is added; from high above it the far layers too
        std::vector<Layer> layers = {
            layer(kFloor, 0, 0, 4, 4),
            layer(kFloor, 4, 0, 30, 4),
            layer(kFloor, 40, 0, 4, 4),
            layer(kFloor, 70, 0, 4, 4),
        };
        for (int z = 0; z <= 4; ++z)
            for (int x : {14, 15, 16})
                layers[1].heights[size_t(z) * 31 + x] = 50000.0f;
        const Ground g(layers);
        std::vector<std::vector<Point>> cache;
        CHECK((farLayers(0, layers, g, {0}, 77.0f, cameraObservers(4096.0f, 11000.0f, 4096.0f), cache) ==
               std::vector<uint16_t>{1}));
        CHECK((farLayers(0, layers, g, {0}, 77.0f, cameraObservers(4096.0f, 200000.0f, 4096.0f), cache) ==
               std::vector<uint16_t>{1, 2, 3}));
        CHECK(farLayers(0, layers, g, {0}, 77.0f, {}, cache).empty());
    }
    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("world_far_land_test: ok\n");
    return g_failed ? 1 : 0;
}
