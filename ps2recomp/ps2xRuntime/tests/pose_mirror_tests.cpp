#include "../src/lib/drakan_pose_mirror.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace drakan_dual_wield::pose;

static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}
static float maxDiff(const Affine& a, const Affine& b) {
    float d = 0;
    for (int i = 0; i < 9; ++i)
        d = std::fmax(d, std::fabs(a.r[i] - b.r[i]));
    for (int i = 0; i < 3; ++i)
        d = std::fmax(d, std::fabs(a.p[i] - b.p[i]) * 0.01f);
    return d;
}
static float det(const Affine& a) {
    const float* m = a.r;
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}
static Affine rot(float ax, float ay, float az, float px, float py, float pz) {
    Affine x, y, z;
    x.r[4] = std::cos(ax), x.r[5] = std::sin(ax), x.r[7] = -std::sin(ax), x.r[8] = std::cos(ax);
    y.r[0] = std::cos(ay), y.r[2] = -std::sin(ay), y.r[6] = std::sin(ay), y.r[8] = std::cos(ay);
    z.r[0] = std::cos(az), z.r[1] = std::sin(az), z.r[3] = -std::sin(az), z.r[4] = std::cos(az);
    Affine o = mul(mul(x, y), z);
    o.p[0] = px, o.p[1] = py, o.p[2] = pz;
    return o;
}

int main() {
    const Affine s = Affine::mirror();
    // A symmetric bind: each right node's bind is the mirror of its left partner's with a 180-degree turn about y (as
    // Rynn's left-side bones have), centre nodes symmetric themselves.
    Affine bind[kNodes], ib[kNodes];
    const Affine turn = rot(0, 3.14159265f, 0, 0, 0, 0);
    for (int i = 0; i < kNodes; ++i) {
        const int m = partner(i);
        if (m == i) {
            bind[i] = rot(0, 0, 0, 0, 10.0f * float(i), 0);
        } else if (i < m) {
            bind[i] = rot(0.1f * float(i), 0.2f, -0.05f * float(i), -20.0f - float(i), 5.0f * float(i), 3);
            bind[m] = mul(mul(s, mul(turn, bind[i])), s); // S * B * S keeps det +1
        }
    }
    bind[kShieldNode] = bind[kShieldParent];
    int bindIndex[kNodes];
    for (int i = 0; i < kNodes; ++i) {
        check(inverse(bind[i], ib[i]), "bind invertible");
        bindIndex[i] = i;
    }
    check(maxDiff(mul(bind[3], ib[3]), Affine{}) < 1e-5f, "inverse");

    // An animated pose: arbitrary rotations per node.
    Affine pose[kNodes], mirrored[kNodes], twice[kNodes];
    for (int i = 0; i < kNodes; ++i)
        pose[i] = rot(0.3f + 0.01f * float(i), -0.7f * float(i % 5), 0.25f * float(i % 3), 3.0f * float(i), -40, 7);
    pose[kShieldNode] = pose[kShieldParent];

    Mirror mir;
    check(mir.build(ib, kNodes, bindIndex), "constants build");
    mir.apply(pose, mirrored);
    float palette = 0, worstDet = 0;
    for (int i = 0; i < kNodes; ++i) {
        const int m = partner(i);
        if (i == kShieldNode)
            continue;
        palette = std::fmax(palette, maxDiff(mul(ib[i], mirrored[i]), mul(mul(s, mul(ib[m], pose[m])), s)));
        worstDet = std::fmax(worstDet, std::fabs(det(mirrored[i]) - 1.0f));
    }
    check(palette < 1e-4f, "skin palette of node i = S * palette(partner) * S");
    check(worstDet < 1e-4f, "mirrored rotations stay proper (det +1)");
    check(maxDiff(mirrored[kShieldNode], mirrored[kShieldParent]) == 0, "shield bone follows node 17");
    mir.apply(mirrored, twice);
    float back = 0;
    for (int i = 0; i < kNodes; ++i)
        if (i != kShieldNode)
            back = std::fmax(back, maxDiff(twice[i], pose[i]));
    check(back < 1e-3f, "mirroring twice gives the pose back");
    // Left grip (19) mirrors the right grip (37): its position is the right one reflected in x through the bind.
    check(partner(19) == 37 && partner(37) == 19 && partner(55) == 61 && partner(3) == 3, "partners");
    {
        Affine badIb[kNodes];
        for (int i = 0; i < kNodes; ++i)
            badIb[i] = ib[i];
        int badIndex[kNodes];
        for (int i = 0; i < kNodes; ++i)
            badIndex[i] = i;
        badIndex[20] = 200;
        Mirror bad;
        check(!bad.build(badIb, kNodes, badIndex) && !bad.valid, "bind index out of range: refused");
    }

    // Split mirror: lower body (root, pelvis, legs) and upper body separately.
    {
        Affine split[kNodes];
        applySplit(mir, pose, 1, 1, split);
        float d = 0;
        for (int i = 0; i < kNodes; ++i)
            d = std::fmax(d, maxDiff(split[i], mirrored[i]));
        check(d < 1e-3f, "both weights 1: the full mirror");
        applySplit(mir, pose, 0, 0, split);
        d = 0;
        for (int i = 0; i < kNodes; ++i)
            d = std::fmax(d, maxDiff(split[i], pose[i]));
        check(d < 1e-4f, "both weights 0: the native pose");
        // Upper body only (moving): the lower body native, the upper palette mirrored about the pelvis.
        applySplit(mir, pose, 1, 0, split);
        float lower = 0, upper = 0, splitWorstDet = 0;
        const Affine kp = mul(ib[kPelvis], pose[kPelvis]);
        Affine kpInv;
        check(inverse(kp, kpInv), "pelvis palette invertible");
        for (int i = 0; i < kNodes; ++i) {
            if (isLower(i)) {
                lower = std::fmax(lower, maxDiff(split[i], pose[i]));
                continue;
            }
            if (i == kShieldNode)
                continue;
            const int m = partner(i);
            const Affine want = mul(mul(mul(mul(s, mul(ib[m], pose[m])), kpInv), s), kp);
            upper = std::fmax(upper, maxDiff(mul(ib[i], split[i]), want));
            splitWorstDet = std::fmax(splitWorstDet, std::fabs(det(split[i]) - 1.0f));
        }
        check(lower < 1e-4f, "upper only: root, pelvis and legs untouched");
        check(upper < 1e-3f, "upper only: palette = S (K[m] Kp^-1) S Kp");
        check(splitWorstDet < 1e-3f, "upper only: proper rotations");
        for (float weight : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            applySplit(mir, pose, weight, 0, split);
            float error = 0;
            for (int i = 0; i < kNodes; ++i)
                if (isLower(i))
                    error = std::fmax(error, maxDiff(split[i], pose[i]));
            check(error < 1e-4f, "moving: legs stay native throughout the attack blend");
        }
        check(isLower(0) && isLower(1) && isLower(54) && isLower(64) && !isLower(2) && !isLower(19) && !isLower(51),
              "lower / upper partition");
    }
    {
        const float yaw[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0}; // world = M * d
        const float world[3] = {0, 0, -5};                 // = M * (5, 0, 0)
        float local[3];
        check(worldToModel(yaw, world, local) && std::fabs(local[0] - 5) < 1e-4f && std::fabs(local[2]) < 1e-4f,
              "world to model space");
    }

    // Quaternion round trip and blend endpoints.
    for (int i = 0; i < kNodes; ++i) {
        Affine r;
        fromQuat(toQuat(pose[i].r), r.r);
        for (int k = 0; k < 3; ++k)
            r.p[k] = pose[i].p[k];
        check(maxDiff(r, pose[i]) < 1e-4f, "quaternion round trip");
    }
    check(maxDiff(blend(pose[19], mirrored[19], 0), pose[19]) == 0, "blend 0 = native");
    check(maxDiff(blend(pose[19], mirrored[19], 1), mirrored[19]) == 0, "blend 1 = mirrored");
    check(std::fabs(det(blend(pose[19], mirrored[19], 0.5f)) - 1.0f) < 1e-4f, "half blend stays a rotation");
    check(approach(0, true, 0.06f) > 0.49f && approach(0, true, 0.06f) < 0.51f, "ramp 0.12 s");
    check(approach(1, false, 1.0f) == 0 && approach(1, true, 1.0f) == 1, "ramp clamps");

    // Root motion: model-space sideways component flipped (rotated actor), forward and up kept, w scales.
    {
        const float yc = std::cos(0.7f), ys = std::sin(0.7f);
        const float m[9] = {yc, 0, ys, 0, 1, 0, -ys, 0, yc}; // world = M * d (yaw)
        const float d[3] = {12, 3, -40};                     // model space: 12 sideways, 40 forward
        float world[3];
        for (int i = 0; i < 3; ++i)
            world[i] = m[i * 3] * d[0] + m[i * 3 + 1] * d[1] + m[i * 3 + 2] * d[2];
        float full[3] = {world[0], world[1], world[2]}, none[3] = {world[0], world[1], world[2]};
        check(mirrorRootDelta(m, full, 1.0f) && mirrorRootDelta(m, none, 0.0f), "root delta mirrors");
        const float want[3] = {-12, 3, -40};
        float local[3];
        for (int i = 0; i < 3; ++i) // M^T * full = the model-space delta (M is a rotation)
            local[i] = m[i] * full[0] + m[3 + i] * full[1] + m[6 + i] * full[2];
        check(std::fabs(local[0] - want[0]) < 1e-3f && std::fabs(local[1] - want[1]) < 1e-3f &&
                  std::fabs(local[2] - want[2]) < 1e-3f,
              "full weight: sideways flipped, forward / up kept");
        check(std::fabs(none[0] - world[0]) < 1e-4f && std::fabs(none[2] - world[2]) < 1e-4f, "weight 0: unchanged");
        float half[3] = {world[0], world[1], world[2]};
        mirrorRootDelta(m, half, 0.5f);
        for (int i = 0; i < 3; ++i)
            local[i] = m[i] * half[0] + m[3 + i] * half[1] + m[6 + i] * half[2];
        check(std::fabs(local[0]) < 1e-3f && std::fabs(local[2] + 40) < 1e-3f, "half weight: no sideways motion");
        const float singular[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        float keep[3] = {1, 2, 3};
        check(!mirrorRootDelta(singular, keep, 1) && keep[0] == 1, "singular matrix: untouched");
    }

    // Strikes alternate while attacking; a pause starts again on the right.
    StrikeTracker t;
    const uint32_t kIdle = 0x2066d, kSwing = 0x20691, kSwing2 = 0x20692;
    check(!t.update(false, kIdle, 0, 0.5f, 0.016f), "idle: right");
    check(!t.update(true, kSwing, 1, 0.0f, 0.016f), "first strike: right");
    check(!t.update(true, kSwing, 1, 0.2f, 0.016f), "same strike stays right");
    check(t.update(true, kSwing2, 2, 0.0f, 0.016f), "chained strike (clip / step change): left");
    check(!t.update(false, kIdle, 2, 0.1f, 0.2f), "between strikes: no mirror");
    check(!t.update(true, kSwing, 1, 0.0f, 0.016f), "next attack soon after: right (alternating)");
    check(!t.update(true, kSwing, 1, 0.3f, 0.016f), "held");
    check(!t.update(false, kIdle, 1, 0.1f, 0.3f), "pause");
    check(t.update(true, kSwing, 1, 0.0f, 0.016f), "attack within the reset time continues the alternation: left");
    check(!t.update(false, kIdle, 1, 0.1f, 1.5f), "long pause");
    check(!t.update(true, kSwing, 1, 0.0f, 0.016f), "after a long pause the right hand leads again");
    // Locked on: the attack starts, the flag drops for a frame and the clip starts again: still the same strike.
    {
        StrikeTracker g;
        check(!g.update(true, kSwing, 1, 0.0f, 0.009f) && !g.update(true, kSwing, 1, 0.009f, 0.009f), "glitch: start");
        check(!g.update(false, kSwing, 1, 0.0f, 0.009f), "glitch: flag drops one frame");
        check(!g.update(true, kSwing, 1, 0.0f, 0.009f), "glitch: the restart is the same (right) strike");
        check(!g.update(true, kSwing, 1, 0.5f, 0.009f), "glitch: runs on");
        check(!g.update(false, kIdle, 1, 0.0f, 0.05f), "between");
        check(g.update(true, kSwing, 1, 0.0f, 0.009f), "the next strike (same clip, after the first ran) is left");
        check(g.update(true, kSwing, 1, 0.0f, 0.009f) && g.update(true, kSwing, 1, 0.3f, 0.009f), "it stays left");
        check(!g.update(true, kSwing, 1, 0.0f, 0.009f), "a clip restart after the strike ran is a new strike");
    }

    std::printf("pose_mirror: %u checks passed\n", checks);
    return 0;
}
