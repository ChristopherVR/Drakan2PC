// Arokh mouse steering (input/DragonSteer.h): counts -> angles, smoothing, the transform rotations and the pitch clamp,
// plus the dragon_* ini keys, the controls v3 upgrade and the DRAKAN_DRAGON_* overrides. Pure logic, no guest.
#include "input/Config.h"
#include "input/DragonSteer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace drakan_input;

static unsigned checks;
static void check(bool b, const char* name) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
static bool near(double a, double b, double eps = 1e-5) {
    return std::fabs(a - b) <= eps;
}

static void identity(float m[9]) {
    for (int i = 0; i < 9; ++i)
        m[i] = (i % 4 == 0) ? 1.0f : 0.0f;
}
static bool orthonormal(const float m[9]) {
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            double d = 0;
            for (int r = 0; r < 3; ++r)
                d += double(m[r * 3 + a]) * m[r * 3 + b];
            if (!near(d, a == b ? 1.0 : 0.0, 1e-4))
                return false;
        }
    return true;
}

static void setEnv(const char* k, const char* v) {
#if defined(_WIN32)
    _putenv_s(k, v ? v : "");
#else
    if (v)
        setenv(k, v, 1);
    else
        unsetenv(k);
#endif
}

int main() {
    // ---- counts -> angles: linear, no dead zone, no clamp, no acceleration
    {
        DragonSteerConfig c;
        DragonSteer s;
        s.add(1, 0);
        DragonSteerStep a = s.step(c, 1.0f / 30.0f);
        check(near(a.yaw, -0.002), "one count turns by the sensitivity (mouse right = yaw down = turn right)");
        check(s.idle(), "raw mode hands everything out at once");
        s.add(1000, 0);
        a = s.step(c, 1.0f / 30.0f);
        check(near(a.yaw, -2.0, 1e-4), "a 1000-count flick is 1000 x one count (no full-deflection clamp)");
        s.add(10, 0);
        float at30 = s.step(c, 1.0f / 30.0f).yaw;
        s.add(10, 0);
        float at144 = s.step(c, 1.0f / 144.0f).yaw;
        check(near(at30, at144), "the angle per count does not depend on the tick length");
        s.add(0, -5);
        a = s.step(c, 1.0f / 60.0f);
        check(a.pitch > 0 && near(a.pitch, 0.01), "mouse up (dy < 0) raises the nose");
        c.invertY = true;
        s.add(0, -5);
        check(near(s.step(c, 1.0f / 60.0f).pitch, -0.01), "invert_y reverses the pitch");
        c.invertY = false;
        c.invertX = true;
        s.add(5, 0);
        check(near(s.step(c, 1.0f / 60.0f).yaw, 0.01), "invert_x reverses the turn");
        c.invertX = false;
        c.scale = 0.5f;
        c.sensY = 0.004f;
        s.add(4, 4);
        a = s.step(c, 1.0f / 60.0f);
        check(near(a.yaw, -0.004) && near(a.pitch, -0.008),
              "dragon_sensitivity scales both axes, sensitivity_y the pitch");
    }
    // ---- smoothing spreads counts over ticks without losing any
    {
        DragonSteerConfig c;
        c.smoothingMs = 50.0f;
        DragonSteer s;
        s.add(100, 0);
        double total = 0;
        float first = s.step(c, 1.0f / 60.0f).yaw;
        total += first;
        check(first < 0 && first > -0.2f, "smoothing hands out only part of a flick on its first tick");
        for (int i = 0; i < 200 && !s.idle(); ++i)
            total += s.step(c, 1.0f / 60.0f).yaw;
        check(s.idle(), "smoothing ends: a stop is a stop");
        check(near(total, -0.2, 1e-5), "smoothing keeps every count");
        s.add(100, 0);
        check(s.step(c, 0.0f).yaw == 0.0f && !s.idle(), "a tick without time hands nothing out while smoothing");
        s.reset();
        check(s.idle(), "reset drops pending counts");
    }
    // ---- transform rotations (rows of the guest 3 x 3: column 0 right, 1 up, 2 back)
    {
        float m[9];
        identity(m);
        check(near(dragonYaw(m), 0) && near(dragonPitch(m), 0), "identity is heading 0, level");
        rotateWorldYaw(m, 0.3f);
        check(near(dragonYaw(m), 0.3) && near(dragonPitch(m), 0), "world yaw turns the heading only");
        rotateLocalPitch(m, 0.25f);
        check(near(dragonPitch(m), 0.25) && near(dragonYaw(m), 0.3, 1e-4),
              "local pitch raises the nose, keeps heading");
        check(orthonormal(m), "rotations keep the matrix orthonormal");
        rotateWorldYaw(m, -0.5f);
        check(near(dragonPitch(m), 0.25, 1e-4) && near(dragonYaw(m), -0.2, 1e-4), "yaw while pitched keeps the pitch");
        // a scaled transform keeps its scale
        float k[9];
        identity(k);
        for (float& v : k)
            v *= 2.0f;
        rotateLocalPitch(k, 0.4f);
        rotateWorldYaw(k, 1.0f);
        check(near(std::sqrt(k[0] * k[0] + k[3] * k[3] + k[6] * k[6]), 2.0, 1e-4), "rotations keep column length");
        check(near(dragonPitch(k), 0.4, 1e-4), "pitch reads the same on a scaled transform");
        // banked: pitch about the dragon's own (tilted) right axis still raises the nose
        float b[9];
        identity(b);
        const float roll = 0.5f; // right axis tilted about the back axis
        b[0] = std::cos(roll);
        b[3] = std::sin(roll);
        b[1] = -std::sin(roll);
        b[4] = std::cos(roll);
        rotateLocalPitch(b, 0.2f);
        check(dragonPitch(b) > 0.15f && orthonormal(b), "pitch while banked raises the nose");
    }
    // ---- pitch clamp
    {
        check(near(clampPitchDelta(0.0f, 0.3f, -1.0f, 1.0f), 0.3), "inside the window: unchanged");
        check(near(clampPitchDelta(0.9f, 0.3f, -1.0f, 1.0f), 0.1), "stops at the nose-up limit");
        check(near(clampPitchDelta(-0.95f, -0.3f, -1.0f, 1.0f), -0.05), "stops at the nose-down limit");
        check(clampPitchDelta(1.2f, 0.1f, -1.0f, 1.0f) == 0.0f, "beyond the limit: no further up");
        check(near(clampPitchDelta(1.2f, -0.1f, -1.0f, 1.0f), -0.1), "beyond the limit: back down is free");
        check(clampPitchDelta(0.2f, 0.0f, -1.0f, 1.0f) == 0.0f, "no motion, no change");
    }
    // ---- ini keys, the v3 upgrade, env overrides
    {
        Config d = Config::defaults();
        check(!d.dragonStick && d.dragonStrafe && d.dragonScale == 1.0f && d.dragonSmoothingMs == 0.0f,
              "defaults: mouse steering, A / D strafe, scale 1, raw");
        check(d.controlsVersion == 3, "the default file is controls v3");
        Config c = Config::defaults();
        std::string warn;
        Config::parse("[mouse]\ndragon_mode = stick\ndragon_sensitivity = 0.7\ndragon_smoothing = 20\n"
                      "dragon_pitch_up = 0.8\ndragon_pitch_down = 0.6\n[keyboard]\ndragon_strafe = 0\n",
                      c, &warn);
        check(warn.empty(), "dragon keys parse");
        check(c.dragonStick && near(c.dragonScale, 0.7) && near(c.dragonSmoothingMs, 20) && !c.dragonStrafe,
              "dragon keys are read");
        check(near(c.dragonPitchMax, 0.8) && near(c.dragonPitchMin, -0.6), "pitch limits: up positive, down negative");
        Config::parse("[mouse]\ndragon_mode = direct\n", c, &warn);
        check(!c.dragonStick, "direct is the mouse mode");
        Config::parse("[mouse]\ndragon_mode = sideways\n", c, &warn);
        check(!warn.empty(), "an unknown dragon_mode warns");

        const std::string v2 = "[general]\r\nlook_mode = direct\r\ncontrols_version = 2       ; 2 = ...\r\n[mouse]\r\n"
                               "sensitivity_x = 0.0031\r\ndragon_mode = stick        ; old comment\r\n";
        const std::string up = Config::upgradeText(v2, 2);
        check(up.find("dragon_mode = mouse") != std::string::npos &&
                  up.find("dragon_mode = stick") == std::string::npos,
              "v3 upgrade switches the old written default to mouse");
        check(up.find("controls_version = 3") != std::string::npos &&
                  up.find("controls_version = 2") == std::string::npos,
              "v3 upgrade sets the version in place");
        check(up.find("sensitivity_x = 0.0031\r\n") != std::string::npos, "v3 upgrade keeps other lines byte for byte");
        Config u = Config::defaults();
        u.controlsVersion = 0;
        Config::parse(up, u, &warn);
        check(!u.dragonStick && u.controlsVersion == 3 && near(u.sensX, 0.0031), "the upgraded file reads as v3");
        const std::string v0 = "[mouse]\nsensitivity_x = 0.002\n";
        const std::string up0 = Config::upgradeText(v0, 0);
        check(up0.find("controls v2") != std::string::npos && up0.find("controls_version = 3") != std::string::npos &&
                  up0.find("controls_version = 2") == std::string::npos,
              "a pre-v2 file gets the v2 block, then version 3");

        setEnv("DRAKAN_DRAGON_MODE", "stick");
        setEnv("DRAKAN_DRAGON_SENS", "1.5");
        setEnv("DRAKAN_DRAGON_SMOOTHING", "12");
        setEnv("DRAKAN_DRAGON_STRAFE", "0");
        Config e = Config::defaults();
        Config::applyEnvOverrides(e);
        check(e.dragonStick && near(e.dragonScale, 1.5) && near(e.dragonSmoothingMs, 12) && !e.dragonStrafe,
              "DRAKAN_DRAGON_* override the file");
        setEnv("DRAKAN_DRAGON_MODE", "mouse");
        setEnv("DRAKAN_DRAGON_SENS", "-3");
        Config::applyEnvOverrides(e);
        check(!e.dragonStick && near(e.dragonScale, 1.5), "a negative sensitivity override is ignored");
        setEnv("DRAKAN_DRAGON_MODE", nullptr);
        setEnv("DRAKAN_DRAGON_SENS", nullptr);
        setEnv("DRAKAN_DRAGON_SMOOTHING", nullptr);
        setEnv("DRAKAN_DRAGON_STRAFE", nullptr);
    }
    std::printf("dragon_steer_test: %u checks passed\n", checks);
    return 0;
}
