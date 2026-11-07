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
    {
        // pitched aim: forward (= -column 2) points at the target, the basis is orthonormal, the pure-yaw case matches
        // yawMatrix
        float m[9];
        aimMatrix(300, -400, -1200, m);
        const float len = std::sqrt(300.0f * 300 + 400.0f * 400 + 1200.0f * 1200);
        assert(near(-m[2], 300 / len) && near(-m[5], -400 / len) && near(-m[8], -1200 / len));
        assert(near(m[0] * m[1] + m[3] * m[4] + m[6] * m[7], 0.0f));
        assert(near(m[0] * m[0] + m[3] * m[3] + m[6] * m[6], 1.0f));
        float flat[9], yaw[9];
        aimMatrix(500, 0, -200, flat);
        yawMatrix(yawToward(500, -200), yaw);
        for (int i = 0; i < 9; ++i)
            assert(near(flat[i], yaw[i]));
    }
    std::cout << "autoaim policy: ok\n";
    return 0;
}
