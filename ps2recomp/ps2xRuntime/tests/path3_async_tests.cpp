#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1_async.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "runtime/ps2_render_queue.h"

int main() {
    using namespace std::chrono_literals;
    PS2Memory mem;
    if (!mem.initialize())
        return 2;
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, released = false;
    std::atomic<bool> timedOut = false;
    std::vector<uint8_t> observed, expected;
    mem.setGifPacketCallback([&](const uint8_t* data, uint32_t bytes) {
        std::unique_lock lock(mutex);
        if (!entered) {
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return released; });
        }
        observed.insert(observed.end(), data, data + bytes);
    });
    auto packet = [](uint32_t id) {
        std::array<uint8_t, 32> p{};
        const uint64_t tag = 1ull | (1ull << 15) | (1ull << 60), reg = 0xE;
        std::memcpy(p.data(), &tag, 8);
        std::memcpy(p.data() + 8, &reg, 8);
        std::memcpy(p.data() + 16, &id, 4);
        return p;
    };
    auto vif = [&](uint32_t id) {
        auto p = packet(id);
        std::vector<uint8_t> v(36);
        const uint32_t command = 0x50000002u;
        std::memcpy(v.data(), &command, 4);
        std::memcpy(v.data() + 4, p.data(), 32);
        expected.insert(expected.end(), p.begin(), p.end());
        vu1async::submit(&mem, std::move(v), {});
    };
    auto kick = [&](uint32_t address, uint32_t qwc, uint32_t chcr) {
        mem.writeIORegister(0x1000A010u, address);
        mem.writeIORegister(0x1000A020u, qwc);
        mem.writeIORegister(0x1000A000u, chcr);
    };
    vif(1);
    {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, 2s, [&] { return entered; }))
            return 3;
    }
    // If a supposedly asynchronous operation waits for this job, release it
    // after a bounded interval and fail, rather than hanging the test process.
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, 2s, [&] { return released; })) {
            timedOut = true;
            released = true;
            cv.notify_all();
        }
    });
    mem.writeIORegister(0x10003010u, 5);
    const bool modeGood = mem.readIORegister(0x10003010u) == 5;
    auto p = packet(2);
    std::memcpy(mem.getRDRAM() + 0x1000, p.data(), 32);
    expected.insert(expected.end(), p.begin(), p.end());
    kick(0x1000, 2, 0x101);
    std::memset(mem.getRDRAM() + 0x1000, 0xCC, 32); // queued DMA owns its bytes
    vif(3);
    p = packet(4);
    const uint64_t endTag = 2ull | (7ull << 28);
    std::memcpy(mem.getRDRAM() + 0x2000, &endTag, 8);
    std::memcpy(mem.getRDRAM() + 0x2010, p.data(), 32);
    expected.insert(expected.end(), p.begin(), p.end());
    mem.writeIORegister(0x1000A030u, 0x2000);
    kick(0, 0, 0x105);
    std::memset(mem.getRDRAM() + 0x2000, 0xDD, 48);
    p = packet(5);
    for (uint32_t i = 0; i < 32; ++i)
        mem.write8(0x70000000u + i, p[i]);
    expected.insert(expected.end(), p.begin(), p.end());
    kick(0x70000000u, 2, 0x101);
    for (uint32_t i = 0; i < 32; ++i)
        mem.write8(0x70000000u + i, 0xEE);
    p = packet(6);
    std::memcpy(mem.getRDRAM() + PS2_RAM_SIZE - 16, p.data(), 16);
    std::memcpy(mem.getRDRAM(), p.data() + 16, 16);
    expected.insert(expected.end(), p.begin(), p.end());
    kick(PS2_RAM_SIZE - 16, 2, 0x101);
    std::memset(mem.getRDRAM() + PS2_RAM_SIZE - 16, 0xFF, 16);
    std::memset(mem.getRDRAM(), 0xFF, 16);
    vif(7);
    {
        std::lock_guard lock(mutex);
        released = true;
        cv.notify_all();
    }
    watchdog.join();
    vu1async::sync();
    if (!modeGood || timedOut || observed != expected) {
        std::fprintf(stderr, "FAIL ordering/snapshot/overlap: timeout=%d observed=%zu expected=%zu\n",
                     int(timedOut.load()), observed.size(), expected.size());
        return 5;
    }
    // Exercise queue back-pressure and repeated source-buffer reuse.
    for (uint32_t i = 0; i < 2048; ++i) {
        p = packet(100 + i);
        std::memcpy(mem.getRDRAM() + 0x1000, p.data(), 32);
        expected.insert(expected.end(), p.begin(), p.end());
        kick(0x1000, 2, 0x101);
        if ((i & 15) == 0)
            vif(10000 + i);
    }
    vu1async::sync();
    bool good = observed == expected;
    GifArbiter arbiter;
    arbiter.setProcessPacketFn([](const uint8_t*, uint32_t) {});
    std::vector<std::pair<uint32_t, uint32_t>> paths;
    arbiter.setProcessPathPacketFn([&](uint8_t path, const uint8_t* data, uint32_t bytes) {
        uint32_t id = 0;
        if (bytes >= 20)
            std::memcpy(&id, data + 16, 4);
        paths.emplace_back(path, id);
    });
    mem.setGifArbiter(&arbiter);
    const auto mask = [&](bool on) {
        uint32_t command = on ? 0x06008000u : 0x06000000u;
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(), &command, 4);
        vu1async::submit(&mem, std::move(bytes), {});
    };
    mask(true);
    p = packet(700);
    std::memcpy(mem.getRDRAM() + 0x1000, p.data(), 32);
    kick(0x1000, 2, 0x101);
    vu1async::submitRenderTask([&] { paths.emplace_back(0, 999); });
    vif(701);
    mask(false);
    vu1async::sync();
    good = good && paths == std::vector<std::pair<uint32_t, uint32_t>>{{0, 999}, {2, 701}, {3, 700}} &&
           !mem.isPath3Masked();
    if (!good)
        for (auto [path, id] : paths)
            std::fprintf(stderr, "path=%u id=%u\n", path, id);
    mem.setGifArbiter(nullptr);
    vu1async::shutdown();
    std::printf("%s: FIFO VIF/PATH3 order, normal/chain/scratch/wrapping DMA snapshots, EE overlap, queue pressure "
                "(%zu bytes)\n",
                good ? "PASS" : "FAIL", expected.size());
    return good ? 0 : 6;
}
