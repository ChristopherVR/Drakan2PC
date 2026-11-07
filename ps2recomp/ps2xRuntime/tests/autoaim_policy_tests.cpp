#include "../src/lib/drakan_autoaim.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace drakan_autoaim;

static bool near(float a, float b) {
    return std::fabs(a - b) < 1e-4f;
}

int main() {
    // facing -z is yaw 0, +x is -pi/2, +z is pi, -x is +pi/2 (the `place` convention)
    assert(near(yawToward(0, -10), 0.0f));
    assert(near(yawToward(10, 0), -1.5707964f));
    assert(near(std::fabs(yawToward(0, 10)), 3.1415927f));
    assert(near(yawToward(-10, 0), 1.5707964f));
    // the matrix written for a yaw reads back as the same yaw
    for (float y : {-3.0f, -1.2f, 0.0f, 0.7f, 2.9f}) {
        float m[9];
        yawMatrix(y, m);
        assert(near(yawOfMatrix(m), y));
    }
    assert(near(stepToward(0, 100, 25), 25) && near(stepToward(90, 100, 25), 100) &&
           near(stepToward(0, -100, 25), -25));
    const auto w = parseWaypoints("1 2 3; 4,5,6 ;bad; 7 8");
    assert(w.size() == 2 && near(w[1].p[2], 6));
    std::cout << "autoaim policy: ok\n";
    return 0;
}
