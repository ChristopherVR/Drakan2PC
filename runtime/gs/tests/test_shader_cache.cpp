// Shader blob cache (src/shader_cache.h): stable keys, file image validation, disk round trip, one compile per key
// under concurrency, failures not cached. Portable, no GPU.
#include "shader_cache.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace gsr::shader_cache;

namespace {
Request request(const char* source, const char* entry, const char* target, const Macro* macros = nullptr,
                size_t count = 0, uint32_t flags = 0) {
    Request r;
    r.source = source;
    r.length = std::char_traits<char>::length(source);
    r.name = "test";
    r.entry = entry;
    r.target = target;
    r.macros = macros;
    r.macroCount = count;
    r.flags = flags;
    return r;
}
} // namespace

int main() {
    const char* src = "float4 main():SV_Target{return 1;}";
    // Keys: deterministic, and every input that changes the compiled bytes changes the key.
    const uint64_t k = key(request(src, "main", "ps_5_0"));
    assert(k == key(request(src, "main", "ps_5_0")));
    assert(k != key(request(src, "main", "ps_4_0")));
    assert(k != key(request(src, "PSMain", "ps_5_0")));
    assert(k != key(request(src, "main", "ps_5_0", nullptr, 0, 1u)));
    assert(k != key(request("float4 main():SV_Target{return 0;}", "main", "ps_5_0")));
    const Macro hw1[] = {{"HW_BLEND", "1"}}, hw0[] = {{"HW_BLEND", "0"}}, split[] = {{"HW_BLEN", "D1"}};
    assert(key(request(src, "main", "ps_5_0", hw1, 1)) != key(request(src, "main", "ps_5_0", hw0, 1)));
    assert(key(request(src, "main", "ps_5_0", hw1, 1)) != key(request(src, "main", "ps_5_0", split, 1)));
    assert(key(request(src, "main", "ps_5_0", hw1, 1)) != k);

    // File image: round trip; wrong key, truncation, a flipped payload byte and a bad magic are all misses.
    const std::vector<uint8_t> blob = {'D', 'X', 'B', 'C', 1, 2, 3, 4, 5};
    std::vector<uint8_t> file = encode(k, blob), back;
    assert(decode(k, file, back) && back == blob);
    assert(!decode(k + 1, file, back));
    std::vector<uint8_t> cut(file.begin(), file.end() - 1);
    assert(!decode(k, cut, back));
    std::vector<uint8_t> flipped = file;
    flipped.back() ^= 0x40;
    assert(!decode(k, flipped, back));
    std::vector<uint8_t> magic = file;
    magic[0] = 'X';
    assert(!decode(k, magic, back));
    assert(!decode(k, encode(k, {}), back)); // an empty payload is never a valid blob

    // Memory level, with the producer run once per key even when many threads ask at the same time.
    {
        Cache cache;
        std::atomic<int> runs{0};
        std::vector<std::thread> threads;
        std::vector<std::vector<uint8_t>> got(8);
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&, i] {
                cache.get(
                    k,
                    [&](std::vector<uint8_t>& out) {
                        ++runs;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        out = blob;
                        return true;
                    },
                    got[i]);
            });
        for (auto& t : threads)
            t.join();
        assert(runs == 1);
        for (auto& g : got)
            assert(g == blob);
        assert(cache.contains(k));
        const Stats s = cache.stats();
        assert(s.produced == 1 && s.memoryHits == 7 && s.failed == 0);
    }

    // A failed compile is not cached: the next caller runs the producer again.
    {
        Cache cache;
        int runs = 0;
        std::vector<uint8_t> out;
        auto fail = [&](std::vector<uint8_t>&) {
            ++runs;
            return false;
        };
        assert(cache.get(k, fail, out) == Source::Failed && out.empty());
        assert(cache.get(k, fail, out) == Source::Failed);
        assert(runs == 2 && !cache.contains(k));
    }

    // Disk level: a second process (a fresh cache) reads the blob back without compiling; a corrupt file is
    // recompiled and rewritten.
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("gsr_shader_cache_test_" + std::to_string(k & 0xffff));
        std::error_code ec;
        fs::remove_all(dir, ec);
        int runs = 0;
        auto produce = [&](std::vector<uint8_t>& out) {
            ++runs;
            out = blob;
            return true;
        };
        std::vector<uint8_t> out;
        {
            Cache first;
            first.setDirectory(utf8(dir));
            assert(first.get(k, produce, out) == Source::Produced && out == blob);
        }
        {
            Cache second;
            second.setDirectory(utf8(dir));
            assert(second.get(k, produce, out) == Source::Disk && out == blob);
            assert(second.get(k, produce, out) == Source::Memory);
        }
        assert(runs == 1);
        int files = 0;
        fs::path written;
        for (const auto& e : fs::directory_iterator(dir)) {
            ++files;
            written = e.path();
        }
        assert(files == 1 && written.extension() == ".bin"); // no temporary left behind
        {
            std::ofstream bad(written, std::ios::binary | std::ios::trunc);
            bad << "garbage";
        }
        {
            Cache third;
            third.setDirectory(utf8(dir));
            assert(third.get(k, produce, out) == Source::Produced && out == blob && runs == 2);
        }
        {
            Cache fourth;
            fourth.setDirectory(utf8(dir));
            assert(fourth.get(k, produce, out) == Source::Disk && runs == 2);
        }
        fs::remove_all(dir, ec);
    }
    std::printf("shader cache ok\n");
    return 0;
}
