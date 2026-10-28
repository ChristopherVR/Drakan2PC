#pragma once
#include "drakan_portable_camera.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <mutex>

// Window events and guest pad reads run on different threads and at different
// rates. Retain short button presses until a guest read can observe them.
class HostPadState {
    std::mutex mutex_;
    std::array<uint8_t, 32> sample_{};
    std::array<uint8_t, 32> frame_{};
    bool frameLatched_ = false;
    bool frameMouseLook_ = false;
    uint16_t pressed_ = 0;
    uint16_t suppressed_ = 0;
    uint32_t context_ = 0;
    float mouseX_ = 0, mouseY_ = 0;
    float consumedX_ = 0, consumedY_ = 0;
    HostMouseFrame mouseFrame_;
    bool mouseCaptured_ = false;
    bool mouseOwner_ = true, focused_ = true;

    static std::array<uint8_t, 32> neutralSample() {
        std::array<uint8_t, 32> sample{};
        sample[0] = 1;
        sample[1] = 0x73;
        sample[2] = sample[3] = 0xff;
        for (unsigned i = 4; i < 8; ++i)
            sample[i] = 0x80;
        return sample;
    }

    std::array<uint8_t, 32> consumeLocked(uint32_t context, bool directMouse, float frameSeconds) {
        const uint16_t physical = uint16_t(sample_[2]) | (uint16_t(sample_[3]) << 8);
        if (context != context_) {
            // A held confirm must not also answer a newly opened dialog.
            suppressed_ |= static_cast<uint16_t>(~physical);
            pressed_ = 0;
            context_ = context;
            mouseX_ = mouseY_ = 0;
        }
        // Drop pending samples from a suppressed hold before processing release;
        // otherwise its final down sample could reappear as a fresh press.
        pressed_ &= static_cast<uint16_t>(~suppressed_);
        suppressed_ &= static_cast<uint16_t>(~physical);
        auto result = sample_;
        const uint16_t buttons = static_cast<uint16_t>((physical & ~pressed_) | suppressed_);
        result[2] = static_cast<uint8_t>(buttons);
        result[3] = static_cast<uint8_t>(buttons >> 8);
        pressed_ = 0;
        consumedX_ = mouseCaptured_ ? mouseX_ : 0;
        consumedY_ = mouseCaptured_ ? mouseY_ : 0;
        if (!directMouse && mouseCaptured_ && sample_[4] == 128 && sample_[5] == 128 &&
            (mouseX_ != 0 || mouseY_ != 0)) {
            const float gain = 5.0f / std::clamp(frameSeconds * 30.0f, 0.125f, 7.5f);
            result[4] = static_cast<uint8_t>(std::clamp(128.0f + mouseX_ * gain, 0.0f, 255.0f));
            result[5] = static_cast<uint8_t>(std::clamp(128.0f - mouseY_ * gain, 0.0f, 255.0f));
        }
        mouseX_ = mouseY_ = 0;
        return result;
    }

public:
    HostPadState() : sample_(neutralSample()) {}
    void publish(const std::array<uint8_t, 32>& sample, float mouseX = 0, float mouseY = 0, bool captured = false,
                 bool mouseActivity = false, bool controllerActivity = false, bool focused = true) {
        std::lock_guard lock(mutex_);
        pressed_ |= static_cast<uint16_t>(~(uint16_t(sample[2]) | (uint16_t(sample[3]) << 8)));
        sample_ = focused ? sample : neutralSample();
        focused_ = focused;
        if (focused) {
            if (controllerActivity)
                mouseOwner_ = false;
            if (mouseActivity)
                mouseOwner_ = true;
        }
        if (!focused) {
            pressed_ = suppressed_ = 0;
        }
        mouseCaptured_ = captured && focused;
        if (mouseCaptured_) {
            mouseX_ += mouseX;
            mouseY_ += mouseY;
        } else {
            mouseX_ = mouseY_ = 0;
        }
    }
    std::array<uint8_t, 32> consume(uint32_t context = 0, int port = 0, int slot = 0, bool directMouse = false,
                                    float frameSeconds = 1.0f / 30.0f) {
        // Other ports must not consume player one's pending input.
        if (port != 0 || slot != 0)
            return neutralSample();
        std::lock_guard lock(mutex_);
        return consumeLocked(context, directMouse, frameSeconds);
    }
    void latchFrame(uint32_t context, bool directMouse = false, float seconds = 1.0f / 30.0f) {
        std::lock_guard lock(mutex_);
        const auto sample = consumeLocked(context, directMouse, seconds);
        frame_ = sample;
        frameLatched_ = true;
        frameMouseLook_ = mouseCaptured_ && sample_[4] == 128 && sample_[5] == 128;
        const bool ownsView = mouseOwner_ && sample_[4] == 128 && sample_[5] == 128;
        mouseFrame_ = {consumedX_, consumedY_, seconds, ownsView && focused_, ownsView};
    }
    HostMouseFrame mouseFrame() {
        std::lock_guard lock(mutex_);
        return mouseFrame_;
    }
    bool mouseLookActive() {
        std::lock_guard lock(mutex_);
        return frameMouseLook_;
    }
    std::array<uint8_t, 32> readFrame(uint32_t context, int port, int slot) {
        if (port != 0 || slot != 0)
            return neutralSample();
        {
            std::lock_guard lock(mutex_);
            if (frameLatched_)
                return frame_;
        }
        return consume(context);
    }
};
