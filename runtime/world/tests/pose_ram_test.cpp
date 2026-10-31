// Data-dependent check of readPlayerPose over local RAM dumps (game-derived, under build/, never committed).
// Exits 77 (ctest: skipped) when no dump is present.
//   world_pose_ram_test                       the default dumps below, relative to the repository root
//   world_pose_ram_test <dump.ram> ...        the same invariants for other gameplay dumps
//   world_pose_ram_test --json out.json <dump.ram> ...   also writes the poses as a JSON array (S1 playback sample)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "world/Pose.h"

#ifndef WORLD_REPO_ROOT
#define WORLD_REPO_ROOT "."
#endif

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

namespace {

bool load(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return out.size() >= 0x2000000;
}

bool finite3(const world::Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool check(const std::string& name, const std::vector<uint8_t>& ram, world::PlayerPose& p) {
    const world::FlatGuestMemory mem(ram.data(), ram.size());
    world::PoseReadState st;
    const world::PoseStatus s = world::readPlayerPose(mem, world::Globals{}, st, 0.0, 0, p);
    std::printf("%s: %s class %x mounted %d ch %u lower %x@%.2f upper %x@%.2f weapon R %x L %x\n", name.c_str(),
                world::poseStatusName(s), p.classId, p.mounted, p.channelCount, p.lower.clip, double(p.lower.time),
                p.upper.clip, double(p.upper.time), p.weaponRight, p.weaponLeft);
    if (s == world::PoseStatus::NoContext || s == world::PoseStatus::NoPlayer)
        return false; // title screen / loading dump: nothing to check
    CHECK(s == world::PoseStatus::Ok);
    if (s != world::PoseStatus::Ok)
        return false;
    CHECK(p.classId == world::kClassRynn || p.classId == world::kClassDragon);
    CHECK(finite3(p.position) && p.position.x > 1000 && p.position.z > 1000);
    // a rotation: rows unit length, row 1 = (0,1,0) for the grounded player
    for (int r = 0; r < 3; ++r) {
        const float* m = p.rotation + 3 * r;
        CHECK(std::fabs(m[0] * m[0] + m[1] * m[1] + m[2] * m[2] - 1.0f) < 0.01f);
    }
    CHECK(p.channelCount >= 1 && p.channelCount <= world::skel::kMaxChannels);
    CHECK(p.lower.clip >= 0x20000 && p.lower.clip < 0x50000);
    CHECK(p.upper.clip >= 0x20000 && p.upper.clip < 0x50000);
    CHECK(std::isfinite(p.lower.time) && p.lower.time >= 0 && p.lower.time < 60);
    CHECK(std::isfinite(p.upper.time) && p.upper.time >= 0 && p.upper.time < 60);
    CHECK(p.lowerMask != 0);
    if (p.mounted)
        CHECK(p.weaponRight == 0);
    return true;
}

void writeJson(const std::string& path, const std::vector<world::PlayerPose>& poses) {
    std::ofstream f(path);
    f << "[\n";
    for (size_t i = 0; i < poses.size(); ++i) {
        const world::PlayerPose& p = poses[i];
        char buf[640];
        std::snprintf(buf, sizeof buf,
                      "  {\"t\":%.3f,\"pos\":[%.2f,%.2f,%.2f],\"yaw\":%.5f,\"vel\":[%.2f,%.2f,%.2f],"
                      "\"lower\":{\"clip\":%u,\"time\":%.4f},\"upper\":{\"clip\":%u,\"time\":%.4f},"
                      "\"weaponRight\":%u,\"weaponLeft\":%u,\"mounted\":%s,\"attacking\":%s}%s\n",
                      p.timeSec, double(p.position.x), double(p.position.y), double(p.position.z), double(p.yaw),
                      double(p.velocity.x), double(p.velocity.y), double(p.velocity.z), p.lower.clip,
                      double(p.lower.time), p.upper.clip, double(p.upper.time), p.weaponRight, p.weaponLeft,
                      p.mounted ? "true" : "false", p.attacking ? "true" : "false", i + 1 < poses.size() ? "," : "");
        f << buf;
    }
    f << "]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    std::string json;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--json" && i + 1 < argc)
            json = argv[++i];
        else
            paths.push_back(argv[i]);
    }
    if (paths.empty()) {
        const std::string root = std::string(WORLD_REPO_ROOT) + "/build/";
        const std::string melee = root + "camera-pitch-build02-prepared/t/camera-melee-build02-on-02/";
        paths = {melee + "ram_neutral.ram",
                 melee + "ram_strafe.ram",
                 melee + "ram_hit2.ram",
                 melee + "ram_hit3.ram",
                 melee + "ram_block_held.ram",
                 root + "harness/bow4/ram_closed.ram",
                 root + "harness/arokh_30/ram_a00_start.ram"};
    }
    int used = 0;
    std::vector<world::PlayerPose> poses;
    for (const std::string& p : paths) {
        std::vector<uint8_t> ram;
        if (!load(p, ram))
            continue;
        world::PlayerPose pose;
        if (check(p, ram, pose)) {
            ++used;
            pose.timeSec = 0.05 * double(poses.size());
            poses.push_back(pose);
        }
    }
    if (!used) {
        std::puts("world_pose_ram_test: no RAM dump present, skipped");
        return 77;
    }
    if (!json.empty())
        writeJson(json, poses);
    std::printf("world_pose_ram_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
