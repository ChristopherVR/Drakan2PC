#pragma once
#include "runtime/gs/gs_texture_page_cache.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

namespace gsr {
struct PackImage;
}
struct GSDrawState;
struct GSPrimitiveBatch;
namespace gs_cpu {
inline constexpr unsigned kMaxRasterThreads = 16;
inline unsigned rasterThreadCount() {
    const unsigned available = std::thread::hardware_concurrency();
    unsigned count = std::clamp(available, 1u, 8u);
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    // Leave room for EE, resource/audio and presentation work. Four render
    // workers remain useful on smaller machines; larger hosts can use eight.
    count = std::min(count, std::clamp(available / 2, 4u, 8u));
#endif
#if defined(__EMSCRIPTEN__)
    // Keep the eight-worker tile grid: extra workers share its existing jobs.
    // This avoids increasing primitive setup work on sixteen-logical-CPU hosts.
    if (available >= kMaxRasterThreads)
        count = kMaxRasterThreads;
#endif
    if (const char* value = std::getenv("PS2X_CPU_RASTER_THREADS"))
        count = static_cast<unsigned>(std::clamp(std::strtol(value, nullptr, 10), 1l, long(kMaxRasterThreads)));
    return count;
}
// Only used inside an immutable texture scope. A generation change invalidates
// decoded colors without clearing the table on every texture/CLUT transition.
class PaletteMemo {
public:
    struct Entry {
        uint32_t generation = 0, color = 0;
    };
    void reset() {
        advance();
        validKey_ = false;
    }
    void texture(uint64_t key) {
        if (!validKey_ || key_ != key) {
            advance();
            key_ = key;
            validKey_ = true;
        }
    }
    Entry& entry(uint8_t index) { return entries_[index]; }
    bool read(const Entry& entry, uint32_t& color) const {
        if (entry.generation != generation_)
            return false;
        color = entry.color;
        return true;
    }
    void write(Entry& entry, uint32_t color) { entry = {generation_, color}; }

private:
    void advance() {
        if (++generation_ == 0) {
            entries_.fill({});
            generation_ = 1;
        }
    }
    std::array<Entry, 256> entries_{};
    uint64_t key_ = 0;
    uint32_t generation_ = 0;
    bool validKey_ = false;
};
struct alignas(64) TileContext {
    int firstRow = 0, lastRow = -1;
    int firstColumn = 0, lastColumn = -1;
    int lastSampleX = 0, lastSampleY = 0;
    GSMem::TexturePageCache textureCache;
    bool sampledTexture = false;
    const gsr::PackImage* replacement = nullptr;
    bool preparedNative = false;
    PaletteMemo* palette = nullptr;
    const std::array<uint16_t, 512>* clut = nullptr;
    const std::array<uint32_t, 256>* clut32 = nullptr;
    const GSDrawState* lastTextureState = nullptr;
    uint32_t lastU = 0, lastV = 0;
    const GSPrimitiveBatch* deferredSample = nullptr;
    float sampleW0 = 0, sampleW1 = 0, sampleW2 = 0;
    uint64_t batchIndex = 0, lastSampleBatch = 0;
};
inline thread_local TileContext* currentTile = nullptr;
inline std::atomic<uint64_t> tileDispatches{0};
inline std::atomic<uint64_t> tilePrimitives{0};

// One synchronous dispatch at a time; each tile owns disjoint 32-bit pixels.
// Persistent threads avoid creating OS/browser workers for each primitive.
class TileWorkers {
    std::mutex dispatchMutex_, stateMutex_;
    std::condition_variable workReady_, workDone_;
    std::vector<std::thread> threads_;
    std::function<void(unsigned)> task_;
    std::exception_ptr error_;
    unsigned generation_ = 0, pending_ = 0;
    bool stopping_ = false;

    void recordError() {
        std::lock_guard lock(stateMutex_);
        if (!error_)
            error_ = std::current_exception();
    }
    void worker(unsigned index) {
        unsigned generation = 0;
        for (;;) {
            {
                std::unique_lock lock(stateMutex_);
                workReady_.wait(lock, [&] { return stopping_ || generation_ != generation; });
                if (stopping_)
                    return;
                generation = generation_;
            }
            try {
                task_(index);
            } catch (...) {
                recordError();
            }
            {
                std::lock_guard lock(stateMutex_);
                if (--pending_ == 0)
                    workDone_.notify_one();
            }
        }
    }

public:
    explicit TileWorkers(unsigned requested = rasterThreadCount()) {
        const unsigned count = std::min(requested, std::max(std::thread::hardware_concurrency(), 1u));
        for (unsigned index = 1; index < count; ++index) {
            try {
                threads_.emplace_back([this, index] { worker(index); });
            } catch (const std::system_error&) {
                break;
            }
        }
    }
    ~TileWorkers() {
        {
            std::lock_guard lock(stateMutex_);
            stopping_ = true;
        }
        workReady_.notify_all();
        for (auto& thread : threads_)
            thread.join();
    }
    unsigned size() const { return static_cast<unsigned>(threads_.size()) + 1; }
    void run(const std::function<void(unsigned)>& task) {
        std::lock_guard dispatch(dispatchMutex_);
        {
            std::lock_guard lock(stateMutex_);
            task_ = task;
            error_ = nullptr;
            pending_ = static_cast<unsigned>(threads_.size());
            ++generation_;
        }
        workReady_.notify_all();
        try {
            task_(0);
        } catch (...) {
            recordError();
        }
        std::unique_lock lock(stateMutex_);
        workDone_.wait(lock, [&] { return pending_ == 0; });
        task_ = {};
        if (error_)
            std::rethrow_exception(error_);
    }
};
inline std::shared_ptr<TileWorkers> sharedTileWorkers(unsigned requested = rasterThreadCount()) {
    static std::mutex mutex;
    static std::array<std::weak_ptr<TileWorkers>, kMaxRasterThreads + 1> pools;
    const auto count = std::clamp(requested, 1u, kMaxRasterThreads);
    std::lock_guard lock(mutex);
    auto workers = pools[count].lock();
    if (!workers) {
        workers = std::make_shared<TileWorkers>(count);
        pools[count] = workers;
    }
    return workers;
}
} // namespace gs_cpu
