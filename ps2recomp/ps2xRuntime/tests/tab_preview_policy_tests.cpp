#include "../src/lib/drakan_tab_preview_policy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace drakan_tab::detail;

static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}
static bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

// The camera of the live run tabc-before (ram_char_00): camera object matrix (rows) and position, Rynn's root.
static CameraFrame liveCamera() {
    CameraFrame c;
    c.pos = {424152.7f, 161290.4f, 502822.6f};
    const float m[9] = {0.107f, 0.183f, -0.977f, 0.0f, 0.983f, 0.184f, 0.994f, -0.02f, 0.105f};
    for (int i = 0; i < 9; ++i)
        c.m[i] = m[i];
    return c;
}

int main() {
    // Identity camera looking down -z: x / depth.
    {
        CameraFrame cam;
        const Vec3 pts[] = {{1, 0, -10}, {-3, 0, -10}, {5, 0, 10} /* behind: ignored */};
        float x = 0;
        check(skeletonViewX(pts, 3, cam, x), "points in front give a direction");
        check(near(x, -0.1f), "mean of x / depth over the points in front");
        check(!skeletonViewX(pts + 2, 1, cam, x), "only points behind the camera: no direction");
        const Vec3 bad[] = {{std::numeric_limits<float>::quiet_NaN(), 0, -10}, {2, 0, -10}};
        check(skeletonViewX(bad, 2, cam, x) && near(x, 0.2f), "non-finite joints are skipped");
        check(!skeletonViewX(pts, 0, cam, x), "no joints: no direction");
    }
    // Live camera: Rynn's root (424575.09, 161011.5, 502777.03) is straight ahead (the native camera aims at her), a
    // point 50 units to the camera's right lands right of the centre.
    {
        const CameraFrame cam = liveCamera();
        const Vec3 root{424575.09f, 161011.5f, 502777.03f};
        float x = 1;
        check(skeletonViewX(&root, 1, cam, x) && std::fabs(x) < 0.01f, "the root is on the camera axis");
        const Vec3 right{root.x + 50 * cam.m[0], root.y + 50 * cam.m[3], root.z + 50 * cam.m[6]};
        check(skeletonViewX(&right, 1, cam, x) && x > 0.09f && x < 0.12f, "camera right is positive view x");
    }
    // Follow: snaps first, eases without overshoot, frame-rate independent.
    {
        Follow f;
        check(near(followStep(f, 0.3f, 1.0f / 60, 0.35f), 0.3f), "first sample snaps");
        check(near(followStep(f, 0.3f, -1, 0.35f), 0.3f), "negative dt snaps");
        float last = 0.3f;
        bool monotone = true;
        for (int i = 0; i < 600; ++i) {
            const float x = followStep(f, 0.0f, 1.0f / 60, 0.35f);
            monotone = monotone && x <= last + 1e-7f && x >= -1e-6f;
            last = x;
        }
        check(monotone, "eases to the target without overshoot");
        check(std::fabs(last) < 1e-4f, "reaches the target");

        Follow a, b;
        followStep(a, 1, 0, 0.35f);
        followStep(b, 1, 0, 0.35f);
        for (int i = 0; i < 60; ++i)
            followStep(a, 0, 1.0f / 60, 0.35f);
        for (int i = 0; i < 15; ++i)
            followStep(b, 0, 1.0f / 15, 0.35f);
        check(near(a.x, b.x, 0.01f), "60 and 15 fps reach the same place after one second");
        check(a.x > 0 && a.x < 0.2f, "one second covers most of a step");

        Follow c;
        followStep(c, 0.5f, 0, 0.35f);
        followStep(c, std::numeric_limits<float>::quiet_NaN(), 1.0f / 60, 0.35f);
        check(near(c.x, 0.5f), "a non-finite target keeps the position");
    }
    // Crop: the fixed placement without a direction; a direction shifts the centre the other way; the crop covers.
    {
        const float w = 512;
        Crop c = previewCrop(0.17f, 0, 229, w, 0.2f);
        check(near(c.offset, (0.17f - 0.5f) * w), "no direction: the fixed offset");
        check(near(c.scale, 1 + 2 * std::fabs(c.offset) / w + 0.01f), "crop covers the screen");
        c = previewCrop(0.17f, 0.1f, 229, w, 0.2f);
        check(near(c.offset, (0.17f - 0.5f) * w - 22.9f, 1e-3f), "a point right of centre moves the centre left");
        check(near(0.5f * w + c.offset + 229 * 0.1f, 0.17f * w, 1e-3f), "the point lands on the host's centre");
        check(c.scale >= 1 + 2 * std::fabs(c.offset) / w, "crop still covers the screen");
        c = previewCrop(0.5f, 10.0f, 229, w, 0.2f);
        check(near(c.offset, -0.2f * w), "the shift is limited");
        c = previewCrop(0.5f, 0.1f, 0, w, 0.2f);
        check(near(c.offset, 0), "no projection: the fixed offset");
        c = previewCrop(0.0f, 0, 229, w, 0.2f);
        check(near(c.offset, -0.4f * w), "the centre is clamped to 0.1 .. 0.9");
        c = previewCrop(0.3f, 0.1f, 229, 0, 0.2f);
        check(near(c.offset, 0) && near(c.scale, 1), "no frame width: neutral");
    }
    {
        // Pillarbox: a 4:3 picture (x 200..1400) on a 1600 px output, the host column centred at 270.
        check(near(previewCentreInPicture(270, 0, 1600, 160), 270.0f / 1600), "no pillarbox: the column's centre");
        check(near(previewCentreInPicture(270, 200, 1200, 160), 160.0f / 1200), "kept half her width inside");
        check(near(previewCentreInPicture(1390, 200, 1200, 160), 1040.0f / 1200), "right edge too");
        check(near(previewCentreInPicture(270, 200, 200, 160), 0.5f), "a narrow picture centres her");
        check(near(previewCentreInPicture(270, 0, 0, 160), 0.5f), "no picture: centre");
    }
    std::printf("tab_preview_policy: %u checks passed\n", checks);
    return 0;
}
