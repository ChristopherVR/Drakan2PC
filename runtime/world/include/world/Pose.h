// Pose model of the controlled actor: what a ghost needs to look like the local player. Plain data, std only, no
// guest calls, no dependency on net (the net::PlayerState conversion is a duck-typed template at the end).
//   readPlayerPose   one bounds-checked read of the controlled actor's transform and skeleton channels
//   PoseBuffer       time-stamped samples + interpolation for rendering ~100 ms in the past
// Layout facts and confidence tags: docs/reference/world-model.md section 6.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>

#include "world/Addresses.h"
#include "world/Reader.h"
#include "world/Snapshot.h"

namespace world {

// Skeleton (sk = *(handle + 0x50)); offsets are region independent.
namespace skel {
inline constexpr uint32_t kHandleSkeleton = 0x50; // handle -> skeleton instance
inline constexpr uint32_t kFlags = 0x8;
inline constexpr uint32_t kNodeToChannel = 0xdc; // u16[node] -> channel index (-1 none)
inline constexpr uint32_t kChannels = 0xe8;      // ptr to channel array
inline constexpr uint32_t kChannelCount = 0xf0;
inline constexpr uint32_t kChannelStride = 0x80;
inline constexpr uint32_t kChannelNode = 0x4;
inline constexpr uint32_t kChannelClip = 0x50; // u32 clip resource id
inline constexpr uint32_t kChannelTime = 0x58; // float, seconds into the clip
inline constexpr uint32_t kNodeRoot = 0;       // node whose channel carries the locomotion clip (root, legs)
inline constexpr uint32_t kNodeSpine = 2;      // node whose channel carries the upper-body clip (spine, arms)
inline constexpr uint32_t kMaxChannels = 32;   // seen 7 (Arokh) .. 19 (Rynn with held items)
} // namespace skel

// Rynn's instance data (*(*(handle+0xfc)+0x10)): state flags and the two weapon holders (0x24 bytes each).
namespace playerdata {
inline constexpr uint32_t kState = 0x94;
inline constexpr uint32_t kStateAttacking = 0x80;
inline constexpr uint32_t kRightHolder = 0xeb4;    // melee hand
inline constexpr uint32_t kLeftHolder = 0xe90;     // bow / left hand
inline constexpr uint32_t kHolderWeaponRef = 0x1c; // u16 registry ref of the held item (0 or 0xff.. = none)
} // namespace playerdata

struct ChannelPose {
    uint32_t clip = 0; // clip resource id (0x2xxxx); changes snap
    float time = 0;    // seconds into the clip
};

struct PlayerPose {
    uint32_t handle = 0;  // guest handle the pose was read from
    uint32_t classId = 0; // kClassRynn, or kClassDragon while mounted
    bool mounted = false; // controlled actor is the dragon (Rynn herself is then not posed)
    bool attacking = false;
    double timeSec = 0;      // caller's monotonic clock at the read
    double snapshotSec = -1; // network sample time in wall seconds when timeSec uses sender ticks
    uint32_t tick = 0;       // caller's frame counter (informational)
    Vec3 position;
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float yaw = 0; // atan2(m[0][2], m[0][0]), same convention as EntitySnapshot
    float pitch =
        0; // mounted only: rotation = Ry(yaw) * Rx(pitch) * Rz(roll) (yaw is then the Euler angle, see setEuler)
    float roll = 0;
    Vec3 velocity;     // world units / second, finite difference to the previous read (0 on the first)
    ChannelPose lower; // channel of node 0: locomotion (root, legs)
    ChannelPose upper; // channel of node 2: spine / arms (attacks, bow); equals lower when both play one clip
    uint8_t channelCount = 0;
    uint32_t lowerMask = 0;   // bit i: channel i plays the lower clip (all bits when lower == upper)
    uint32_t weaponRight = 0; // resource id of the item in the right (melee) holder, 0 none
    uint32_t weaponLeft = 0;  // resource id of the item in the left (bow) holder, 0 none
};

// Kept between reads (velocity).
struct PoseReadState {
    bool havePrev = false;
    uint32_t prevHandle = 0;
    double prevTime = 0;
    Vec3 prevPos;
};

enum class PoseStatus : uint8_t { Ok, NoContext, NoPlayer, NoSkeleton };
const char* poseStatusName(PoseStatus s);

// Sets rotation (rows (c,0,s),(0,1,0),(-s,0,c), how the game stores Rynn's) and yaw.
void setYaw(PlayerPose& p, float yaw);

// Rotation Ry(yaw) * Rx(pitch) * Rz(roll) (a flying dragon pitches and banks); sets rotation, yaw, pitch and roll.
void setEuler(PlayerPose& p, float yaw, float pitch, float roll);
// The inverse for the current rotation: fills yaw, pitch and roll (gimbal lock at pitch = +-90 degrees is not handled).
void eulerFromRotation(PlayerPose& p);

// Reads the controlled actor (ctx+0x4b0). EE thread, plain reads, no guest calls. `nowSec` must be monotonic.
PoseStatus readPlayerPose(const IGuestMemory& m, const Globals& g, PoseReadState& st, double nowSec, uint32_t tick,
                          PlayerPose& out);

// Inventory turns the live actor for its preview. Retain its gameplay transform for replication, while still
// publishing current clips and equipped items. False means no gameplay transform has been observed yet.
class InventoryPoseFilter {
public:
    bool apply(PlayerPose& pose, bool inventoryOpen);

private:
    PlayerPose gameplay_;
    bool valid_ = false, wasOpen_ = false;
};

// Jitter buffer for a remote player's samples. Sender ticks are the sample coordinate (since protocol v9 the sender's
// pose time in milliseconds; older senders counted their frames), not the receiving instance's update clock. The
// shown tick advances on elapsed time at the sender's cadence, measured over a ~2 s window of
// (tick, snapshot server time) pairs so that snapshot quantisation does not modulate the speed. The speed is trimmed
// by at most a few percent so that the buffered time left just before each new sample (its low envelope) settles on
// a small margin: the playback neither runs dry (a freeze followed by a burst) nor drifts behind. Never runs past the
// newest received tick, never goes backwards; after an outage it catches up smoothly, or jumps when more than
// kSnapSec behind.
class PosePlaybackClock {
public:
    // `nominalRate`: the cadence assumed until the first measurement (1000 for millisecond ticks).
    explicit PosePlaybackClock(double nominalRate = 60) : rate_(nominalRate) {}
    double advance(uint32_t newestTick, uint32_t sampleServerMs, double nowSec);
    double rate() const { return rate_; } // estimated sender ticks per second
    double bufferedSec() const;           // newest received tick minus the shown one, in seconds

    static constexpr double kStartDelaySec = 0.12; // buffered time after a reset
    static constexpr double kMarginSec = 0.04;     // target low envelope of the buffered time
    static constexpr double kSnapSec = 1.0;        // further behind than this: jump

private:
    struct Mark {
        uint32_t tick, serverMs;
    };
    std::deque<Mark> marks_;
    bool valid_ = false, rateKnown_ = false;
    uint32_t newestTick_ = 0, sampleServerMs_ = 0;
    double playTick_ = 0, rate_, lastTime_ = 0, lastArrival_ = 0;
    double lowHeadroom_ = kStartDelaySec; // seconds
};

// Interpolation of two poses at `alpha` in [0,1]: position and velocity lerp, rotation nlerp of the quaternions, clip
// time lerp when both play the same clip without a wrap (else advance); a clip change snaps to b's clip when b is
// reached (a's clip keeps playing until then). Flags / weapons come from a until alpha reaches 1.
// `clipRate`: clip seconds per unit of timeSec (1 when timeSec is wall seconds) for the advance across a wrap.
PlayerPose interpolatePose(const PlayerPose& a, const PlayerPose& b, float alpha, double clipRate = 1.0);

struct PoseBufferConfig {
    double delaySec = 0.100;         // render this far behind the newest sample
    double maxExtrapolateSec = 0.25; // beyond the newest sample: velocity extrapolation and clip advance stop here
    size_t maxSamples = 64;
    double jumpDistance = 1500.0; // a step larger than this between samples is a teleport: no lerp
};

// Samples ordered by timeSec (out-of-order or equal timestamps are dropped). Not thread safe.
class PoseBuffer {
public:
    explicit PoseBuffer(PoseBufferConfig c = {}) : cfg_(c) {}
    void push(const PlayerPose& p);
    void clear() { samples_.clear(); }
    size_t size() const { return samples_.size(); }
    bool empty() const { return samples_.empty(); }
    const PlayerPose* newest() const { return samples_.empty() ? nullptr : &samples_.back(); }
    // Pose to render at `nowSec` (the buffer's clock; the delay is applied here). False when empty.
    bool sample(double nowSec, PlayerPose& out) const { return sampleAt(nowSec - cfg_.delaySec, out); }
    // Pose at an exact buffer time (no delay). `clipRate` as in interpolatePose.
    bool sampleAt(double t, PlayerPose& out, double clipRate = 1.0) const;
    const PoseBufferConfig& config() const { return cfg_; }

private:
    PoseBufferConfig cfg_;
    std::deque<PlayerPose> samples_;
};

// --- net bridge ---------------------------------------------------------------------------------------------------
// Duck-typed so `world` does not depend on `net`: a world -> net include would drag the protocol into the guest-reading
// layer and `net` should stay game-free. Works with any struct with net::PlayerState's fields {tick, pos[3], yaw,
// vel[3], animId, animTime, upperAnimId, upperAnimTime, lowerMask, weaponRight, weaponLeft, mounted}. Lossy by design:
// clips travel as u16 (the 0x2xxxx high part of Rynn's clip ids is constant), the weapons as resource record ids (u32,
// disc constants), and yaw only (Rynn's actor is a pure Y rotation). Arokh's clips (0x4xxxx) travel with their high
// half in clipHi (v8) and a mounted sender's yaw / pitch / roll are sent in full.
inline uint16_t clipWireId(uint32_t clip) {
    return static_cast<uint16_t>(clip & 0xffffu);
}
inline uint32_t clipFromWireId(uint16_t id) {
    return id ? 0x20000u | id : 0u;
}

template <class PlayerStateT>
void toPlayerState(const PlayerPose& p, PlayerStateT& s) {
    s.tick = p.tick;
    s.pos[0] = p.position.x;
    s.pos[1] = p.position.y;
    s.pos[2] = p.position.z;
    s.yaw = p.yaw;
    s.vel[0] = p.velocity.x;
    s.vel[1] = p.velocity.y;
    s.vel[2] = p.velocity.z;
    s.animId = clipWireId(p.lower.clip);
    s.animTime = p.lower.time;
    s.upperAnimId = clipWireId(p.upper.clip);
    s.upperAnimTime = p.upper.time;
    s.lowerMask = p.lowerMask;
    s.weaponRight = p.weaponRight;
    s.weaponLeft = p.weaponLeft;
    s.mounted = p.mounted;
    if (p.mounted) { // v8: a dragon's full orientation (yaw above is then the Euler angle)
        s.yaw = p.yaw;
        s.pitch = p.pitch;
        s.roll = p.roll;
    } else {
        s.pitch = s.roll = 0;
    }
    const uint32_t hi = p.lower.clip >> 16;
    s.clipHi = hi == 2 || hi == 0 ? 0 : static_cast<uint16_t>(hi); // 0 = the usual 0x2xxxx
}

// A pose a ghost can play from a received state; `timeSec` is the receiver's clock for the sample.
template <class PlayerStateT>
PlayerPose fromPlayerState(const PlayerStateT& s, double timeSec) {
    PlayerPose p;
    p.tick = s.tick;
    p.timeSec = timeSec;
    p.position = {s.pos[0], s.pos[1], s.pos[2]};
    p.velocity = {s.vel[0], s.vel[1], s.vel[2]};
    setYaw(p, s.yaw);
    const uint32_t hi = s.clipHi ? static_cast<uint32_t>(s.clipHi) << 16 : 0x20000u;
    p.lower = {s.animId ? hi | s.animId : 0u, s.animTime};
    p.upper = {s.upperAnimId ? hi | s.upperAnimId : 0u, s.upperAnimTime};
    p.lowerMask = s.lowerMask;
    p.weaponRight = s.weaponRight;
    p.weaponLeft = s.weaponLeft;
    p.mounted = s.mounted;
    if (p.mounted)
        setEuler(p, s.yaw, s.pitch, s.roll);
    return p;
}

} // namespace world
