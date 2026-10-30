// Double-buffered publish of world snapshots: the EE thread writes, the UI thread reads an immutable copy.
// Same contract as gameui::ModelExchange: no allocation in steady state, one short mutex section per call.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>

#include "world/Snapshot.h"

namespace world {

class SnapshotExchange {
public:
    SnapshotExchange();
    Snapshot& back();                                // writer only
    void publish();                                  // writer only: back() becomes the front
    std::shared_ptr<const Snapshot> acquire() const; // any thread; null before the first publish
    uint64_t published() const;

private:
    mutable std::mutex m_;
    std::shared_ptr<Snapshot> front_, back_;
    uint64_t count_ = 0;
};

} // namespace world
