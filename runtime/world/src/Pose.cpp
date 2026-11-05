#include "world/Pose.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace world {

const char* poseStatusName(PoseStatus s) {
    switch (s) {
        case PoseStatus::Ok:
            return "Ok";
        case PoseStatus::NoContext:
            return "NoContext";
        case PoseStatus::NoPlayer:
            return "NoPlayer";
        case PoseStatus::NoSkeleton:
            return "NoSkeleton";
    }
    return "?";
}

void setYaw(PlayerPose& p, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float rot[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    std::copy(rot, rot + 9, p.rotation);
    p.yaw = std::atan2(p.rotation[2], p.rotation[0]);
}

void setEuler(PlayerPose& p, float yaw, float pitch, float roll) {
    const float ca = std::cos(yaw), sa = std::sin(yaw), cb = std::cos(pitch), sb = std::sin(pitch);
    const float cc = std::cos(roll), sc = std::sin(roll);
    const float rot[9] = {ca * cc + sa * sb * sc,  -ca * sc + sa * sb * cc, sa * cb, cb * sc, cb * cc, -sb,
                          -sa * cc + ca * sb * sc, sa * sc + ca * sb * cc,  ca * cb};
    std::copy(rot, rot + 9, p.rotation);
    p.yaw = yaw;
    p.pitch = pitch;
    p.roll = roll;
}

void eulerFromRotation(PlayerPose& p) {
    const float* m = p.rotation;
    p.pitch = std::asin(std::clamp(-m[5], -1.0f, 1.0f));
    p.yaw = std::atan2(m[2], m[8]);
    p.roll = std::atan2(m[3], m[4]);
}

namespace {

float readF(const IGuestMemory& m, uint32_t a) {
    const uint32_t v = m.read32(a);
    float f;
    static_assert(sizeof f == sizeof v);
    std::memcpy(&f, &v, sizeof f);
    return std::isfinite(f) ? f : 0.0f;
}

// Resource id of the item a weapon holder points at (holder + 0x1c = registry ref), 0 when none / not live.
uint32_t holderResource(const IGuestMemory& m, const Globals& g, uint32_t ctx, uint32_t data, uint32_t holder) {
    const uint32_t ref = m.read16(data + holder + playerdata::kHolderWeaponRef);
    const uint32_t high = m.read32(ctx + ctx::kRegistryHighWater);
    const uint32_t table = m.read32(ctx + ctx::kRegistryTable);
    if (ref == 0 || ref > high + 1 || !isRamPointer(table))
        return 0;
    const uint32_t h = m.read32(table + (ref - 1) * 8u);
    if (!isLiveHandle(m, g, h, ref - 1))
        return 0;
    EntitySnapshot e;
    readEntity(m, g, ctx, h, e);
    return e.resourceId;
}

struct Quat {
    float w, x, y, z;
};

// Rotation matrix (row-major, any consistent convention) <-> quaternion.
Quat toQuat(const float* m) {
    Quat q;
    const float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q = {0.25f * s, (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2;
        q = {(m[7] - m[5]) / s, 0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s};
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2;
        q = {(m[2] - m[6]) / s, (m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2;
        q = {(m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s};
    }
    return q;
}

void fromQuat(Quat q, float* m) {
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n > 1e-8f) {
        q.w /= n;
        q.x /= n;
        q.y /= n;
        q.z /= n;
    } else {
        q = {1, 0, 0, 0};
    }
    m[0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    m[1] = 2 * (q.x * q.y - q.z * q.w);
    m[2] = 2 * (q.x * q.z + q.y * q.w);
    m[3] = 2 * (q.x * q.y + q.z * q.w);
    m[4] = 1 - 2 * (q.x * q.x + q.z * q.z);
    m[5] = 2 * (q.y * q.z - q.x * q.w);
    m[6] = 2 * (q.x * q.z - q.y * q.w);
    m[7] = 2 * (q.y * q.z + q.x * q.w);
    m[8] = 1 - 2 * (q.x * q.x + q.y * q.y);
}

float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}
Vec3 lerpv(const Vec3& a, const Vec3& b, float t) {
    return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t)};
}

// Clip time `t0` at pose a advanced by dt, or blended toward b's time when the same clip continues without wrapping.
ChannelPose blendChannel(const ChannelPose& a, const ChannelPose& b, double span, double dt, float alpha) {
    ChannelPose r;
    const bool sameClip = a.clip == b.clip;
    r.clip = alpha >= 1.0f ? b.clip : a.clip;
    if (alpha >= 1.0f) {
        r.time = b.time;
        return r;
    }
    const float advanced = a.time + static_cast<float>(dt);
    const bool noWrap = sameClip && b.time >= a.time && (b.time - a.time) <= 2.0 * span + 0.05;
    r.time = noWrap ? lerpf(a.time, b.time, alpha) : advanced;
    return r;
}

} // namespace

PoseStatus readPlayerPose(const IGuestMemory& m, const Globals& g, PoseReadState& st, double nowSec, uint32_t tick,
                          PlayerPose& out) {
    out = PlayerPose{};
    const uint32_t ctx = m.read32(g.contextPtr);
    if (!isRamPointer(ctx) || !isRamPointer(ctx + 0x4c0u))
        return PoseStatus::NoContext;
    const uint32_t h = m.read32(ctx + ctx::kPlayerHandle);
    if (!isRamPointer(h) || !isRamPointer(h + 0x120u) || (h & 3u) || m.read32(h + handle::kVtable) != g.handleVtable)
        return PoseStatus::NoPlayer;

    EntitySnapshot e;
    readEntity(m, g, ctx, h, e);
    out.handle = h;
    out.classId = e.classId;
    out.mounted = e.classId == kClassDragon;
    out.timeSec = nowSec;
    out.tick = tick;
    out.position = e.position;
    std::copy(e.rotation, e.rotation + 9, out.rotation);
    out.yaw = e.yaw;
    if (out.mounted)
        eulerFromRotation(out);

    if (st.havePrev && st.prevHandle == h && nowSec > st.prevTime) {
        const auto inv = static_cast<float>(1.0 / (nowSec - st.prevTime));
        out.velocity = {(e.position.x - st.prevPos.x) * inv, (e.position.y - st.prevPos.y) * inv,
                        (e.position.z - st.prevPos.z) * inv};
    }
    st.havePrev = true;
    st.prevHandle = h;
    st.prevTime = nowSec;
    st.prevPos = e.position;

    const uint32_t sk = m.read32(h + skel::kHandleSkeleton);
    const uint32_t channels = isRamPointer(sk) ? m.read32(sk + skel::kChannels) : 0;
    const uint32_t count = isRamPointer(sk) ? m.read32(sk + skel::kChannelCount) : 0;
    const uint32_t table = isRamPointer(sk) ? m.read32(sk + skel::kNodeToChannel) : 0;
    if (!isRamPointer(channels) || !isRamPointer(table) || count == 0 || count > skel::kMaxChannels ||
        !isRamPointer(channels + count * skel::kChannelStride))
        return PoseStatus::NoSkeleton;
    out.channelCount = static_cast<uint8_t>(count);

    const auto channelOfNode = [&](uint32_t node) -> int {
        const auto idx = static_cast<int16_t>(m.read16(table + node * 2));
        return idx >= 0 && static_cast<uint32_t>(idx) < count ? idx : -1;
    };
    const auto readChannel = [&](int c) {
        const uint32_t a = channels + static_cast<uint32_t>(c) * skel::kChannelStride;
        return ChannelPose{m.read32(a + skel::kChannelClip), readF(m, a + skel::kChannelTime)};
    };
    const int lo = channelOfNode(skel::kNodeRoot), up = channelOfNode(skel::kNodeSpine);
    if (lo < 0)
        return PoseStatus::NoSkeleton;
    out.lower = readChannel(lo);
    out.upper = up >= 0 ? readChannel(up) : out.lower;
    if (out.lower.clip == out.upper.clip) {
        out.lowerMask = ~0u;
    } else {
        for (uint32_t i = 0; i < count; ++i)
            if (m.read32(channels + i * skel::kChannelStride + skel::kChannelClip) == out.lower.clip)
                out.lowerMask |= 1u << i;
    }

    if (!out.mounted) { // Rynn's instance data: attack flag and the weapon holders
        const uint32_t inner = m.read32(h + handle::kInner);
        const uint32_t data = isRamPointer(inner) ? m.read32(inner + handle::kInnerData) : 0;
        if (isRamPointer(data) && isRamPointer(data + 0xf00u)) {
            out.attacking = (m.read32(data + playerdata::kState) & playerdata::kStateAttacking) != 0;
            out.weaponRight = holderResource(m, g, ctx, data, playerdata::kRightHolder);
            out.weaponLeft = holderResource(m, g, ctx, data, playerdata::kLeftHolder);
        }
    }
    return PoseStatus::Ok;
}

bool InventoryPoseFilter::apply(PlayerPose& pose, bool inventoryOpen) {
    if (!valid_ || gameplay_.handle != pose.handle || gameplay_.mounted != pose.mounted) {
        valid_ = false;
        wasOpen_ = false;
    }
    // The native close clears its flag one PadMgr boundary before restoring the actor's saved transform.
    if ((inventoryOpen || wasOpen_) && !pose.mounted) {
        if (!valid_)
            return false;
        pose.position = gameplay_.position;
        std::copy(gameplay_.rotation, gameplay_.rotation + 9, pose.rotation);
        pose.yaw = gameplay_.yaw;
        pose.pitch = gameplay_.pitch;
        pose.roll = gameplay_.roll;
        pose.velocity = {};
        wasOpen_ = inventoryOpen;
    } else {
        if (wasOpen_)
            pose.velocity = {};
        gameplay_ = pose;
        valid_ = true;
        wasOpen_ = false;
    }
    return true;
}

double PosePlaybackClock::advance(uint32_t newestTick, uint32_t sampleServerMs, double nowSec) {
    const bool reset = !valid_ || newestTick < newestTick_;
    if (!reset && newestTick > newestTick_ && sampleServerMs > sampleServerMs_) {
        const double measured = (newestTick - newestTick_) * 1000.0 / (sampleServerMs - sampleServerMs_);
        if (measured > 5 && measured < 2000)
            rate_ += (measured - rate_) * 0.2;
    }
    const double delay = std::max(3.0, rate_ * 0.1);
    const double target = static_cast<double>(newestTick) - delay;
    const double dt = std::max(0.0, nowSec - lastTime_);
    if (reset || dt > 2.0 || target - playTick_ > rate_ * 2.0) {
        playTick_ = reset ? target : std::max(playTick_, target);
    } else {
        const double frame = std::min(dt, 0.05);
        const double step = (target - playTick_ + rate_ / 5.0) * (1.0 - std::exp(-frame * 5.0));
        playTick_ = std::min(static_cast<double>(newestTick), playTick_ + std::clamp(step, 0.0, 2.0 * rate_ * frame));
    }
    valid_ = true;
    if (reset || newestTick > newestTick_) {
        newestTick_ = newestTick;
        sampleServerMs_ = sampleServerMs;
    }
    lastTime_ = nowSec;
    return playTick_;
}

PlayerPose interpolatePose(const PlayerPose& a, const PlayerPose& b, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    PlayerPose r = alpha >= 1.0f ? b : a;
    const double span = b.timeSec - a.timeSec;
    const double dt = span * alpha;
    r.timeSec = a.timeSec + dt;
    r.position = lerpv(a.position, b.position, alpha);
    r.velocity = lerpv(a.velocity, b.velocity, alpha);
    Quat qa = toQuat(a.rotation), qb = toQuat(b.rotation);
    if (qa.w * qb.w + qa.x * qb.x + qa.y * qb.y + qa.z * qb.z < 0)
        qb = {-qb.w, -qb.x, -qb.y, -qb.z}; // shortest arc
    fromQuat(
        Quat{lerpf(qa.w, qb.w, alpha), lerpf(qa.x, qb.x, alpha), lerpf(qa.y, qb.y, alpha), lerpf(qa.z, qb.z, alpha)},
        r.rotation);
    r.yaw = std::atan2(r.rotation[2], r.rotation[0]);
    r.lower = blendChannel(a.lower, b.lower, span, dt, alpha);
    r.upper = blendChannel(a.upper, b.upper, span, dt, alpha);
    if (alpha < 1.0f)
        r.lowerMask = a.lowerMask;
    return r;
}

void PoseBuffer::push(const PlayerPose& p) {
    if (!samples_.empty() && p.timeSec <= samples_.back().timeSec)
        return;
    samples_.push_back(p);
    while (samples_.size() > cfg_.maxSamples)
        samples_.pop_front();
}

bool PoseBuffer::sampleAt(double t, PlayerPose& out) const {
    if (samples_.empty())
        return false;
    const PlayerPose& first = samples_.front();
    if (t <= first.timeSec) {
        out = first;
        return true;
    }
    const PlayerPose& last = samples_.back();
    if (t >= last.timeSec) { // past the newest: hold, with bounded extrapolation
        const double dt = std::min(t - last.timeSec, cfg_.maxExtrapolateSec);
        out = last;
        out.timeSec = t;
        const auto f = static_cast<float>(dt);
        out.position = {last.position.x + last.velocity.x * f, last.position.y + last.velocity.y * f,
                        last.position.z + last.velocity.z * f};
        out.lower.time += f;
        out.upper.time += f;
        return true;
    }
    // first sample with timeSec > t
    const auto it = std::upper_bound(samples_.begin(), samples_.end(), t,
                                     [](double v, const PlayerPose& s) { return v < s.timeSec; });
    const PlayerPose& b = *it;
    const PlayerPose& a = *(it - 1);
    const double dx = b.position.x - a.position.x, dy = b.position.y - a.position.y, dz = b.position.z - a.position.z;
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > cfg_.jumpDistance) { // teleport (respawn, level change): no blend
        // A runner can travel farther than the teleport threshold between sparse snapshots. Compare the
        // displacement with the earlier velocity; never extrapolate beyond either measured position.
        const double elapsed = b.snapshotSec - a.snapshotSec;
        const bool continuedTravel = a.snapshotSec >= 0 && elapsed > 0 && elapsed <= 2.0 &&
                                     std::hypot(dx - a.velocity.x * elapsed, dy - a.velocity.y * elapsed,
                                                dz - a.velocity.z * elapsed) <= cfg_.jumpDistance;
        if (!continuedTravel) {
            out = a;
            out.timeSec = t;
            out.lower.time += static_cast<float>(t - a.timeSec);
            out.upper.time += static_cast<float>(t - a.timeSec);
            return true;
        }
    }
    const double span = b.timeSec - a.timeSec;
    out = interpolatePose(a, b, static_cast<float>((t - a.timeSec) / span));
    return true;
}

} // namespace world
