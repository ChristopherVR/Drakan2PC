#pragma once
#include <chrono>
#include <cmath>

namespace drakan_pacing {
// [video] frame_limit = auto: present at most once per display refresh while vsync is off; with vsync on the swap
// chain already paces to the refresh and the limiter stays out of the way. The ini spelling is "auto"; the settings
// model and the host loop carry it as this value.
inline constexpr int kFrameLimitAuto = -1;

// Interval of the host loop's frame limiter, zero when it does not limit. `setting` is the frame_limit value (0 = off,
// kFrameLimitAuto, or frames per second), `refreshSeconds` the display refresh period (0 when unknown: auto then uses
// 60 Hz). Auto uses the exact rational refresh period (59.997 Hz is not 60), so the cap does not beat against the
// panel by one frame every few seconds.
inline std::chrono::nanoseconds limiterPeriod(int setting, bool vsync, double refreshSeconds) {
    using std::chrono::nanoseconds;
    if (setting > 0)
        return nanoseconds(1000000000LL / setting);
    if (setting != kFrameLimitAuto || vsync)
        return nanoseconds(0);
    // Ignore implausible readings (below 24 Hz or above 1000 Hz).
    const double period = (refreshSeconds >= 0.001 && refreshSeconds <= 1.0 / 24.0) ? refreshSeconds : 1.0 / 60.0;
    return nanoseconds(static_cast<long long>(std::llround(period * 1e9)));
}
} // namespace drakan_pacing
