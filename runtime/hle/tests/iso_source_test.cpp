#include "hle/IsoFs.h"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
struct MemoryDisc final : hle::IsoSource {
    std::vector<hle::u8> bytes = std::vector<hle::u8>(24 * 2048);
    bool readAt(hle::u64 offset, void* destination, size_t size) const override {
        if (offset > bytes.size() || size > bytes.size() - offset)
            return false;
        std::memcpy(destination, bytes.data() + offset, size);
        return true;
    }
};
void put32(hle::u8* out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<hle::u8>(value >> (8 * i));
}
} // namespace
int main(int argc, char** argv) {
    auto disc = std::make_shared<MemoryDisc>();
    auto* pvd = disc->bytes.data() + 16 * 2048;
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    put32(pvd + 158, 20);
    put32(pvd + 166, 2048);
    auto* record = disc->bytes.data() + 20 * 2048;
    record[0] = 44;
    put32(record + 2, 21);
    put32(record + 10, 4);
    record[32] = 11;
    std::memcpy(record + 33, "HELLO.BIN;1", 11);
    std::memcpy(disc->bytes.data() + 21 * 2048, "DISC", 4);
    hle::IsoFs fs;
    assert(fs.openSource(disc));
    hle::IsoFs::Node file;
    assert(fs.lookup("HELLO.BIN", file));
    std::array<char, 8> result{};
    assert(fs.read(file, 0, result.data(), result.size()) == 4);
    assert(std::memcmp(result.data(), "DISC", 4) == 0);
    assert(fs.read(file, 2, result.data(), result.size()) == 2);
    assert(std::memcmp(result.data(), "SC", 2) == 0);
    assert(fs.read(file, 4, result.data(), result.size()) == 0);
    std::string error;
    auto invalid = std::make_shared<MemoryDisc>();
    assert(!fs.openSource(invalid, &error));
    assert(!fs.isIso() && fs.entries().empty() && !error.empty());
#if !defined(__EMSCRIPTEN__)
    assert(argc == 2);
    const std::filesystem::path path(argv[1]);
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(disc->bytes.data()), disc->bytes.size());
        assert(out.good());
    }
    assert(fs.openIso(path.string()));
    assert(fs.lookup("HELLO.BIN", file));
    assert(fs.read(file, 0, result.data(), result.size()) == 4);
    assert(std::memcmp(result.data(), "DISC", 4) == 0);
    fs.openSource(nullptr);
    std::filesystem::remove(path);
#endif
}
