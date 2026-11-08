// The ray output present latch and the modern water status text (src/ray_status.h). Regression: the title crashed
// (null RayWater::status) when its settings read the water status while the ray pass was still built on the prewarm
// thread and a world image of epoch 0 had been latched as a water image.
#include "ray_status.h"

#include <cassert>
#include <iostream>

using gsr::raystatus::outputLatched;
using gsr::raystatus::PassView;
using gsr::raystatus::waterStatus;

int main() {
    // No pass has written an output yet: world epoch 0 (logos, the title backdrop) is not a water image.
    assert(!outputLatched(true, 0, 0));
    assert(!outputLatched(false, 5, 5));
    assert(!outputLatched(true, 4, 5)); // an older pass's output
    assert(outputLatched(true, 5, 5));

    // The pass is still being built (prewarm): no renderer, whatever the latch says.
    const PassView none;
    assert(waterStatus(true, none, true, true, "DXR reflections").find("no eligible water world") != std::string::npos);
    assert(waterStatus(true, none, false, true, "DXR reflections").find("no eligible water world") !=
           std::string::npos);
    assert(waterStatus(false, none, true, true, "DXR reflections") == "Modern water off");

    PassView failed{true, false, "pass failed"};
    assert(waterStatus(true, failed, true, true, "DXR reflections") == "pass failed");

    PassView ready{true, true, "ready"};
    const std::string done = waterStatus(true, ready, true, true, "DXR reflections");
    assert(done.find("pass completed") != std::string::npos &&
           done.find("DXR reflections on. ready") != std::string::npos);
    assert(waterStatus(true, ready, true, false, "ray-traced reflections").find("ray-traced reflections off.") !=
           std::string::npos);
    assert(waterStatus(true, ready, false, true, "DXR reflections").find("Last pass: ready") != std::string::npos);
    std::cout << "ray status latch and water text passed\n";
    return 0;
}
