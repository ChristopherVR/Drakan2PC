// Parses the environment into RuntimeFlags. See runtime_flags.h for the per-flag documentation.
#include "runtime_flags.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gsr {
namespace {

const char* env(const char* name) {
    return std::getenv(name);
}
bool present(const char* name) {
    return env(name) != nullptr;
}
// "not-"0"": set, and not exactly "0"
bool setAndNotZero(const char* name) {
    const char* v = env(name);
    return v && std::strcmp(v, "0");
}
// unset or not-"0"
bool unsetOrNotZero(const char* name) {
    const char* v = env(name);
    return !v || std::strcmp(v, "0");
}
// first-char != '0', unset counts as on
bool firstCharNotZeroDefaultOn(const char* name) {
    const char* v = env(name);
    return !(v && v[0] == '0');
}
std::optional<std::string> optString(const char* name) {
    const char* v = env(name);
    return v ? std::optional<std::string>(v) : std::nullopt;
}
bool iequals(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

RuntimeFlags parse() {
    RuntimeFlags f;

    // modern water / ray tracing
    f.modernWater = setAndNotZero("GSR_MODERN_WATER");
    f.rayReflections = unsetOrNotZero("GSR_RAY_REFLECTIONS");
    f.rayShadows = setAndNotZero("GSR_RAY_SHADOWS");
    f.rayCpuCache = firstCharNotZeroDefaultOn("GSR_RAY_CPU_CACHE");
    f.surfaceAsync = firstCharNotZeroDefaultOn("GSR_SURFACE_ASYNC");
    if (const char* e = env("GSR_RAY_CPU_CACHE_CHECK"))
        f.rayCpuCacheCheck = e[0] == '1';
    f.rayWorldCache = firstCharNotZeroDefaultOn("GSR_RAY_WORLD_CACHE");
    f.rayPackedTextures = firstCharNotZeroDefaultOn("GSR_RAY_PACKED_TEXTURES");
    if (const char* e = env("GSR_WATER_COVERAGE_DEBUG"))
        f.waterCoverageDebug = uint32_t(std::atoi(e));
    if (const char* e = env("GSR_WATER_TIME"))
        f.waterTime = std::strtof(e, nullptr);
    f.raySceneDump = optString("GSR_RAY_SCENE_DUMP");
    f.waterLog = present("GSR_WATER_LOG");

    // temporal upscaler / DLSS
    if (const char* e = env("GSR_UPSCALE"))
        f.upscaleMode = iequals(e, "dlaa")          ? 1u
                        : iequals(e, "quality")     ? 2u
                        : iequals(e, "balanced")    ? 3u
                        : iequals(e, "performance") ? 4u
                                                    : 0u;
    if (const char* e = env("GSR_UPSCALE_PRESET"))
        f.upscalePreset = iequals(e, "j")   ? 10u
                          : iequals(e, "k") ? 11u
                          : iequals(e, "l") ? 12u
                          : iequals(e, "m") ? 13u
                                            : 0u;
    if (const char* e = env("GSR_UPSCALE_SHARP"))
        f.upscaleSharp = (float)std::atof(e);
    f.upscaleTiming = present("GSR_UPSCALE_TIMING");
    f.dlssLog = present("DRAKAN_DLSS_LOG");
    f.dlssDir = optString("DRAKAN_DLSS_DIR");
    if (const char* e = env("DRAKAN_DLSS_DEBUG"))
        f.dlssDebugMode = iequals(e, "mv") ? 1u : 0u;
    f.dlssValidate = present("DRAKAN_DLSS_VALIDATE");
    f.dlssPresentLog = present("DRAKAN_DLSS_PRESENT_LOG");
    f.motionValidate = present("DRAKAN_MOTION_VALIDATE");
    f.motionLog = present("DRAKAN_MOTION_LOG");
    f.temporalDumpDir = optString("GSR_TEMPORAL_DUMP_DIR");
    f.temporalDumpFrame = optString("GSR_TEMPORAL_DUMP_FRAME");
    f.temporalDumpEpoch = optString("GSR_TEMPORAL_DUMP_EPOCH");

    // matched comparisons / world screenshots
    if (const char* e = env("GSR_MATCHED_COMPARE_EPOCH"))
        f.matchedCompareEpoch = std::strtoull(e, nullptr, 10);
    f.matchedCompareEffect = optString("GSR_MATCHED_COMPARE_EFFECT");
    f.matchedComparePrefix = optString("GSR_MATCHED_COMPARE");
    if (const char* e = env("GSR_WORLD_SHOT_AT"))
        f.worldShotAt = std::strtoull(e, nullptr, 10);
    f.worldShotPrefix = optString("GSR_WORLD_SHOT");

    // surface picking / audit
    f.surfacePick = optString("GSR_SURFACE_PICK");
    if (const char* e = env("GSR_SURFACE_PICK_EPOCH"))
        f.surfacePickEpoch = std::strtoull(e, nullptr, 10);
    f.surfaceAudit = optString("GSR_SURFACE_AUDIT");

    // texture options
    if (const char* v = env("GSR_TEXUP"))
        f.texUpFactor = iequals(v, "auto") ? 0u : iequals(v, "off") ? 1u : (uint32_t)std::atoi(v);
    if (const char* v = env("GSR_TEXUP_FILTER"))
        f.texUpFilter = iequals(v, "xbrz") ? 1u : iequals(v, "smooth") ? 2u : 0u;
    if (const char* v = env("GSR_ANISO"))
        f.aniso = (uint32_t)std::atoi(v);
    if (const char* v = env("GSR_MIPBIAS")) {
        RuntimeFlags::MipBias m;
        m.automatic = iequals(v, "auto");
        m.value = m.automatic ? 0.0f : (float)std::atof(v);
        f.mipBias = m;
    }
    if (const char* v = env("GSR_TEXDUMP"))
        f.texDump = std::atoi(v) != 0;
    if (const char* v = env("GSR_TEXREPLACE"))
        f.texReplace = std::atoi(v) != 0;
    f.texDir = optString("GSR_TEXDIR");
    if (const char* v = env("GSR_TEXBUDGET"))
        f.texBudget = std::atoi(v);
    if (const char* v = env("GSR_TEXUP_SYNC"))
        f.texUpSync = std::atoi(v) != 0;
    if (const char* v = env("GSR_PALETTE_HD"))
        f.paletteHd = std::atoi(v) != 0;
    f.paletteHdDump = optString("GSR_PALETTE_HD_DUMP");
    f.texUpLog = present("GSR_TEXUP_LOG");
    f.texLog = present("GSR_TEXLOG");
    f.lodStat = present("GSR_LODSTAT");
    f.hlDump = optString("GSR_HLDUMP");

    // HUD / menu
    if (const char* v = env("GSR_HUDSCALE"))
        f.hudScale = (float)std::atof(v);
    if (const char* v = env("GSR_SMOOTHHL"))
        f.smoothHighlight = std::atoi(v) != 0;
    if (const char* p = env("GSR_SPRITE_FOOTPRINT"))
        f.spriteFootprint = std::atoi(p) != 0;
    f.hudLog = present("GSR_HUDLOG");
    f.hudCanvasLog = present("GSR_HUD_CANVAS_LOG");
    f.hudCornerLog = present("GSR_HUD_CORNER_LOG");
    if (const char* e = env("GSR_HUD_CORNER_FROM"))
        f.hudCornerFrom = std::strtoull(e, nullptr, 10);
    f.subtitleLog = present("GSR_SUBTITLE_LOG");
    if (const char* p = env("GSR_INVENTORY_DRAWLOG"))
        f.inventoryDrawLogDelay = std::max<int64_t>(0, std::strtoll(p, nullptr, 10));

    // draw / render-target debugging
    if (const char* dl = env("GSR_DRAWLOG")) {
        long a = 0, b = 0;
        if (std::sscanf(dl, "%ld-%ld", &a, &b) >= 1) {
            f.drawLogFirst = a;
            f.drawLogLast = (std::sscanf(dl, "%ld-%ld", &a, &b) == 2) ? b : a;
        }
    }
    if (const char* dt = env("GSR_DRAWLOGTBP"))
        f.drawLogTbp = std::strtol(dt, nullptr, 0);
    if (const char* dv = env("GSR_DRAWLOGV"))
        f.drawLogVerts = (uint32_t)std::atoi(dv);
    if (const char* ds = env("GSR_DRAWSTOP"))
        f.drawStop = std::atoll(ds);
    if (const char* dk = env("GSR_DRAWSKIP")) {
        long a = 0, b = 0;
        if (std::sscanf(dk, "%ld-%ld", &a, &b) == 2) {
            f.drawSkipFirst = a;
            f.drawSkipLast = b;
        }
    }
    if (const char* rd = env("GSR_RTDUMP"))
        for (const char* q = rd; *q;) {
            char* e = nullptr;
            const long long v = std::strtoll(q, &e, 10);
            if (e == q)
                break;
            f.rtDumpDraws.push_back(v);
            if (*e != ',')
                break;
            q = e + 1;
        }
    if (const char* rd = env("GSR_RTDUMPD")) {
        f.rtDumpFrameFirst = std::atoll(rd);
        f.rtDumpFrameLast =
            f.rtDumpFrameFirst >= 0 && std::strchr(rd, '-') ? std::atoll(std::strchr(rd, '-') + 1) : f.rtDumpFrameFirst;
    }
    if (const char* e = env("GSR_RTDUMP_ROWS"))
        f.rtDumpRows = (uint32_t)std::atoi(e);
    if (const char* d = env("GSR_RTDUMP_DIR"))
        f.rtDumpDir = d;
    if (const char* rf = env("GSR_RTDUMPF")) {
        long a = 0, b = 0;
        if (std::sscanf(rf, "%ld-%ld", &a, &b) == 2)
            f.rtDumpFrames = std::make_pair(a, b);
    }
    f.rtList = present("GSR_RTLIST");
    f.nativeReadback = unsetOrNotZero("GSR_NATIVE_READBACK");

    // window screenshots
    if (const char* wsp = env("GSR_WINSHOT")) {
        f.winShotPrefix = wsp;
        f.winShotByPresent = present("GSR_WINSHOT_PRESENT");
        const char* at = env("GSR_WINSHOT_AT");
        for (const char* q = at ? at : "600"; *q;) {
            char* e = nullptr;
            const unsigned long long v = std::strtoull(q, &e, 10);
            if (e == q)
                break;
            f.winShotAt.push_back(v);
            q = (*e == ',') ? e + 1 : e;
        }
        std::sort(f.winShotAt.begin(), f.winShotAt.end());
    }

    // gs_core / profiling
    f.xferLog = present("GSR_XFERLOG");
    if (const char* e = env("GSR_SEAMFIX"))
        f.seamFix = e[0] != '0';
    if (const char* value = env("GSR_PROFILE")) {
        const double n = std::strtod(value, nullptr);
        if (n > 0)
            f.profileSeconds = n >= 2 && n <= 60 ? n : 2;
    }
    return f;
}

} // namespace

const RuntimeFlags& runtimeFlags() {
    static const RuntimeFlags parsed = parse();
    return parsed;
}

} // namespace gsr
