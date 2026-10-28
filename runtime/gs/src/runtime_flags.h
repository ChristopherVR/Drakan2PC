// Environment-variable configuration of the gs library (debug aids, test hooks and expert overrides).
//
// Every GSR_* / DRAKAN_* variable the renderer understands is parsed here, once, into a typed field of RuntimeFlags;
// the rest of the library reads fields from runtimeFlags() instead of calling std::getenv. Each field documents its
// variable name, parse rule and default. The same list, with effects, is in docs/reference/env-flags.md.
//
// Parse rules (kept identical to the inline getenv code this replaced):
//   presence      - the variable exists, whatever its value (including empty)
//   not-"0"       - exists and is not exactly the string "0" (empty counts as on)
//   first-char    - exists and its first character is / is not '0' (or '1')
//   atoi          - std::atoi(value) != 0 (or the numeric value itself)
//   string        - the raw value, empty string included
//
// Timing: runtimeFlags() parses the environment on first use (thread-safe) and the result is immutable afterwards, so
// every reader sees the same values for the life of the process. Nothing in the repository changes the environment of a
// running renderer, so this equals the previous mix of "read in a constructor" and "read every frame". The per-frame
// readers that used to call getenv in hot paths now cost one load; no flag is deliberately live.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gsr {

struct RuntimeFlags {
    // ---- modern water / ray tracing (d3d11_backend, ray_water) -------------------------------------------------
    bool modernWater = false;                // GSR_MODERN_WATER: not-"0"; default off
    bool rayReflections = true;              // GSR_RAY_REFLECTIONS: unset or not-"0"; default on
    bool rayShadows = false;                 // GSR_RAY_SHADOWS: not-"0"; default off
    bool rayCpuCache = true;                 // GSR_RAY_CPU_CACHE: first-char != '0'; default on
    bool rayCpuCacheCheck = false;           // GSR_RAY_CPU_CACHE_CHECK: first-char == '1'; default off
    bool surfaceAsync = true;                // GSR_SURFACE_ASYNC: first-char != '0'; surface reconstruction thread
    bool rayWorldCache = true;               // GSR_RAY_WORLD_CACHE: first-char != '0'; default on
    bool rayPackedTextures = true;           // GSR_RAY_PACKED_TEXTURES: first-char != '0'; default on
    uint32_t waterCoverageDebug = 0;         // GSR_WATER_COVERAGE_DEBUG: atoi; default 0
    std::optional<float> waterTime;          // GSR_WATER_TIME: strtof; fixes the water clock (site rejects non-finite)
    std::optional<std::string> raySceneDump; // GSR_RAY_SCENE_DUMP: string; ray scene dump path (world epoch 200 only)
    bool waterLog = false;                   // GSR_WATER_LOG: presence

    // ---- temporal upscaler / DLSS (d3d11_backend, temporal, dlss_ngx) --------------------------------------------
    std::optional<uint32_t> upscaleMode;   // GSR_UPSCALE: dlaa=1 quality=2 balanced=3 performance=4, anything else 0
                                           // (case-insensitive); overrides Config::upscaleMode when set
    std::optional<uint32_t> upscalePreset; // GSR_UPSCALE_PRESET: j=10 k=11 l=12 m=13, anything else 0 (auto)
    std::optional<float> upscaleSharp;     // GSR_UPSCALE_SHARP: atof; overrides Config::upscaleSharpness
    bool upscaleTiming = false;            // GSR_UPSCALE_TIMING: presence
    bool dlssLog = false;                  // DRAKAN_DLSS_LOG: presence (backend tpLog and NGX log callback)
    std::optional<std::string> dlssDir;    // DRAKAN_DLSS_DIR: string; extra folder searched for nvngx_dlss.dll
    std::optional<uint32_t> dlssDebugMode; // DRAKAN_DLSS_DEBUG: "mv" (case-insensitive) = 1, any other value = 0
    bool dlssValidate = false;             // DRAKAN_DLSS_VALIDATE: presence
    bool dlssPresentLog = false;           // DRAKAN_DLSS_PRESENT_LOG: presence
    bool motionValidate = false;           // DRAKAN_MOTION_VALIDATE: presence
    bool motionLog = false;                // DRAKAN_MOTION_LOG: presence
    std::optional<std::string> temporalDumpDir;   // GSR_TEMPORAL_DUMP_DIR: string; with one of the two below, dumps
    std::optional<std::string> temporalDumpFrame; // GSR_TEMPORAL_DUMP_FRAME: string, strtoull at the use site
    std::optional<std::string> temporalDumpEpoch; // GSR_TEMPORAL_DUMP_EPOCH: string, strtoull at the use site

    // ---- matched on/off comparisons and world screenshots (d3d11_backend) -----------------------------------------
    std::optional<uint64_t> matchedCompareEpoch;     // GSR_MATCHED_COMPARE_EPOCH: strtoull; world epoch to compare
    std::optional<std::string> matchedCompareEffect; // GSR_MATCHED_COMPARE_EFFECT: water | models | shadows | other
    std::optional<std::string> matchedComparePrefix; // GSR_MATCHED_COMPARE: output path prefix
    std::optional<uint64_t> worldShotAt;             // GSR_WORLD_SHOT_AT: strtoull; world epoch to screenshot
    std::optional<std::string> worldShotPrefix;      // GSR_WORLD_SHOT: output path prefix (used with the above)

    // ---- surface picking / audit (d3d11_backend) ------------------------------------------------------------------
    std::optional<std::string> surfacePick;  // GSR_SURFACE_PICK: "x,y" raster pixel to report
    uint64_t surfacePickEpoch = 70;          // GSR_SURFACE_PICK_EPOCH: strtoull; default 70
    std::optional<std::string> surfaceAudit; // GSR_SURFACE_AUDIT: string (output directory); presence enables

    // ---- texture pack / upscaling overrides of the Config texture options (d3d11_backend) ---------------------------
    std::optional<uint32_t> texUpFactor; // GSR_TEXUP: auto=0 off=1 else atoi; overrides Config::textureUpscale
    std::optional<uint32_t> texUpFilter; // GSR_TEXUP_FILTER: xbrz=1 smooth=2 else 0 (texup::Algo values)
    std::optional<uint32_t> aniso;       // GSR_ANISO: atoi
    struct MipBias {
        bool automatic = false; // value was "auto" (case-insensitive)
        float value = 0.0f;     // atof otherwise
    };
    std::optional<MipBias> mipBias;    // GSR_MIPBIAS: "auto" or atof
    std::optional<bool> texDump;       // GSR_TEXDUMP: atoi != 0
    std::optional<bool> texReplace;    // GSR_TEXREPLACE: atoi != 0
    std::optional<std::string> texDir; // GSR_TEXDIR: string
    std::optional<int> texBudget;      // GSR_TEXBUDGET: atoi MiB (<= 0 means auto, min 64 otherwise)
    std::optional<bool> texUpSync;     // GSR_TEXUP_SYNC: atoi != 0
    std::optional<bool> paletteHd;     // GSR_PALETTE_HD: atoi != 0 (default on)
    // GSR_PALETTE_HD_DUMP: folder for the first recoloured palette steps (debug)
    std::optional<std::string> paletteHdDump;
    bool texUpLog = false;             // GSR_TEXUP_LOG: presence
    bool texLog = false;               // GSR_TEXLOG: presence
    bool lodStat = false;              // GSR_LODSTAT: presence
    std::optional<std::string> hlDump; // GSR_HLDUMP: string (directory); menu highlight debug dumps

    // ---- HUD / menu ------------------------------------------------------------------------------------------------
    std::optional<float> hudScale;       // GSR_HUDSCALE: atof; overrides Config::hudScaleX
    std::optional<bool> smoothHighlight; // GSR_SMOOTHHL: atoi != 0; overrides Config::smoothMenuHighlight
    bool spriteFootprint = true;         // GSR_SPRITE_FOOTPRINT: atoi != 0 when set; default on
    bool hudLog = false;                 // GSR_HUDLOG: presence
    bool hudCanvasLog = false;           // GSR_HUD_CANVAS_LOG: presence
    bool hudCornerLog = false;           // GSR_HUD_CORNER_LOG: presence
    uint64_t hudCornerFrom = 0;          // GSR_HUD_CORNER_FROM: strtoull; default 0 (used with the above)
    bool subtitleLog = false;            // GSR_SUBTITLE_LOG: presence
    int64_t inventoryDrawLogDelay = -1;  // GSR_INVENTORY_DRAWLOG: max(0, strtoll); -1 when unset

    // ---- draw / render-target debugging (d3d11_backend)
    // --------------------------------------------------------------
    int64_t drawLogFirst = -1, drawLogLast = -1;   // GSR_DRAWLOG: "a" or "a-b" (vsync frames), -1 when unset/invalid
    int64_t drawLogTbp = -1;                       // GSR_DRAWLOGTBP: strtol base 0; log draws sampling this TBP
    uint32_t drawLogVerts = 3;                     // GSR_DRAWLOGV: atoi; vertices printed per logged draw
    int64_t drawStop = -1;                         // GSR_DRAWSTOP: atoll; run only the first n draws
    int64_t drawSkipFirst = -1, drawSkipLast = -1; // GSR_DRAWSKIP: "a-b" (needs both numbers), skip draws a..b
    std::vector<int64_t> rtDumpDraws; // GSR_RTDUMP: comma-separated draw indices (strtoll), dump after each
    int64_t rtDumpFrameFirst = -1, rtDumpFrameLast = -1; // GSR_RTDUMPD: "a" or "a-b"; dump after every draw of frames
    uint32_t rtDumpRows = 0;                             // GSR_RTDUMP_ROWS: atoi; minimum rows read back for dumps
    std::string rtDumpDir = ".";                         // GSR_RTDUMP_DIR: string; default "."
    std::optional<std::pair<long, long>>
        rtDumpFrames;           // GSR_RTDUMPF: "a-b" (both numbers); dump all targets at frame end
    bool rtList = false;        // GSR_RTLIST: presence; list targets in verbose draw logs
    bool nativeReadback = true; // GSR_NATIVE_READBACK: unset or not-"0"; default on

    // ---- window screenshots (d3d11_backend)
    // ----------------------------------------------------------------------------
    std::optional<std::string> winShotPrefix; // GSR_WINSHOT: output prefix; enables window screenshots
    bool winShotByPresent = false;            // GSR_WINSHOT_PRESENT: presence (only honoured with GSR_WINSHOT)
    std::vector<uint64_t> winShotAt;          // GSR_WINSHOT_AT: comma list (strtoull), sorted; default "600"

    // ---- gs_core / profiling
    // ------------------------------------------------------------------------------------------
    bool xferLog = false;        // GSR_XFERLOG: presence; log every transfer start
    std::optional<bool> seamFix; // GSR_SEAMFIX: first-char != '0'; overrides Config::fixSpriteSeams
    double profileSeconds = 0.0; // GSR_PROFILE: strtod n; > 0 enables the CPU profiler, reporting
                                 // every n seconds (n outside [2,60] reports every 2)
};

// Parsed on first call, immutable afterwards.
const RuntimeFlags& runtimeFlags();

// Pointer to the string inside an optional flag, or nullptr when the variable was unset (the getenv shape).
inline const char* cstr(const std::optional<std::string>& value) {
    return value ? value->c_str() : nullptr;
}

} // namespace gsr
