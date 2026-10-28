#include "runtime/ps2_vu1_async.h"
#include "runtime/ps2_memory.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <mutex>

static void require(bool result) {
    if (!result) {
        std::fputs("Async queue check failed\n", stderr);
        std::abort();
    }
}
int main() {
    using namespace std::chrono_literals;
    PS2Memory memory;
    std::mutex mutex;
    std::condition_variable gate;
    bool entered = false, released = false;
    std::atomic<uint32_t> executed{0};
    memory.execute = [&](const uint8_t* bytes, uint32_t size) {
        uint32_t id;
        require(size == 4);
        std::memcpy(&id, bytes, 4);
        require(vu1async::onWorker());
        require(vu1async::currentMeta() && vu1async::currentMeta()->ctx == &memory);
        vu1async::sync(); // worker recursion must never wait on itself
        if (id == 0) {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            gate.notify_all();
            gate.wait(lock, [&] { return released; });
        }
        require(id == executed.fetch_add(1));
    };
    auto submit = [&](uint32_t id) {
        std::vector<uint8_t> bytes(4);
        std::memcpy(bytes.data(), &id, 4);
        vu1async::Meta meta;
        meta.ctx = &memory;
        vu1async::submit(&memory, std::move(bytes), meta);
    };
    submit(0);
    {
        std::unique_lock<std::mutex> lock(mutex);
        require(gate.wait_for(lock, 5s, [&] { return entered; }));
    }
    for (uint32_t i = 1; i <= 256; ++i)
        submit(i);
    auto blockedProducer = std::async(std::launch::async, [&] { submit(257); });
    auto completion = std::async(std::launch::async, [] { vu1async::sync(); });
    auto secondCompletion = std::async(std::launch::async, [] { vu1async::sync(); });
    require(blockedProducer.wait_for(20ms) == std::future_status::timeout);
    require(completion.wait_for(20ms) == std::future_status::timeout);
    require(secondCompletion.wait_for(20ms) == std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
    }
    gate.notify_all();
    require(blockedProducer.wait_for(5s) == std::future_status::ready);
    blockedProducer.get();
    require(completion.wait_for(5s) == std::future_status::ready);
    completion.get();
    require(secondCompletion.wait_for(5s) == std::future_status::ready);
    secondCompletion.get();
    vu1async::sync();
    require(executed == 258 && vu1async::g_pending == 0);
    uint32_t id = 258;
    for (uint32_t batch = 0; batch < 100; ++batch) {
        for (uint32_t n = 0; n < 1000; ++n)
            submit(id++);
        vu1async::sync();
        require(executed == id && vu1async::g_pending == 0);
    }
    for (uint32_t n = 0; n < 1000; ++n)
        submit(id++);
    vu1async::shutdown();
    require(executed == id && vu1async::g_pending == 0);
    std::printf(
        "PASS: %u ordered jobs; full-queue backpressure, completion barriers, metadata, worker sync and shutdown\n",
        id);
}
