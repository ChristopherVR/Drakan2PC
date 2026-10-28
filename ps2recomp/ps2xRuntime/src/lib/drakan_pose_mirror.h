// Left-handed strikes for dual wielding (drakan_dual_wield.cpp): Rynn's evaluated pose mirrored left <-> right, so her
// left arm performs the right-handed swing. Pure math on the native records, kept free of guest memory for tests.
//
// Native layout (SCES_500.06, docs/reference/combat-controls.md "Dual wielding"): the skeleton's pose is an array of
// 0x6c-byte records at sk+0x10c (65 nodes for Rynn), each starting with a model-space affine {position[3],
// rotation[3][3]} (row vectors: x_model = x_local * R + p; child = local * parent). The resource's inverse bind table
// (*(res+0xc8), 0x40 per node) starts with the same affine. Skinning uses the palette IB[k] * M[i], k = record +0x68.
// The model is symmetric about x = 0 (left side -x).
//
// Mirror: the palette of node i becomes S * palette(partner) * S with S = diag(-1, 1, 1), i.e.
//   M'[i] = B[k(i)] * S * IB[k(m)] * M[m] * S        (m = partner of i, B = IB^-1)
// which keeps every rotation proper (det +1).
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace drakan_dual_wield::pose {

struct Affine {
    float p[3] = {0, 0, 0};
    float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // row-major

    static Affine mirror() { // S
        Affine s;
        s.r[0] = -1;
        return s;
    }
};

// a then b (row vectors): x * a * b.
inline Affine mul(const Affine& a, const Affine& b) {
    Affine o;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o.r[i * 3 + j] = a.r[i * 3] * b.r[j] + a.r[i * 3 + 1] * b.r[3 + j] + a.r[i * 3 + 2] * b.r[6 + j];
    for (int j = 0; j < 3; ++j)
        o.p[j] = a.p[0] * b.r[j] + a.p[1] * b.r[3 + j] + a.p[2] * b.r[6 + j] + b.p[j];
    return o;
}

// General inverse (bind matrices are not assumed orthonormal). False when singular.
inline bool inverse(const Affine& a, Affine& o) {
    const float* m = a.r;
    const float c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8], c2 = m[3] * m[7] - m[4] * m[6];
    const float det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    if (!(std::fabs(det) > 1e-12f))
        return false;
    const float id = 1.0f / det;
    o.r[0] = c0 * id;
    o.r[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    o.r[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    o.r[3] = c1 * id;
    o.r[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    o.r[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    o.r[6] = c2 * id;
    o.r[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    o.r[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    for (int j = 0; j < 3; ++j)
        o.p[j] = -(a.p[0] * o.r[j] + a.p[1] * o.r[3 + j] + a.p[2] * o.r[6 + j]);
    return true;
}

// Rynn's 65-node skeleton (bind-pose analysis): left arm 15..31 <-> right arm 33..49 (clavicle, upper arm, forearm,
// hand, grip, then the finger chains in sibling order), legs 54..58 <-> 60..64, everything else (spine, head, hair,
// helpers) on the centre plane. Node 32 (the shield bone) shares node 17's bind and follows it.
inline constexpr int kNodes = 65;
inline constexpr int kShieldNode = 32, kShieldParent = 17;
// The lower body: root, pelvis and the hip / leg nodes (0 -> 1 pelvis -> 2 spine; 0 -> 52 -> 53 -> legs 54..64). The
// rest (spine, head, arms) is the upper body.
inline constexpr int kPelvis = 1;
inline bool isLower(int i) {
    return i <= kPelvis || i >= 52;
}
inline int partner(int i) {
    if (i >= 15 && i <= 31)
        return i + 18;
    if (i >= 33 && i <= 49)
        return i - 18;
    if (i >= 54 && i <= 58)
        return i + 6;
    if (i >= 60 && i <= 64)
        return i - 6;
    return i;
}

// Per-skeleton constants: C[i] = B[k(i)] * S * IB[k(m)], so that M'[i] = C[i] * M[m] * S.
struct Mirror {
    std::array<Affine, kNodes> c{};
    Affine ibPelvis; // the pelvis' inverse bind
    bool valid = false;

    // ib: inverse bind per bind index, bindIndex: record +0x68 per node.
    bool build(const Affine* ib, int ibCount, const int* bindIndex) {
        valid = false;
        const Affine s = Affine::mirror();
        for (int i = 0; i < kNodes; ++i) {
            const int ki = bindIndex[i], km = bindIndex[partner(i)];
            if (ki < 0 || ki >= ibCount || km < 0 || km >= ibCount)
                return false;
            Affine b;
            if (!inverse(ib[ki], b))
                return false;
            c[size_t(i)] = mul(mul(b, s), ib[km]);
        }
        ibPelvis = ib[bindIndex[kPelvis]];
        valid = true;
        return true;
    }

    // out[i] = mirrored pose; in and out must not alias.
    void apply(const Affine* in, Affine* out) const {
        const Affine s = Affine::mirror();
        for (int i = 0; i < kNodes; ++i)
            out[i] = i == kShieldNode ? Affine{} : mul(mul(c[size_t(i)], in[partner(i)]), s);
        out[kShieldNode] = out[kShieldParent];
    }
};

// ---- blending (w = 0 the native pose, 1 the mirrored one) ----
struct Quat {
    float x, y, z, w;
};
inline Quat toQuat(const float* m) {
    const float t = m[0] + m[4] + m[8];
    Quat q;
    if (t > 0) {
        const float s = std::sqrt(t + 1.0f) * 2;
        q = {(m[5] - m[7]) / s, (m[6] - m[2]) / s, (m[1] - m[3]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[6] + m[2]) / s, (m[5] - m[7]) / s};
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[6] - m[2]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2;
        q = {(m[6] + m[2]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[1] - m[3]) / s};
    }
    return q;
}
inline void fromQuat(Quat q, float* m) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 0) {
        q.x /= n;
        q.y /= n;
        q.z /= n;
        q.w /= n;
    }
    m[0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    m[1] = 2 * (q.x * q.y + q.z * q.w);
    m[2] = 2 * (q.x * q.z - q.y * q.w);
    m[3] = 2 * (q.x * q.y - q.z * q.w);
    m[4] = 1 - 2 * (q.x * q.x + q.z * q.z);
    m[5] = 2 * (q.y * q.z + q.x * q.w);
    m[6] = 2 * (q.x * q.z + q.y * q.w);
    m[7] = 2 * (q.y * q.z - q.x * q.w);
    m[8] = 1 - 2 * (q.x * q.x + q.y * q.y);
}
// Normalised linear quaternion blend (shortest arc) and a linear position blend.
inline Affine blend(const Affine& a, const Affine& b, float w) {
    if (w <= 0)
        return a;
    if (w >= 1)
        return b;
    Quat qa = toQuat(a.r), qb = toQuat(b.r);
    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0)
        qb = {-qb.x, -qb.y, -qb.z, -qb.w};
    Affine o;
    fromQuat({qa.x + (qb.x - qa.x) * w, qa.y + (qb.y - qa.y) * w, qa.z + (qb.z - qa.z) * w, qa.w + (qb.w - qa.w) * w},
             o.r);
    for (int j = 0; j < 3; ++j)
        o.p[j] = a.p[j] + (b.p[j] - a.p[j]) * w;
    return o;
}

// ---- split mirror ----
// The lower body (root, pelvis, legs) blends toward the full mirror by wLower; the upper body blends toward its mirror
// about the final pelvis by wUpper:
//   M'[i] = C[i] * M[m] * Kp^-1 * S * Kp'      Kp = IBpelvis * M[pelvis] (native), Kp' = the same of the final pelvis
// so the spine stays on the hips. With the lower body mirrored too (Kp' = S Kp S) this is the full mirror C M[m] S;
// with it native, the upper body's palette is the partner's mirrored about the pelvis. A strike made while moving keeps
// the legs' locomotion (and its root motion) native and only the attack is left-handed.
inline void applySplit(const Mirror& mir, const Affine* in, float wUpper, float wLower, Affine* out) {
    Affine full[kNodes];
    mir.apply(in, full);
    for (int i = 0; i < kNodes; ++i)
        out[i] = isLower(i) ? blend(in[i], full[i], wLower) : in[i];
    const Affine s = Affine::mirror();
    Affine kpInv;
    if (!inverse(mul(mir.ibPelvis, in[kPelvis]), kpInv)) {
        for (int i = 0; i < kNodes; ++i)
            if (!isLower(i))
                out[i] = blend(in[i], full[i], wUpper);
        out[kShieldNode] = out[kShieldParent];
        return;
    }
    const Affine tail = mul(mul(kpInv, s), mul(mir.ibPelvis, out[kPelvis]));
    for (int i = 0; i < kNodes; ++i)
        if (!isLower(i) && i != kShieldNode)
            out[i] = blend(in[i], mul(mul(mir.c[size_t(i)], in[partner(i)]), tail), wUpper);
    out[kShieldNode] = out[kShieldParent];
}

// World <-> model space for actor deltas: world = M * d (M rows at model+0x20). False when M is singular.
inline bool worldToModel(const float m[9], const float world[3], float out[3]) {
    Affine t;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t.r[i * 3 + j] = m[j * 3 + i];
    Affine inv;
    if (!inverse(t, inv))
        return false;
    for (int j = 0; j < 3; ++j)
        out[j] = world[0] * inv.r[j] + world[1] * inv.r[3 + j] + world[2] * inv.r[6 + j];
    return true;
}

// ---- root motion ----
// The pose update moves the actor by the animation's root motion (0x1198d0 -> 0x17ea70(model, delta, 0)) as a world
// delta = M * d, M the actor's matrix (rows at model+0x20, column vectors), d the model-space delta. A mirrored strike
// lunges to the other side: d.x scaled by 1 - 2w. False (delta untouched) when M is singular.
inline bool mirrorRootDelta(const float m[9], float delta[3], float w) {
    Affine t; // row-vector form of M: x * M^T
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t.r[i * 3 + j] = m[j * 3 + i];
    Affine inv;
    if (!inverse(t, inv))
        return false;
    float d[3];
    for (int j = 0; j < 3; ++j)
        d[j] = delta[0] * inv.r[j] + delta[1] * inv.r[3 + j] + delta[2] * inv.r[6 + j];
    d[0] *= 1.0f - 2.0f * std::fmin(1.0f, std::fmax(0.0f, w));
    for (int j = 0; j < 3; ++j)
        delta[j] = d[0] * t.r[j] + d[1] * t.r[3 + j] + d[2] * t.r[6 + j];
    return true;
}

// ---- which strikes are left-handed ----
// Strikes alternate right, left, right ... while Rynn keeps attacking; after a pause the next attack starts with the
// right hand again. A strike starts when the attack flag rises, or (still attacking) when the attack clip or the combo
// step changes.
struct StrikeTracker {
    bool attacking = false, left = false, nextLeft = false;
    uint32_t clip = 0, step = 0;
    float time = 0, progress = 0, idle = 0;
    static constexpr float kResetAfter = 1.0f; // seconds without attacking: the next attack leads with the right hand
    // A strike that restarts its own clip (same clip and combo step, time back to 0) before it got this far, after
    // at most this long without the attack flag, is the same strike. Locked on, the game starts an attack, drops the
    // flag for a frame and starts the clip again (combat-controls.md "Dual wielding", run dw-enemy2).
    static constexpr float kMinStrike = 0.1f;

    // Returns whether the current strike is left-handed. time: the root channel's clip time.
    bool update(bool nowAttacking, uint32_t nowClip, uint32_t nowStep, float nowTime, float dt) {
        if (!nowAttacking) {
            idle += dt;
            if (idle > kResetAfter)
                nextLeft = false;
            attacking = false;
            return false;
        }
        const bool started = !attacking || nowClip != clip || nowStep != step || nowTime + 1e-3f < time;
        const bool restart =
            started && nowClip == clip && nowStep == step && progress < kMinStrike && idle < kMinStrike;
        if (started && !restart) {
            left = nextLeft;
            nextLeft = !nextLeft;
            progress = 0;
        }
        idle = 0;
        attacking = true;
        clip = nowClip;
        step = nowStep;
        time = nowTime;
        progress = std::fmax(progress, nowTime);
        return left;
    }
};

// Eases the mirror weight toward its target (seconds for a full swing).
inline float approach(float w, bool target, float dt, float seconds = 0.12f) {
    const float step = seconds > 0 ? dt / seconds : 1.0f;
    return target ? std::fmin(1.0f, w + step) : std::fmax(0.0f, w - step);
}

} // namespace drakan_dual_wield::pose
