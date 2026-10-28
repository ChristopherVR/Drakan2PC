#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace drakan_presentation {
// Opt-in observations only. Configuration/reset happens before guest execution.
// Snapshots taken during execution may straddle one producer update.
class FrameMetrics {
public:
    static constexpr std::array<int64_t, 7> kBoundsNs{16666667, 25000000, 30000000, 33333334,
                                                      40000000, 50000000, 100000000};
    void enable(bool enabled) {
        enabled_.store(false, std::memory_order_release);
        last_ = 0;
        lastUpload_ = 0;
        lastUploadKey_ = 0;
        for (auto& bucket : buckets_)
            bucket = 0;
        for (auto& bucket : uploadBuckets_)
            bucket = 0;
        gameNs_ = updates_ = invalid_ = uploads_ = uploadErrors_ = 0;
        enabled_.store(enabled, std::memory_order_release);
    }
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }
    void frame(int64_t now) {
        if (!enabled())
            return;
        if (last_ != 0) {
            if (now < last_) {
                invalid_.fetch_add(1, std::memory_order_relaxed);
            } else {
                const int64_t delta = now - last_;
                unsigned bucket = 0;
                while (bucket < kBoundsNs.size() && delta > kBoundsNs[bucket])
                    ++bucket;
                buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
            }
        }
        last_ = now;
    }
    void gameDelta(float dt) {
        if (!enabled())
            return;
        if (!std::isfinite(dt) || dt < 0 || dt > 60) {
            invalid_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        gameNs_.fetch_add(static_cast<uint64_t>(double(dt) * 1000000000.0 + 0.5), std::memory_order_relaxed);
        updates_.fetch_add(1, std::memory_order_relaxed);
    }
    void uploaded(uint64_t key, int64_t now) {
        if (enabled() && key != lastUploadKey_) {
            if (lastUpload_ != 0 && now >= lastUpload_) {
                unsigned bucket = 0;
                while (bucket < kBoundsNs.size() && now - lastUpload_ > kBoundsNs[bucket])
                    ++bucket;
                uploadBuckets_[bucket].fetch_add(1, std::memory_order_relaxed);
            } else if (lastUpload_ != 0) {
                invalid_.fetch_add(1, std::memory_order_relaxed);
            }
            lastUpload_ = now;
            lastUploadKey_ = key;
            uploads_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    void uploadError() {
        if (enabled())
            uploadErrors_.fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t uploads() const { return uploads_.load(std::memory_order_relaxed); }
    uint64_t uploadErrors() const { return uploadErrors_.load(std::memory_order_relaxed); }
    uint64_t uploadBucket(unsigned index) const {
        return index < uploadBuckets_.size() ? uploadBuckets_[index].load(std::memory_order_relaxed) : 0;
    }
    uint64_t bucket(unsigned index) const {
        return index < buckets_.size() ? buckets_[index].load(std::memory_order_relaxed) : 0;
    }
    double gameSeconds() const { return double(gameNs_.load(std::memory_order_relaxed)) / 1000000000.0; }
    uint64_t updates() const { return updates_.load(std::memory_order_relaxed); }
    uint64_t invalid() const { return invalid_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> enabled_{false};
    uint64_t lastUploadKey_ = 0; // One window-thread producer.
    int64_t lastUpload_ = 0;
    int64_t last_ = 0; // One graphics-worker producer; consumers read the counters only.
    std::array<std::atomic<uint64_t>, 8> buckets_{};
    std::array<std::atomic<uint64_t>, 8> uploadBuckets_{};
    std::atomic<uint64_t> gameNs_{0}, updates_{0}, invalid_{0}, uploads_{0}, uploadErrors_{0};
};
inline FrameMetrics g_frameMetrics;
} // namespace drakan_presentation
