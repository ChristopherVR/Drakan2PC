#include "world/Exchange.h"

#include <utility>

namespace world {

SnapshotExchange::SnapshotExchange() : back_(std::make_shared<Snapshot>()) {}

Snapshot& SnapshotExchange::back() {
    return *back_;
}

void SnapshotExchange::publish() {
    std::lock_guard<std::mutex> lock(m_);
    std::swap(front_, back_);
    ++count_;
    // The old front becomes the next back buffer unless a reader still holds it.
    if (!back_ || back_.use_count() > 1)
        back_ = std::make_shared<Snapshot>();
}

std::shared_ptr<const Snapshot> SnapshotExchange::acquire() const {
    std::lock_guard<std::mutex> lock(m_);
    return front_;
}

uint64_t SnapshotExchange::published() const {
    std::lock_guard<std::mutex> lock(m_);
    return count_;
}

} // namespace world
