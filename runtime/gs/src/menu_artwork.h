#pragma once
#include <cstdint>

namespace gsr::hud {
// Exact immutable INTERFCE.TXD title/scenic tiles, not generic full-color effects.
constexpr bool menuArtwork(std::uint64_t hash) {
    switch (hash) {
        case 0xb3cdb49737930d6dull:
        case 0x4f7ee3d0edc23360ull:
        case 0x9810e71247d924e8ull:
        case 0x2ca46f8a975e9e38ull:
        case 0x3d7202d7d8b7115full:
        case 0xce000a7fd2216f1cull:
        case 0x4ce8e477b5873557ull:
        case 0x81bd5b91d7726154ull:
        case 0xbb40ebe4a17c57abull:
        case 0x75ef0216c338277aull:
        case 0x5bfb4a7fa13d41b9ull:
        case 0x06134eac70eaf6d4ull:
        case 0x3de6b9b1c88262e3ull:
        case 0x08ec88ee9b808e6aull:
        case 0xe6b89628225bbb59ull:
        case 0xf071ad3ba421ddf7ull:
        case 0x675b752c148e5250ull:
        case 0xc98228356f31fc5eull:
        case 0xeadaddcbabf5bd2bull:
        case 0x16896dc76573918full:
        case 0xc7238e124f0529eeull:
        case 0xed49867a6f55c572ull:
        case 0xcc31d99e55033acbull:
        case 0xa4b1d3987a0e96caull:
        case 0x9317d6b165a6f17eull:
        case 0x56a95e5f0d87d78eull:
        case 0x6774179b4d46100eull:
        case 0xa30f2abe258fd09aull:
        case 0xd6583d9e786479e1ull:
        case 0x8d8f414b52d11cbdull:
        case 0x8d29018d24c15c4cull:
        case 0x0950e4424cc7dd66ull:
        case 0x61f1ea6689f638d7ull:
        case 0xddba95a5303adaa4ull:
        case 0x250b3fb0cba33d7cull:
        case 0x588d402d3095ac3bull:
        case 0xb3faaee26c40bed9ull:
        case 0xac4c051ac797c951ull:
        case 0xb44be67eee459e34ull:
        case 0x1417655478e4719cull:
        case 0x073ea88a3a0319f8ull:
        case 0xaa8e305710a283ddull:
            return true;
        default:
            return false;
    }
}
} // namespace gsr::hud
