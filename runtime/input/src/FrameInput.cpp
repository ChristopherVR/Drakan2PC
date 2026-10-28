#include "input/FrameInput.h"
#include <algorithm>
#include <chrono>
#include <limits>

namespace drakan_input {

void InputAccumulator::onKey(int dik, bool down) {
    if (dik < 0 || dik > 255)
        return;
    std::lock_guard<std::mutex> l(m_);
    if (heldOver_[dik]) {
        // Down through a releaseAll (ownership change): Raw Input's auto-repeat of it is no new press; armed again
        // once it is released.
        if (!down)
            heldOver_[dik] = false;
        return;
    }
    if (down_[dik] != down)
        keyEdges_[dik].push_back(down);
    down_[dik] = down;
}
void InputAccumulator::onMouseButton(uint8_t b, bool down) {
    std::lock_guard<std::mutex> l(m_);
    for (int i = 0; i < 5; ++i) {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        if ((b & bit) && ((mDown_ & bit) != 0) != down)
            mouseEdges_[i].push_back(down);
    }
    if (down)
        mDown_ |= b;
    else
        mDown_ &= static_cast<uint8_t>(~b);
}
int64_t InputAccumulator::nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void InputAccumulator::onMouseMove(int dx, int dy) {
    onMouseMove(dx, dy, nowUs());
}
void InputAccumulator::onMouseMove(int dx, int dy, int64_t tUs) {
    std::lock_guard<std::mutex> l(m_);
    motion_.push_back({tUs, dx, dy});
}
void InputAccumulator::onWheel(int n) {
    std::lock_guard<std::mutex> l(m_);
    wheel_ += n;
}
void InputAccumulator::releaseAll() {
    std::lock_guard<std::mutex> l(m_);
    // Cancel queued attacks on focus/context loss; only release states the game
    // actually saw. Never replay an old click into the newly focused menu.
    for (auto& edges : keyEdges_)
        edges.clear();
    for (auto& edges : mouseEdges_)
        edges.clear();
    pressed_.reset();
    mPressed_ = 0;
    released_ |= deliveredKeys_;
    heldOver_ |= down_;
    down_.reset();
    deliveredKeys_.reset();
    mReleased_ |= deliveredMouse_;
    mDown_ = 0;
    deliveredMouse_ = 0;
    motion_.clear();
    wheel_ = 0;
}
void InputAccumulator::consume(FrameInput& o) {
    consume(o, std::numeric_limits<int64_t>::max());
}
void InputAccumulator::consume(FrameInput& o, int64_t cutoffUs) {
    std::lock_guard<std::mutex> l(m_);
    for (size_t i = 0; i < 256; ++i) {
        auto& edges = keyEdges_[i];
        if (edges.empty() || released_[i])
            continue;
        const bool down = edges.front();
        edges.pop_front();
        deliveredKeys_[i] = down;
        if (down)
            pressed_[i] = true;
        else
            released_[i] = true;
    }
    for (int i = 0; i < 5; ++i) {
        auto& edges = mouseEdges_[i];
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        if (edges.empty() || (mReleased_ & bit))
            continue;
        const bool down = edges.front();
        edges.pop_front();
        if (down) {
            deliveredMouse_ |= bit;
            mPressed_ |= bit;
        } else {
            deliveredMouse_ &= static_cast<uint8_t>(~bit);
            mReleased_ |= bit;
        }
    }
    o.keyDown = deliveredKeys_;
    o.keyPressed = pressed_;
    o.keyReleased = released_;
    o.mouseDown = deliveredMouse_;
    o.mousePressed = mPressed_;
    o.mouseReleased = mReleased_;
    // No per-frame mouse speed cap: preserve all polling packets up to the cutoff. FrameInput carries int counts, so
    // a packet that would overflow it stays queued rather than dropping displacement after a delayed poll.
    long long sx = 0, sy = 0;
    while (!motion_.empty() && motion_.front().tUs <= cutoffUs) {
        const Motion& m = motion_.front();
        const long long nx = sx + m.dx, ny = sy + m.dy;
        if (nx > std::numeric_limits<int>::max() || nx < std::numeric_limits<int>::min() ||
            ny > std::numeric_limits<int>::max() || ny < std::numeric_limits<int>::min())
            break;
        sx = nx;
        sy = ny;
        motion_.pop_front();
    }
    o.dx = static_cast<int>(sx);
    o.dy = static_cast<int>(sy);
    o.wheel = wheel_;
    pressed_.reset();
    released_.reset();
    mPressed_ = mReleased_ = 0;
    wheel_ = 0;
}

} // namespace drakan_input
