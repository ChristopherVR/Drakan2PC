#include "runtime/object_motion_tracker.h"
#include "runtime/object_motion_capture.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <fstream>
#include <cmath>
#include <vector>
using namespace drakan_object_motion;
static int checkCount = 0;
static void check(bool ok, const char* what) {
    ++checkCount;
    if (!ok) {
        std::fprintf(stderr, "FAIL %d: %s\n", checkCount, what);
        std::exit(1);
    }
}
static Scope obj(Tracker& t, uint32_t a = 0x800008, uint32_t mesh = 0x900000) {
    return t.object(a, a - 8, 0x200, 0x123400000200ull, mesh, 0x1485c8);
}
static Scope part(Tracker& t, const Scope& p, uint32_t address = 0x910000, uint64_t hash = 0xabcdef) {
    return t.part(p, address, 0x1ec160, hash, 16, 24, true);
}
static void sink(const Scope& p, void* user) {
    static_cast<std::vector<Scope>*>(user)->push_back(p);
}
int main(int argc, char** argv) {
    Tracker t;
    check(!(obj(t).flags & Scope::Valid), "no epoch means no valid object");
    t.nextFrame();
    auto a = obj(t), b = obj(t, 0x800208);
    check(a.objectId != b.objectId && a.generation == 1 && b.generation == 1,
          "two engine addresses distinguish two objects sharing mesh");
    auto a1 = part(t, a);
    check((a1.flags & Scope::Reset) && !(a1.flags & Scope::TopologyStable), "first topology cannot borrow history");
    t.nextFrame();
    auto a2 = obj(t);
    auto p2 = part(t, a2);
    check(a2.objectId == a.objectId && a2.generation == a.generation, "identity and generation persist across frames");
    check((p2.flags & Scope::TopologyStable) != 0, "same preclip topology in consecutive frame is certified");
    auto other = part(t, a2, 0x910030);
    check(other.partId != p2.partId, "part identity is descriptor address, not draw ordinal");
    auto pass = t.part(a2, 0x910000, 0x1ec8f0, 0xabcdef, 16, 24, true);
    check(pass.passKey != p2.passKey && !(pass.flags & Scope::TopologyStable),
          "different engine passes have independent topology certificates");
    auto duplicate = part(t, a2);
    check((duplicate.flags & Scope::Ambiguous) && !(duplicate.flags & Scope::TopologyStable),
          "duplicate object/part/pass rejects ambiguous correspondence");
    t.nextFrame();
    auto p3 = part(t, obj(t));
    check(!(p3.flags & Scope::TopologyStable), "frame after duplicated key also fails closed");
    t.nextFrame();
    auto changed = part(t, obj(t), 0x910000, 0xcccc);
    check(!(changed.flags & Scope::TopologyStable), "changed original selector bytes reject correspondence");
    t.nextFrame();
    auto changedCount = t.part(obj(t), 0x910000, 0x1ec160, 0xcccc, 32, 24, true);
    check(!(changedCount.flags & Scope::TopologyStable), "changed vertex count rejects correspondence");
    t.nextFrame();
    t.nextFrame();
    auto gap = t.part(obj(t), 0x910000, 0x1ec160, 0xcccc, 32, 24, true);
    check(!(gap.flags & Scope::TopologyStable), "missing engine epoch rejects correspondence");
    t.freed(0x800000, 0x200);
    auto reused = obj(t);
    check(reused.generation == a.generation + 1 && (reused.flags & Scope::Reset),
          "freed allocation reused at same address changes generation");
    auto newMesh = obj(t, 0x800008, 0x920000);
    check(newMesh.objectId == reused.objectId && newMesh.meshId != reused.meshId,
          "LOD/resource replacement has different mesh identity");
    auto unverified = t.part(newMesh, 0x930000, 0x1ec160, 0xcccc, 32, 24, false);
    check(!(unverified.flags & Scope::TopologyStable) && (unverified.flags & Scope::Rejected),
          "unverified input topology cannot be certified");

    // Physical DMAtag provenance maps to offsets of copied VIF commands. A REF
    // payload can follow without changing identity; only actual tag markers do.
    auto marked = part(t, newMesh, 0x940000);
    marked.flags |= Scope::TopologyStable;
    auto marked2 = marked;
    marked2.partId = 0x940030;
    marked2.partAddress = 0x940030;
    t.mark(0xa00100, 0xa00000, 0x50000, marked);
    t.mark(0xa00200, 0xa00000, 0x50000, marked2);
    auto chain = Tracker::Chain(t, marked.sceneSerial);
    chain.tag(0xa00100, 0);
    chain.tag(0xa00110, 24);
    chain.tag(0xa00200, 80);
    auto job = chain.finish(160);
    check(job && job->markers.size() == 2 && job->markers[1].byteOffset == 80,
          "tag spans use copied byte offsets and deduplicate repeated tags");
    check(job->markers[0].scope.submissionSerial == job->markers[1].scope.submissionSerial,
          "one immutable job has one submission serial");
    // EE has already advanced, freed/reused objects and overwritten the buffer
    // before the simulated worker executes its earlier copied job.
    t.nextFrame();
    t.freed(0x800000, 0x200);
    auto later = obj(t);
    auto laterPart = part(t, later, 0x950000);
    t.mark(0xa00100, 0xa00000, 0x50000, laterPart);
    check(job->markers[0].scope.partId == 0x940000 && job->markers[0].scope.generation != later.generation,
          "delayed VIF job does not read current CPU scope or reused buffer");
    std::vector<Scope> output;
    std::thread worker([&] {
        Cursor c;
        c.start(job.get());
        c.command(0, sink, &output);
        c.command(24, sink, &output);
        c.command(80, sink, &output);
        c.close(sink, &output);
    });
    worker.join();
    check(output.size() == 4 && (output[0].flags & Scope::Begin) && (output[1].flags & Scope::End) &&
              output[2].partId == 0x940030 && (output[3].flags & Scope::End),
          "worker emits ordered begin/end around exact part boundaries");
    output.clear();
    Cursor clipped;
    clipped.start(job.get());
    clipped.command(0, sink, &output);
    clipped.clipping(true, sink, &output);
    check(output.size() == 3 && (output.back().flags & Scope::Clipped) &&
              !(output.back().flags & Scope::TopologyStable),
          "clipper enabled makes correspondence ineligible before GIF");
    output.clear();
    Cursor crossed;
    crossed.start(job.get());
    crossed.command(0, sink, &output);
    crossed.command(84, sink, &output);
    check((output.back().flags & Scope::Ambiguous) && !(output.back().flags & Scope::Valid),
          "marker crossed within VIF payload rejects rather than shifts scope");
    auto wrong = Tracker::Chain(t, marked.sceneSerial);
    wrong.tag(0xa00100, 0);
    auto rejected = wrong.finish(16);
    check(rejected && !(rejected->markers[0].scope.flags & Scope::Valid),
          "stale physical-buffer marker from other scene is rejected");
    auto missing = Tracker::Chain(t, later.sceneSerial);
    missing.tag(0x700000, 0);
    auto noOwner = missing.finish(16);
    check(noOwner && !(noOwner->markers[0].scope.flags & Scope::Valid),
          "unmarked/rolled-over buffer does not inherit last part");
    Tracker::Chain disabled(t, 0);
    check(!disabled.finish(128), "disabled/unknown object does not allocate a sidecar job");
    auto rejectedRange = t.object(0x10, 0x20, 0x100, 123, 0x900000, 0);
    check(!(rejectedRange.flags & Scope::Valid), "object outside owning allocation rejected");
    t.reset();
    check(obj(t).generation > later.generation, "mode/reset invalidates old generations");
    Tracker pool;
    pool.nextFrame();
    auto slot = pool.object(0xa5b6a0, 0xa5b6a0, 0x120, 0x51b8b0, 0x900000, 0x1485c8);
    pool.nextFrame();
    auto sameSlot = pool.object(0xa5b6a0, 0xa5b6a0, 0x120, 0x51b8b0, 0x900000, 0x1485c8);
    check(slot.generation == sameSlot.generation && slot.objectId == sameSlot.objectId,
          "real pool-slot address needs no malloc header and persists");
    pool.lifetimeBoundary(0xa5b6a0);
    auto rebuilt = pool.object(0xa5b6a0, 0xa5b6a0, 0x120, 0x51b8b0, 0x900000, 0x1485c8);
    check(rebuilt.generation > slot.generation && (rebuilt.flags & Scope::Reset),
          "destruction/reconstruction of same pool slot rejects old history");
    pool.lifetimeBoundary(0xa5b7c0);
    check(pool.object(0xa5b6a0, 0xa5b6a0, 0x120, 0x51b8b0, 0x900000, 0x1485c8).generation == rebuilt.generation,
          "neighbor slot lifetime does not change this object");
    t.invalidateTags();
    check(!(t.atTag(0xa00100, laterPart.sceneSerial).flags & Scope::Valid),
          "buffer rewind cannot retain earlier part in the same object scene");
    std::vector<uint8_t> ram(0x200000), scratch(0x4000);
    check(resolveState(ram.data(), (uint32_t)ram.size(), scratch.data(), (uint32_t)scratch.size(), 0x70000000, 0x80) ==
              scratch.data(),
          "actual renderer scratchpad state resolves without masking to RAM zero");
    check(!resolveState(ram.data(), (uint32_t)ram.size(), scratch.data(), (uint32_t)scratch.size(), 0x70003ff0, 0x80),
          "scratch state crossing end rejects");
    check(resolveState(ram.data(), (uint32_t)ram.size(), scratch.data(), (uint32_t)scratch.size(), 0x80100000, 0x80) ==
              ram.data() + 0x100000,
          "RAM alias renderer state remains supported");
    auto put = [&](uint32_t p, uint32_t v) { std::memcpy(ram.data() + p, &v, 4); };
    const uint32_t mesh = 0x120000, desc = 0x130000, group = 0x140000, index = 0x150000, pos = 0x160000, uv = 0x170000;
    put(mesh + 0xc, 1);
    put(mesh + 0x10, desc);
    put(desc + 0x10, 3);
    put(desc + 0x20, group);
    put(desc + 0x28, 1);
    put(desc + 0x2c, index);
    put(group, 3);
    put(group + 4, uv);
    put(group + 8, pos);
    ram[index] = 0;
    ram[index + 2] = 1;
    ram[index + 4] = 2;
    Tracker inlineTracker;
    inlineTracker.nextFrame();
    std::vector<Scope> groups;
    std::vector<uint64_t> positionHashes;
    auto capture = [&] {
        auto owner = inlineTracker.object(0x180000, 0x180000, 0x120, 0x51b8b0, mesh, 0x1485c8);
        captureInlineGroups(ram.data(), (uint32_t)ram.size(), inlineTracker, owner, mesh, true,
                            [&](const Scope& p, uint32_t source, uint64_t ph) {
                                check(source == pos, "source provenance uses actual position REF array");
                                groups.push_back(p);
                                positionHashes.push_back(ph);
                            });
    };
    capture();
    check(groups.size() == 1 && groups.back().partId == group && groups.back().passKey == 0x1ed8f0,
          "inline builder selects actual 16-byte group descriptor as part");
    inlineTracker.nextFrame();
    put(pos, 0x40000000);
    capture();
    check((groups.back().flags & Scope::TopologyStable) && positionHashes[0] != positionHashes[1],
          "deforming source positions retain certified original index and UV order");
    check(!(groups.back().flags & Scope::Animated), "position changes alone do not claim skeletal animation");
    auto refs = Tracker::Chain(inlineTracker, groups.back().sceneSerial);
    refs.tag(0x1a0000, 0, pos, 0x78030036);
    refs.tag(0x1a0010, 48, uv, 0x74030216);
    auto refJob = refs.finish(96);
    check(refJob->markers.size() == 1 && refJob->markers[0].scope.partId == group,
          "position REF begins scope and following UV upload retains it");
    auto wrongNum = Tracker::Chain(inlineTracker, groups.back().sceneSerial);
    wrongNum.tag(0x1a0000, 0, pos, 0x78020036);
    check((wrongNum.finish(96)->markers[0].scope.flags & Scope::Rejected) != 0,
          "VIF UNPACK count must equal original group count");
    inlineTracker.invalidateTags();
    check(inlineTracker.atReference(pos, groups.back().sceneSerial).partId == group,
          "immutable resource provenance survives command-buffer rewind");
    auto unknown = Tracker::Chain(inlineTracker, groups.back().sceneSerial);
    unknown.tag(0x1a0000, 0, pos + 0x100, 0x78030036);
    check(!(unknown.finish(48)->markers[0].scope.flags & Scope::Valid),
          "unknown position REF cannot inherit prior group");
    inlineTracker.reference(pos, groups.back());
    check((inlineTracker.atReference(pos, groups.back().sceneSerial).flags & Scope::Ambiguous) != 0,
          "aliased arrays shared by group descriptors reject ambiguous ownership");
    inlineTracker.nextFrame();
    ram[index + 2] = 2;
    capture();
    check(!(groups.back().flags & Scope::TopologyStable),
          "changed original preclip indices reject even with unchanged UV count");
    inlineTracker.nextFrame();
    ram[uv] = 1;
    capture();
    check(!(groups.back().flags & Scope::TopologyStable), "changed ordered UV stream rejects inline certificate");
    inlineTracker.nextFrame();
    put(desc + 0x2c, 0);
    capture();
    check((groups.back().flags & Scope::Rejected) != 0,
          "missing original indices cannot certify deforming correspondence");
    check(refJob->markers[0].scope.topologyHash == groups[1].topologyHash,
          "copied inline scope survives later index mutation and EE advance");
    const uint32_t object = 0x180000, controller = 0x190000, vt = 0x191000, matrices = 0x192000, header = 0x193000,
                   posed = 0x194000;
    put(object + 0x50, controller);
    put(controller + 0xd4, object);
    put(controller, vt);
    put(vt + 0x144, 0x106aa8);
    put(controller + 8, 0x100000);
    put(controller + 0x114, 2);
    put(controller + 0x10c, matrices);
    put(controller + 0x120, header);
    put(header, posed);
    put(header + 8, 4);
    put(controller + 0x118, 4);
    check(provenPoseSource(ram.data(), (uint32_t)ram.size(), object).address == posed,
          "known owner/controller/bone-output chain resolves exact per-object source");
    put(controller + 0xd4, object + 0x120);
    check(!provenPoseSource(ram.data(), (uint32_t)ram.size(), object).address,
          "wrong controller owner cannot claim Animated");
    put(controller + 0xd4, object);
    put(controller + 8, 0);
    check(!provenPoseSource(ram.data(), (uint32_t)ram.size(), object).address,
          "static-copy controller branch cannot claim Animated");
    put(controller + 8, 0x100000);
    put(vt + 0x144, 0x100000);
    check(!provenPoseSource(ram.data(), (uint32_t)ram.size(), object).address,
          "unknown controller implementation cannot claim Animated");
    put(vt + 0x144, 0x106aa8);
    put(header + 8, 3);
    check(!provenPoseSource(ram.data(), (uint32_t)ram.size(), object).address,
          "incomplete pose buffer cannot certify animation");
    put(header + 8, 4);
    put(controller + 0x10, 1);
    check(!provenPoseSource(ram.data(), (uint32_t)ram.size(), object).address,
          "untraced controller slot remains unclassified");
    const uint32_t table = 0x1a0000, selectors = 0x1b0000, packed = 0x1c0000;
    put(mesh + 8, table);
    put(desc + 8, 1);
    put(desc + 0xc, selectors);
    put(table, 2u | (3u << 16));
    auto indexed = inspectInputTopology(ram.data(), (uint32_t)ram.size(), mesh, desc, true);
    check(indexed.verified && indexed.vertices == 3 && indexed.maxPositionIndex == 5,
          "indexed certificate resolves original start/count range rather than selector alone");
    put(table, 3u | (3u << 16));
    check(inspectInputTopology(ram.data(), (uint32_t)ram.size(), mesh, desc, true).hash != indexed.hash,
          "changed source range rejects stable-topology certificate");
    put(table, 2u | (3u << 16));
    ram[selectors + 1] = 1;
    check(inspectInputTopology(ram.data(), (uint32_t)ram.size(), mesh, desc, true).hash != indexed.hash,
          "changed VU destination selector also changes certificate");
    put(desc + 0x10, 3);
    put(desc + 0x14, packed);
    auto sequential = inspectInputTopology(ram.data(), (uint32_t)ram.size(), mesh, desc, false);
    check(sequential.verified, "sequential packed ordering bytes are validated");
    ram[packed + 63] = 1;
    check(inspectInputTopology(ram.data(), (uint32_t)ram.size(), mesh, desc, false).hash != sequential.hash,
          "last padded block is hashed exactly as VIF reads it");
    put(desc + 0x14, 0);
    check(!inspectInputTopology(ram.data(), (uint32_t)ram.size(), mesh, desc, false).verified,
          "invalid packed ordering source rejects");
    if (argc == 3) {
        std::vector<uint8_t> before(32u * 1024u * 1024u), after(before.size());
        std::ifstream first(argv[1], std::ios::binary), last(argv[2], std::ios::binary);
        first.read(reinterpret_cast<char*>(before.data()), before.size());
        last.read(reinterpret_cast<char*>(after.data()), after.size());
        check(first.gcount() == static_cast<std::streamsize>(before.size()) &&
                  last.gcount() == static_cast<std::streamsize>(after.size()),
              "both measured combat RAM snapshots read completely");
        auto oldPose = provenPoseSource(before.data(), (uint32_t)before.size(), 0xa5b6a0);
        auto newPose = provenPoseSource(after.data(), (uint32_t)after.size(), 0xa5b6a0);
        check(oldPose.address == 0x14d61d0 && newPose.address == oldPose.address && newPose.count == 1628,
              "actual combat owner reaches persistent verified skeletal pose buffer");
        auto distance = [&](const std::vector<uint8_t>& bytes) {
            float x[3], y[3];
            std::memcpy(x, bytes.data() + newPose.address + 500 * 12, 12);
            std::memcpy(y, bytes.data() + newPose.address + 1200 * 12, 12);
            double result = 0;
            for (int i = 0; i < 3; ++i)
                result += double(x[i] - y[i]) * double(x[i] - y[i]);
            return result;
        };
        const double d0 = distance(before), d1 = distance(after);
        check(std::fabs(d1 - d0) > d0 * 0.01,
              "measured vertex pair changes distance: deformation beyond common rigid transform");
        auto oldIndexed = inspectInputTopology(before.data(), (uint32_t)before.size(), 0xbcbca0, 0xbcbda0, true);
        auto newIndexed = inspectInputTopology(after.data(), (uint32_t)after.size(), 0xbcbca0, 0xbcbda0, true);
        check(oldIndexed.verified && newIndexed.verified && oldIndexed.hash == newIndexed.hash,
              "actual moving owner preserves indexed input certificate across combat snapshots");
        auto oldSeq = inspectInputTopology(before.data(), (uint32_t)before.size(), 0xbcbca0, 0xbcbda0, false);
        auto newSeq = inspectInputTopology(after.data(), (uint32_t)after.size(), 0xbcbca0, 0xbcbda0, false);
        check(oldSeq.verified && newSeq.verified && oldSeq.hash == newSeq.hash,
              "actual moving owner preserves packed strip ordering across combat snapshots");
        std::printf("RAM evidence: owner=a5b6a0 controller=%x pose=%x vertices=%u distance2(500,1200)=%.6f -> %.6f "
                    "topology=%llx/%llx\n",
                    newPose.controller, newPose.address, newPose.count, d0, d1, (unsigned long long)newIndexed.hash,
                    (unsigned long long)newSeq.hash);
    }
    std::printf("PASS: %d object identity, topology, generation and delayed VIF provenance checks\n", checkCount);
}
