// Dual wielding (drakan_dual_wield.h). Wrappers around native player code that run the original on the real context.
// Post-hooks act only when it completed (a function that yielded returns with pc != $ra; the next call catches up,
// every decision below is idempotent); the pre-hooks (skinning, actor move, the blade request) only rewrite inputs:
//   * 0x344cb8 (inventory controller vtable +0x24, item -> equipment slot): a second one-handed weapon goes to slot 2,
//     the shield slot (rule: gameui/DualWield.h, shared with the inventory screens);
//   * 0x31ca20 (attach the equipped items, mask of slots): after the shield-slot pass, attach the slot-2 weapon to the
//     left hand bone (Rynn's property 0x46) or detach a stale one, and give it Rynn as owner so its blade sweep skips
//     her;
//   * 0x342f90 (details text of the selected item): a line naming the hand ("Equips to left hand");
//   * 0x49c590 (hand holder: set the blade live / dead): for the right hand holder (player+0xeb4) the attack window
//     goes to the striking hand's blade (the right one's request is withheld during a left-handed strike);
//   * 0x17ea70 (actor move), from the root-motion step: a mirrored strike's lunge is mirrored too;
//   * 0x1165b0 (skinning, called by the pose update 0x103268 once the pose is final), a pre-hook: for left-handed
//     strikes Rynn's pose is mirrored left <-> right (drakan_pose_mirror.h), then the two weapon attachments and the
//     node matrices the channels export are recomputed from it, so her left arm swings and the off-hand blade follows.
// Guest functions that may block (the skeleton attach can allocate under the allocator semaphore) are tail-called on
// the real context, never nested; 0x31ca20's result is restored by a host trampoline afterwards.
#include "drakan_dual_wield.h"
#include "drakan_pose_mirror.h"
#include "gameui/DualWield.h"
#include "drakan_config.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_dual_wield {
namespace {
using namespace gameui::dualwield;

constexpr uint32_t kFnSlotForItem = 0x344cb8u, kFnAttachEquipped = 0x31ca20u, kFnHolderSwing = 0x49c590u;
constexpr uint32_t kFnItemDetails = 0x342f90u; // controller vtable +0x2c: (ctl, cell) -> the details text buffers
constexpr uint32_t kFnWeaponSwing = 0x49a538u; // (weapon data, live): blade bit, swish sound, clears the hit list
constexpr uint32_t kFnEquip = 0x31e460u;       // (player, inventory cell or -1, equipment slot, flags)
constexpr uint32_t kFnBoneAttach = 0x13b878u;  // *0x4fd57c: (model, item, bone, 0, 0)
constexpr uint32_t kFnBoneDetach = 0x13b8c8u;  // *0x4fd580: (model, bone) -> detached item
constexpr uint32_t kFnHideItem = 0x34d068u, kFnShowItem = 0x34d1f8u; // (item data), updates scene membership too
constexpr uint32_t kFnModelVisible = 0x17e418u; // (model, visible), the attachment visibility setter
constexpr uint32_t kContext = 0x5565d4u;
constexpr uint32_t kCtl = 0x584u, kRightHolder = 0xeb4u;
// Inventory controller (player+0x584): shop mode, selection, slot records (16 bytes, item at +8), the range line of
// the details text (0x40 bytes).
constexpr uint32_t kCtlShopMode = 0xcu, kCtlSelection = 0x20u, kCtlSlots = 0x30u, kCtlRangeLine = 0x384u;
constexpr uint32_t kCtlLineSize = 0x40u;
constexpr int32_t kGridCells = 24;
constexpr uint32_t kHolderRef = 0x1cu;    // u16 registry ref of the holder's weapon
constexpr uint32_t kPropLeftHand = 0x46u; // bone index property of Rynn's model
constexpr uint32_t kWeaponFlags = 0x1cu, kWeaponOwner = 0x60u, kWeaponHitCount = 0x1a0u; // weapon data
constexpr uint32_t kBladeLive = 8u, kBladeHit = 0x10u;
// Pose (docs/reference/combat-controls.md "Dual wielding"): skinning entry and its call from the pose update, the
// model resource the update publishes, the skeleton's pose records, channels, attachments and the inverse bind table.
constexpr uint32_t kFnSkin = 0x1165b0u, kSkinFromPoseUpdate = 0x103890u, kPoseResource = 0x4da734u;
// The actor move (model, world delta, matrix or 0) and its two calls from the root-motion step 0x1198d0.
constexpr uint32_t kFnMoveActor = 0x17ea70u, kMoveFromRootMotion = 0x11a32cu, kMoveFromRootMotion2 = 0x11a424u;
constexpr uint32_t kModelMatrix = 0x20u; // 3x3, rows, column vectors (world = M * d)
constexpr uint32_t kSkFlags = 0x8u, kSkPoseFinal = 0x100000u;
constexpr uint32_t kSkPose = 0x10cu, kSkPoseCount = 0x114u, kPoseStride = 0x6cu, kPoseBindIndex = 0x68u;
constexpr uint32_t kSkChannels = 0xe8u, kSkChannelCount = 0xf0u, kChannelStride = 0x80u;
constexpr uint32_t kChannelNode = 0x4u, kChannelAttach = 0x8u, kChannelExport = 0x18u, kChannelClip = 0x50u,
                   kChannelTime = 0x58u;
constexpr uint32_t kSkAttach = 0xf4u, kSkAttachCount = 0xfcu, kAttachStride = 0x6cu;
constexpr uint32_t kAttachMatrix = 0xcu, kAttachLocal = 0x3cu;
constexpr uint32_t kResBind = 0xc8u, kBindStride = 0x40u;
constexpr uint32_t kPlayerState = 0x94u, kAttacking = 0x80u, kComboStep = 0xb1cu;

bool g_log = false, g_trace = false; // DRAKAN_DUAL_WIELD_LOG=1 events, =2 also every attack frame
PS2Runtime::RecompiledFunction g_origSlot = nullptr, g_origAttach = nullptr, g_origSwing = nullptr,
                               g_origDetails = nullptr, g_origSkin = nullptr, g_origMove = nullptr;

// Left-handed strikes, EE game thread only.
struct PoseState {
    pose::Mirror mirror;
    uint32_t mirrorRes = 0; // the model resource the constants were built from
    pose::StrikeTracker strikes;
    float weight = 0; // 0 native pose .. 1 mirrored
    std::chrono::steady_clock::time_point last{};
} g_pose;

// 0x31ca20's return travels through the trampoline (a free slot of the function table) past the tail-called helper.
uint32_t g_trampoline = 0;
struct Return {
    bool busy = false;
    uint32_t ra = 0;
    __m128i v0{};
    uint32_t stowPlayer = 0; // stow the off-hand after losing the primary weapon
    uint32_t detachModel = 0, detachBone = 0;
} g_return; // EE game thread only

class Ram {
public:
    explicit Ram(uint8_t* rdram) : m_(rdram) {}
    static bool ok(uint32_t a) { return a >= 0x100000u && a < PS2_RAM_SIZE - 0x10u; }
    uint32_t u32(uint32_t a) const {
        uint32_t v = 0;
        if (ok(a & 0x1fffffffu))
            std::memcpy(&v, m_ + (a & 0x1fffffffu), 4);
        return v;
    }
    uint8_t u8(uint32_t a) const { return ok(a & 0x1fffffffu) ? m_[a & 0x1fffffffu] : 0; }
    uint16_t u16(uint32_t a) const {
        uint16_t v = 0;
        if (ok(a & 0x1fffffffu))
            std::memcpy(&v, m_ + (a & 0x1fffffffu), 2);
        return v;
    }
    void put32(uint32_t a, uint32_t v) {
        if (ok(a & 0x1fffffffu))
            std::memcpy(m_ + (a & 0x1fffffffu), &v, 4);
    }
    float f32(uint32_t a) const {
        float v = 0;
        if (ok(a & 0x1fffffffu))
            std::memcpy(&v, m_ + (a & 0x1fffffffu), 4);
        return v;
    }
    // {position[3], rotation[3][3]}, the layout of pose records, bind entries and attachment matrices.
    pose::Affine affine(uint32_t a) const {
        pose::Affine m;
        for (int i = 0; i < 3; ++i)
            m.p[i] = f32(a + uint32_t(i) * 4);
        for (int i = 0; i < 9; ++i)
            m.r[i] = f32(a + 0xc + uint32_t(i) * 4);
        return m;
    }
    void putAffine(uint32_t a, const pose::Affine& m) {
        if (!ok(a & 0x1fffffffu) || !ok((a + 0x30) & 0x1fffffffu))
            return;
        std::memcpy(m_ + (a & 0x1fffffffu), m.p, 12);
        std::memcpy(m_ + ((a + 0xc) & 0x1fffffffu), m.r, 36);
    }
    void putF32(uint32_t a, float v) {
        if (ok(a & 0x1fffffffu))
            std::memcpy(m_ + (a & 0x1fffffffu), &v, 4);
    }
    void put8(uint32_t a, uint8_t v) {
        if (ok(a & 0x1fffffffu))
            m_[a & 0x1fffffffu] = v;
    }
    void put16(uint32_t a, uint16_t v) {
        if (ok(a & 0x1fffffffu))
            std::memcpy(m_ + (a & 0x1fffffffu), &v, 2);
    }

    uint32_t ctx() const { return u32(kContext); }
    // Registry ref (1-based, *(ctx+0x7c) table of {object, count}) -> object, 0 when stale.
    uint32_t object(uint32_t ref) const {
        const uint32_t c = ctx();
        if (!ref || ref > u32(c + 0x84) + 1)
            return 0;
        return u32(u32(c + 0x7c) + (ref - 1) * 8);
    }
    // Cam_GetObjFromHandle: an actor's data object.
    uint32_t data(uint32_t h) const {
        const uint32_t p = h ? u32(h + 0xfc) : 0;
        return p ? u32(p + 0x10) : 0;
    }
    uint32_t prop(uint32_t h, uint32_t id) const {
        const uint32_t pb = u32(h + 0x54);
        return pb ? u32(u32(pb + 4) + id * 4) : ~0u;
    }
    uint32_t cls(uint32_t h) const {
        const uint32_t ix = u16(h + 0x10), db = u32(ctx() + 0x320);
        if (ix == 0xffffu || !db || ix > u32(db + 0x10))
            return 0;
        return u32(u32(db + 8) + ix * 0x24 + 8);
    }
    Item item(uint32_t h) const {
        Item i;
        if (!ok(h))
            return i;
        i.handle = h;
        i.type = u32(h + 0xc);
        i.cls = cls(h);
        i.stance = i.melee() ? prop(h, kPropStance) : ~0u;
        return i;
    }
    uint32_t rynn() const {
        const uint32_t actor = u32(ctx() + 0x4b0);
        return actor ? data(actor) : 0;
    }
    uint32_t slot(uint32_t player, uint32_t n) const { return u32(player + kPlayerSlots + n * kSlotStride); }
    uint32_t holderWeapon(uint32_t holder) const { return object(u16(holder + kHolderRef)); }
    // The item attached to a bone of a model's skeleton (the lookup of the skeleton's detach, 0x103df0).
    uint32_t onBone(uint32_t model, uint32_t bone) const {
        const uint32_t sk = u32(model + 0x50);
        if (!sk || !(u32(sk + 8) & 8u))
            return 0;
        const int16_t ix = int16_t(u16(u32(sk + 0xdc) + bone * 2));
        const uint32_t rec = u32(sk + 0xe8) + uint32_t(int32_t(ix) * 0x80);
        if (u32(rec + 4) != bone || u32(rec + 8) == ~0u)
            return 0;
        return object(u16(u32(sk + 0xf4) + u32(rec + 8) * 0x6c + 4));
    }
    // Takes a reference on an object that already has a registry entry (the part of 0x168388 that cannot allocate).
    uint16_t acquire(uint32_t h) {
        const uint32_t ix = u16(h + 0x12);
        if (!ix)
            return 0;
        const uint32_t e = u32(ctx() + 0x7c) + (ix - 1) * 8;
        put16(e + 4, uint16_t(u16(e + 4) + 1));
        return uint16_t(ix);
    }
    // Drops a reference unless it is the last one (freeing the entry is left to native code; at worst one entry stays).
    void release(uint16_t ref) {
        if (!ref || ref > u32(ctx() + 0x84) + 1)
            return;
        const uint32_t e = u32(ctx() + 0x7c) + (ref - 1) * 8;
        if (u16(e + 4) > 1)
            put16(e + 4, uint16_t(u16(e + 4) - 1));
    }

private:
    uint8_t* m_;
};

void dlog(const char* fmt, uint32_t a, uint32_t b = 0, uint32_t c = 0) {
    if (g_log)
        std::fprintf(stderr, fmt, a, b, c);
}

// Calls the original on the real context; true when it ran to completion (it returned to the caller).
bool runOriginal(PS2Runtime::RecompiledFunction fn, uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    fn(rdram, c, rt);
    return c->pc == ra;
}

SlotQuery query(const Ram& r, uint32_t player, uint32_t handle, int32_t nativeSlot) {
    SlotQuery q;
    q.enabled = enabled();
    q.nativeSlot = nativeSlot;
    q.item = r.item(handle);
    q.right = r.item(r.slot(player, kSlotRight));
    q.offhand = r.item(r.slot(player, kSlotOffhand));
    q.bowDrawn = r.u32(player + kPlayerLeftHolder) == kBowMode;
    return q;
}

void hookSlotForItem(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ctl = getRegU32(c, 4), handle = getRegU32(c, 5);
    if (!runOriginal(g_origSlot, rdram, c, rt))
        return;
    const Ram r(rdram);
    const uint32_t player = ctl - kCtl;
    if (!handle || player != r.rynn())
        return;
    const SlotQuery q = query(r, player, handle, int32_t(getRegU32(c, 2)));
    const int32_t slot = chooseSlot(q);
    if (slot != q.nativeSlot) {
        SET_GPR_S32(c, 2, slot);
        dlog("[dual-wield] item %x -> slot %u (native %u)\n", handle, uint32_t(slot), uint32_t(q.nativeSlot));
    }
}

// The native details panel (screen 0x15, and the modern screens, which show the same lines) gets a hand line in the
// buffer of the bow's range, which the panel draws under Speed and melee weapons leave empty.
// Writes the hand line for `cell` (-1: the selection) into the details text.
void writeHandLine(Ram& r, uint32_t ctl, int32_t arg) {
    const uint32_t player = ctl - kCtl;
    if (player != r.rynn() || r.u32(ctl + kCtlShopMode))
        return;
    const int32_t cell = arg < 0 ? int32_t(r.u32(ctl + kCtlSelection)) : arg;
    const bool grid = cell >= 0 && cell < kGridCells; // else a spell or the shop stock: only clear a line of ours
    const uint32_t handle = grid ? r.u32(ctl + kCtlSlots + uint32_t(cell) * kSlotStride + 8) : 0;
    // The builder may leave the line as it was: a line of ours from an earlier item counts as free.
    char line[kCtlLineSize] = {};
    for (uint32_t i = 0; i + 1 < kCtlLineSize; ++i)
        line[i] = char(r.u8(ctl + kCtlRangeLine + i));
    if (line[0] && !isHandLine(line)) // the native line (a bow's range)
        return;
    const char* text = handLine(query(r, player, handle, kSlotRight));
    for (uint32_t i = 0; i < kCtlLineSize; ++i) {
        r.put8(ctl + kCtlRangeLine + i, uint8_t(text[i]));
        if (!text[i])
            break;
    }
}

void hookItemDetails(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ctl = getRegU32(c, 4);
    const int32_t arg = int32_t(getRegU32(c, 5));
    if (!runOriginal(g_origDetails, rdram, c, rt))
        return;
    Ram r(rdram);
    writeHandLine(r, ctl, arg);
}

// A node's assigned channel, not the first channel (which is locomotion during moving attacks).
uint32_t nodeChannel(const Ram& r, uint32_t sk, uint32_t node) {
    const uint32_t table = r.u32(sk + 0xdc), channels = r.u32(sk + kSkChannels);
    const int16_t index = int16_t(r.u16(table + node * 2));
    return Ram::ok(table) && Ram::ok(channels) && index >= 0 && uint32_t(index) < r.u32(sk + kSkChannelCount)
               ? channels + uint32_t(index) * kChannelStride
               : 0;
}

bool splitLocomotion(const Ram& r, uint32_t sk) {
    const uint32_t root = nodeChannel(r, sk, 0), attack = nodeChannel(r, sk, 2);
    return root && attack && r.u32(root + kChannelClip) != r.u32(attack + kChannelClip);
}

// Mirrors Rynn's final pose for a left-handed strike (the weight blends in / out). Runs before the skinning reads it.
void mirrorPose(Ram& r, uint32_t sk) {
    const uint32_t player = r.rynn();
    const uint32_t model = Ram::ok(player) ? r.u32(player + 0x80) : 0;
    if (!Ram::ok(model) || sk != r.u32(model + 0x50) || !(r.u32(sk + kSkFlags) & kSkPoseFinal))
        return;
    const auto now = std::chrono::steady_clock::now();
    const bool first = g_pose.last == std::chrono::steady_clock::time_point{};
    const float dt = first ? 0.0f : std::fmin(0.1f, std::chrono::duration<float>(now - g_pose.last).count());
    g_pose.last = now;
    const uint32_t channels = r.u32(sk + kSkChannels), nch = r.u32(sk + kSkChannelCount);
    const bool attacking = (r.u32(player + kPlayerState) & kAttacking) != 0;
    const uint32_t attack = nodeChannel(r, sk, 2);
    const bool locomotion = splitLocomotion(r, sk);
    const bool left = g_pose.strikes.update(attacking, r.u32(attack + kChannelClip), r.u32(player + kComboStep),
                                            r.f32(attack + kChannelTime), dt);
    // Only with a weapon in the left hand (equipped in the shield slot and attached to the left hand bone).
    const Item offhand = r.item(r.slot(player, kSlotOffhand));
    const bool armed = enabled() && offhand.melee() && r.onBone(model, r.prop(model, kPropLeftHand)) == offhand.handle;
    const float before = g_pose.weight;
    g_pose.weight = pose::approach(g_pose.weight, armed && left, dt);
    if (g_trace && attacking) {
        std::fprintf(stderr, "[dual-wield] pose clip=%x t=%.3f step=%u left=%d armed=%d weight=%.2f\n",
                     r.u32(attack + kChannelClip), double(r.f32(attack + kChannelTime)), r.u32(player + kComboStep),
                     int(left), int(armed), double(g_pose.weight));
        // The clip each body part plays (node -> channel): root, spine, right / left leg, right / left arm.
        auto clipOf = [&](uint32_t node) {
            const int16_t ch = int16_t(r.u16(r.u32(sk + 0xdcu) + node * 2));
            return ch >= 0 && uint32_t(ch) < nch ? r.u32(channels + uint32_t(ch) * kChannelStride + kChannelClip) : 0u;
        };
        std::fprintf(stderr, "[dual-wield] parts root=%x spine=%x rleg=%x lleg=%x rarm=%x larm=%x\n", clipOf(0),
                     clipOf(2), clipOf(54), clipOf(60), clipOf(34), clipOf(16));
    }
    if (g_pose.weight <= 0)
        return;
    if (before <= 0)
        dlog("[dual-wield] left-handed strike (clip %x, step %u)\n", r.u32(channels + kChannelClip),
             r.u32(player + kComboStep));

    const uint32_t records = r.u32(sk + kSkPose), res = r.u32(kPoseResource);
    const uint32_t bind = Ram::ok(res) ? r.u32(res + kResBind) : 0;
    if (r.u32(sk + kSkPoseCount) != uint32_t(pose::kNodes) || !Ram::ok(records) || !Ram::ok(bind) || nch > 64)
        return;
    pose::Affine in[pose::kNodes], out[pose::kNodes];
    for (int i = 0; i < pose::kNodes; ++i)
        in[i] = r.affine(records + uint32_t(i) * kPoseStride);
    if (g_pose.mirrorRes != res) {
        pose::Affine ib[pose::kNodes];
        int bindIndex[pose::kNodes];
        for (int i = 0; i < pose::kNodes; ++i) {
            ib[i] = r.affine(bind + uint32_t(i) * kBindStride);
            bindIndex[i] = int(r.u32(records + uint32_t(i) * kPoseStride + kPoseBindIndex));
        }
        g_pose.mirrorRes = res;
        if (!g_pose.mirror.build(ib, pose::kNodes, bindIndex))
            std::fprintf(stderr, "[dual-wield] Rynn's bind pose is not mirrorable: left-handed strikes off\n");
    }
    if (!g_pose.mirror.valid)
        return;
    // Preserve locomotion exactly, including on the frame a strafe starts. Mirroring the legs even briefly reverses
    // the direction of their stride. The attack is reflected about the native pelvis instead.
    pose::applySplit(g_pose.mirror, in, g_pose.weight, locomotion ? 0.0f : g_pose.weight, out);
    if (g_trace && attacking) {
        float lowerError = 0;
        for (int i = 0; i < pose::kNodes; ++i)
            if (pose::isLower(i)) {
                for (int j = 0; j < 3; ++j)
                    lowerError = std::fmax(lowerError, std::fabs(in[i].p[j] - out[i].p[j]));
                for (int j = 0; j < 9; ++j)
                    lowerError = std::fmax(lowerError, std::fabs(in[i].r[j] - out[i].r[j]));
            }
        std::fprintf(stderr, "[dual-wield] locomotion split=%d lowerError=%.6f\n", int(locomotion), double(lowerError));
    }
    for (int i = 0; i < pose::kNodes; ++i) {
        r.putAffine(records + uint32_t(i) * kPoseStride, out[i]);
    }
    if (g_trace) // the grips: the new left one sits where the right one was, reflected in x (and vice versa)
        std::fprintf(stderr,
                     "[dual-wield] grips w=%.2f native L(%.0f %.0f %.0f) R(%.0f %.0f %.0f) -> L(%.0f %.0f %.0f) "
                     "R(%.0f %.0f %.0f)\n",
                     double(g_pose.weight), double(in[19].p[0]), double(in[19].p[1]), double(in[19].p[2]),
                     double(in[37].p[0]), double(in[37].p[1]), double(in[37].p[2]), double(out[19].p[0]),
                     double(out[19].p[1]), double(out[19].p[2]), double(out[37].p[0]), double(out[37].p[1]),
                     double(out[37].p[2]));
    // What the pose update already derived from the native pose (0x10b100): the attachment matrices (local * node)
    // and the node copies some channels export (the right grip to player+0x54c, the head, the spine).
    const uint32_t atts = r.u32(sk + kSkAttach), natt = r.u32(sk + kSkAttachCount);
    for (uint32_t c = 0; c < nch; ++c) {
        const uint32_t ch = channels + c * kChannelStride;
        const uint32_t node = r.u32(ch + kChannelNode);
        if (node >= uint32_t(pose::kNodes))
            continue;
        const uint32_t a = r.u32(ch + kChannelAttach);
        if (a < natt) {
            const uint32_t att = atts + a * kAttachStride;
            r.putAffine(att + kAttachMatrix, pose::mul(r.affine(att + kAttachLocal), out[node]));
        }
        if (const uint32_t exported = r.u32(ch + kChannelExport); Ram::ok(exported))
            r.putAffine(exported, out[node]);
    }
}

void stowOffhand(Ram& r, R5900Context* c, uint32_t player, uint32_t model, uint32_t bone) {
    const Item offhand = r.item(r.slot(player, kSlotOffhand));
    if (!offhand.melee() || !g_trampoline || g_return.busy || bone >= 0x100u)
        return;
    const uint32_t weapon = r.data(offhand.handle);
    if (!weapon)
        return;
    g_return.busy = true;
    g_return.ra = getRegU32(c, 31);
    g_return.v0 = c->r[2];
    g_return.stowPlayer = player;
    g_return.detachModel = model;
    g_return.detachBone = bone;
    SET_GPR_U32(c, 4, weapon);
    SET_GPR_U32(c, 5, 0);
    SET_GPR_U32(c, 31, g_trampoline);
    c->pc = kFnWeaponSwing;
    dlog("[dual-wield] stow %x (disabled or primary removed)\n", offhand.handle);
    return;
}

void hookSkin(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t sk = getRegU32(c, 4);
    // A real entry from the pose update (not 0x106aa8's re-skin, not a resume), before the palette is built.
    if (c->pc == kFnSkin && getRegU32(c, 31) == kSkinFromPoseUpdate) {
        Ram r(rdram);
        mirrorPose(r, getRegU32(c, 4));
    }
    if (!runOriginal(g_origSkin, rdram, c, rt) || g_return.busy)
        return;
    // Settings are applied on the host UI thread. Consume the disabled state here on the EE thread, through the
    // native unequip path. This also reconciles a save loaded with an off-hand while the option is disabled.
    Ram r(rdram);
    const uint32_t player = r.rynn(), model = Ram::ok(player) ? r.u32(player + 0x80) : 0;
    if (Ram::ok(model) && sk == r.u32(model + 0x50)) {
        const uint32_t bone = r.prop(model, kPropLeftHand);
        const Item offhand = r.item(r.slot(player, kSlotOffhand));
        const uint32_t leftHolder = r.holderWeapon(player + kPlayerLeftHolder);
        // Bow drawing finishes after the equipped-items hook. Reconcile at the completed pose as well, once the
        // holder actually owns the bone (also repairs a save made in the stale attached state).
        if (bone < 0x100u && offhand.melee()) {
            const uint32_t onBone = r.onBone(model, bone);
            const bool hiddenByBow = leftHolder && offhand.handle != leftHolder && onBone == leftHolder;
            if (hiddenByBow)
                r.put32(offhand.handle + 4, r.u32(offhand.handle + 4) & ~0x400000u);
            if (hiddenByBow && (r.u32(offhand.handle + 4) & 1u)) {
                // Equipped models can also be drawn outside the scene lists. Clear the native model visibility
                // through its setter; skeleton attach restores it from Rynn when returning to melee.
                g_return.busy = true;
                g_return.ra = getRegU32(c, 31);
                g_return.v0 = c->r[2];
                SET_GPR_U32(c, 4, offhand.handle);
                SET_GPR_U32(c, 5, 0);
                SET_GPR_U32(c, 31, g_trampoline);
                c->pc = kFnModelVisible;
                return;
            }
            const uint32_t visibility = r.u32(offhand.handle + 4) & 0x3cu;
            const uint32_t fn = hiddenByBow && visibility ? kFnHideItem
                                : !leftHolder && onBone == offhand.handle && enabled() && visibility != 0x28u
                                    ? kFnShowItem
                                    : 0;
            if (fn && r.data(offhand.handle)) {
                // Changing only flags leaves the model in the scene's render lists. Use the native item
                // hide/show operations, retaining the equipment slot and restoring visibility after reattach.
                g_return.busy = true;
                g_return.ra = getRegU32(c, 31);
                g_return.v0 = c->r[2];
                SET_GPR_U32(c, 4, r.data(offhand.handle));
                SET_GPR_U32(c, 31, g_trampoline);
                c->pc = fn;
                return;
            }
        }
        if (!enabled())
            stowOffhand(r, c, player, model, bone);
    }
}

void trampoline(uint8_t* rdram, R5900Context* c, PS2Runtime*) {
    if (g_return.stowPlayer) {
        SET_GPR_U32(c, 4, g_return.stowPlayer);
        SET_GPR_S32(c, 5, -1);
        SET_GPR_U32(c, 6, kSlotOffhand);
        SET_GPR_U32(c, 7, 0);
        g_return.stowPlayer = 0;
        SET_GPR_U32(c, 31, g_trampoline);
        c->pc = kFnEquip;
        return;
    }
    if (g_return.detachModel) {
        Ram r(rdram);
        const uint32_t model = g_return.detachModel, bone = g_return.detachBone;
        g_return.detachModel = 0;
        // If native unequip deferred (e.g. during a transition), leave the attachment until a later update retries.
        if (!r.item(r.slot(r.rynn(), kSlotOffhand)).melee() && r.item(r.onBone(model, bone)).melee()) {
            SET_GPR_U32(c, 4, model);
            SET_GPR_U32(c, 5, bone);
            SET_GPR_U32(c, 31, g_trampoline);
            c->pc = kFnBoneDetach;
            return;
        }
    }
    c->r[2] = g_return.v0;
    SET_GPR_U32(c, 31, g_return.ra);
    c->pc = g_return.ra;
    g_return.busy = false;
}

void hookAttachEquipped(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t player = getRegU32(c, 4), mask = getRegU32(c, 5);
    if (!runOriginal(g_origAttach, rdram, c, rt))
        return;
    Ram r(rdram);
    if (player != r.rynn())
        return;
    // An equip changed the hands: the selection's details line (built before) says what using it does now.
    writeHandLine(r, player + kCtl, -1);
    // Drawing/stowing the bow and switching the right weapon also change the left-hand attachment. The native
    // bow path preserves a melee item in slot 2, so restore it when the bow releases the hand.
    if (!(mask & (2u | 4u | 8u)) || !g_trampoline || g_return.busy)
        return;
    const uint32_t model = r.u32(player + 0x80);
    const uint32_t bone = r.prop(model, kPropLeftHand);
    if (!Ram::ok(model) || bone >= 0x100u)
        return;
    const Item offhand = r.item(r.slot(player, kSlotOffhand));
    const Item onBone = r.item(r.onBone(model, bone));
    const uint32_t leftHolder = r.holderWeapon(player + kPlayerLeftHolder);
    // Native bow attach replaces the bone entry without detaching our slot-2 melee weapon. Clear the displaced
    // item's attached flag just as 0x13b8c8 does, or it keeps rendering at its last hand position. Keep the inventory
    // assignment: the normal melee return below reattaches it and restores the flag through 0x13b878.
    if (offhand.melee() && leftHolder && onBone.handle == leftHolder && offhand.handle != onBone.handle)
        r.put32(offhand.handle + 4, r.u32(offhand.handle + 4) & ~0x400000u);
    // The native attack controller needs a primary weapon. Dropping it otherwise leaves an unusable weapon marked
    // equipped in the left hand. Put that weapon away through the native inventory path, keeping it available to
    // equip normally. The bow is temporary: preserve the off-hand assignment while its holder owns the left hand.
    if (offhand.melee() && !r.slot(player, kSlotRight) && !r.holderWeapon(player + kPlayerLeftHolder)) {
        stowOffhand(r, c, player, model, bone);
        return;
    }
    const BoneAction action = boneAction(offhand, onBone, leftHolder);
    if (action == BoneAction::None)
        return;
    if (action == BoneAction::Attach) {
        // As the hand holders do (0x49cda0): the weapon's owner is Rynn's model, which its blade sweep (0x4979a8)
        // excludes and the hits are credited to.
        const uint32_t w = r.data(offhand.handle);
        if (!w)
            return;
        const uint16_t want = uint16_t(r.u16(model + 0x12));
        if (r.u16(w + kWeaponOwner) != want) {
            const uint16_t ref = r.acquire(model);
            if (!ref)
                return;
            r.release(r.u16(w + kWeaponOwner));
            r.put16(w + kWeaponOwner, ref);
        }
        SET_GPR_U32(c, 4, model);
        SET_GPR_U32(c, 5, offhand.handle);
        SET_GPR_U32(c, 6, bone);
        SET_GPR_U32(c, 7, 0);
        SET_GPR_U32(c, 8, 0);
        dlog("[dual-wield] attach %x to bone %u (on bone: %x)\n", offhand.handle, bone, onBone.handle);
        if (onBone.handle) // the attach replaces the bone's entry; the old item is no longer attached
            r.put32(onBone.handle + 4, r.u32(onBone.handle + 4) & ~0x400000u);
        c->pc = kFnBoneAttach;
    } else {
        SET_GPR_U32(c, 4, model);
        SET_GPR_U32(c, 5, bone);
        dlog("[dual-wield] detach %x from bone %u\n", onBone.handle, bone);
        c->pc = kFnBoneDetach;
    }
    g_return.busy = true;
    g_return.ra = getRegU32(c, 31);
    g_return.v0 = c->r[2];
    SET_GPR_U32(c, 31, g_trampoline);
}

void hookHolderSwing(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t holder = getRegU32(c, 4);
    Ram r(rdram);
    const uint32_t player = holder - kRightHolder;
    // With a weapon in the left hand the native window on the right hand holder goes to the hand that strikes: the
    // right weapon's request is withheld during a left-handed (mirrored) strike, before the native call.
    uint32_t left = 0;
    if (player == r.rynn()) {
        const Item offhand = r.item(r.slot(player, kSlotOffhand));
        const uint32_t model = r.u32(player + 0x80);
        if (offhand.melee() && r.onBone(model, r.prop(model, kPropLeftHand)) == offhand.handle)
            left = r.data(offhand.handle);
    }
    const bool leftStrike = enabled() && left && g_pose.mirror.valid && g_pose.strikes.attacking && g_pose.strikes.left;
    const BladePlan plan = bladePlan(getRegU32(c, 5) != 0, leftStrike);
    if (left)
        SET_GPR_U32(c, 5, plan.right ? 1u : 0u);
    if (!runOriginal(g_origSwing, rdram, c, rt) || !left)
        return;
    const uint32_t right = r.data(r.holderWeapon(holder));
    const bool rightLive = right && (r.u32(right + kWeaponFlags) & kBladeLive);
    const bool leftLive = (r.u32(left + kWeaponFlags) & kBladeLive) != 0;
    if (g_trace) { // for the harness (scripts/harness/dual_wield_check.py)
        static int s_traced = -1;
        const int pair = int(rightLive) * 4 + int(leftLive) * 2 + int(leftStrike);
        if (s_traced != pair) {
            s_traced = pair;
            std::fprintf(stderr, "[dual-wield] blade right=%d left=%d strike=%s\n", int(rightLive), int(leftLive),
                         leftStrike ? "left" : "right");
        }
        // Targets hit in the current window (the blade's hit list, cleared when the blade goes dead).
        static uint32_t s_hits[2] = {};
        const uint32_t hits[2] = {right ? r.u32(right + kWeaponHitCount) : 0, r.u32(left + kWeaponHitCount)};
        for (int h = 0; h < 2; ++h) {
            if (hits[h] > s_hits[h] && hits[h] < 64)
                std::fprintf(stderr, "[dual-wield] hit hand=%s strike=%s targets=%u\n", h ? "left" : "right",
                             leftStrike ? "left" : "right", hits[h]);
            s_hits[h] = hits[h];
        }
    }
    if (leftLive == plan.left)
        return;
    if (!plan.left) // the window closed (or a hit consumed it): the off-hand's own hit mark goes with it
        r.put32(left + kWeaponFlags, r.u32(left + kWeaponFlags) & ~kBladeHit);
    // 0x49c590 returns nothing; the tail call returns straight to its caller.
    SET_GPR_U32(c, 4, left);
    SET_GPR_U32(c, 5, plan.left ? 1u : 0u);
    c->pc = kFnWeaponSwing;
}

// Root motion of a mirrored strike lunges to the mirrored side. Pre-hook on the actor move (0x17ea70) when the pose
// update's root-motion step calls it for Rynn; the weight is the previous update's (one update late, while it ramps).
void hookMoveActor(uint8_t* rdram, R5900Context* c, PS2Runtime* rt) {
    const uint32_t ra = getRegU32(c, 31);
    if (c->pc == kFnMoveActor && (ra == kMoveFromRootMotion || ra == kMoveFromRootMotion2) && g_pose.weight > 0 &&
        getRegU32(c, 6) == 0) {
        Ram r(rdram);
        const uint32_t model = getRegU32(c, 4), delta = getRegU32(c, 5);
        const uint32_t player = r.rynn();
        if (Ram::ok(player) && model == r.u32(player + 0x80) && Ram::ok(delta)) {
            float m[9], d[3];
            for (int i = 0; i < 9; ++i)
                m[i] = r.f32(model + kModelMatrix + uint32_t(i) * 4);
            for (int i = 0; i < 3; ++i)
                d[i] = r.f32(delta + uint32_t(i) * 4);
            const float before[3] = {d[0], d[1], d[2]};
            const bool locomotion = splitLocomotion(r, r.u32(model + 0x50));
            const float weight = locomotion ? 0.0f : g_pose.weight;
            if (pose::mirrorRootDelta(m, d, weight)) {
                for (int i = 0; i < 3; ++i)
                    r.putF32(delta + uint32_t(i) * 4, d[i]);
                if (g_trace && (before[0] != 0 || before[2] != 0))
                    std::fprintf(stderr, "[dual-wield] root w=%.2f world (%.1f %.1f %.1f) -> (%.1f %.1f %.1f)\n",
                                 double(weight), double(before[0]), double(before[1]), double(before[2]), double(d[0]),
                                 double(d[1]), double(d[2]));
            }
        }
    }
    g_origMove(rdram, c, rt);
}

// A slot of the dense function table that no guest code can reach (no function there), for the trampoline.
uint32_t freeTableSlot(PS2Runtime& rt) {
    for (uint32_t a = 0x100000u; a < 0x100100u; a += 4)
        if (!rt.hasFunction(a) && rt.registerFunction(a, &trampoline))
            return a;
    return 0;
}
} // namespace

void setEnabled(bool on) {
    if (g_enabled.exchange(on) != on)
        std::fprintf(stderr, "[dual-wield] %s\n", on ? "on" : "off");
}

void install(PS2Runtime& runtime) {
    const char* log = std::getenv("DRAKAN_DUAL_WIELD_LOG");
    g_log = log != nullptr;
    g_trace = log && std::atoi(log) >= 2;
    for (uint32_t a : {kFnSlotForItem, kFnAttachEquipped, kFnHolderSwing, kFnWeaponSwing, kFnBoneAttach, kFnBoneDetach,
                       kFnItemDetails, kFnSkin, kFnMoveActor})
        if (!runtime.hasFunction(a)) {
            std::fprintf(stderr, "[dual-wield] native function %x missing: not installed\n", a);
            return;
        }
    g_trampoline = freeTableSlot(runtime);
    if (!g_trampoline) {
        std::fprintf(stderr, "[dual-wield] no free function-table slot for the trampoline: not installed\n");
        return;
    }
    g_origSlot = runtime.lookupFunction(kFnSlotForItem);
    g_origAttach = runtime.lookupFunction(kFnAttachEquipped);
    g_origDetails = runtime.lookupFunction(kFnItemDetails);
    g_origSkin = runtime.lookupFunction(kFnSkin);
    g_origMove = runtime.lookupFunction(kFnMoveActor);
    g_origSwing = runtime.lookupFunction(kFnHolderSwing);
    runtime.replaceFunction(kFnSlotForItem, &hookSlotForItem);
    runtime.replaceFunction(kFnAttachEquipped, &hookAttachEquipped);
    runtime.replaceFunction(kFnItemDetails, &hookItemDetails);
    runtime.replaceFunction(kFnSkin, &hookSkin);
    runtime.replaceFunction(kFnMoveActor, &hookMoveActor);
    runtime.replaceFunction(kFnHolderSwing, &hookHolderSwing);
    g_enabled.store(drakan_cfg::getBool("general", "dual_wield", "DRAKAN_DUAL_WIELD", false));
    std::fprintf(stderr, "[dual-wield] hooks installed (trampoline %x), %s\n", g_trampoline,
                 g_enabled.load() ? "on" : "off");
}
} // namespace drakan_dual_wield
