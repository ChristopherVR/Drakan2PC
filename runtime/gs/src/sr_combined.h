#pragma once
#include <d3d11.h>
#include <cstdint>
#include <memory>
#include <string>

namespace gsr::sr_combined {
struct Size {
    uint32_t width = 0, height = 0;
};
struct Config {
    Size render, output;
    uint32_t mode = 2, preset = 0; // existing Upscaler modes: 1 DLAA, 2 quality, 3 balanced, 4 performance
};
struct Inputs {
    // Exact render-sized single-mip/slice/sample textures, mip-zero Texture2D SRVs.
    ID3D11ShaderResourceView* color = nullptr;  // RGBA16F, prepared world color, raster-jittered
    ID3D11ShaderResourceView* depth = nullptr;  // R32F, normalized D3D reverse Z; already GS-converted
    ID3D11ShaderResourceView* motion = nullptr; // RG16F FULL motion, previous-current unjittered render pixels
    // Optional output-sized original-alpha surface. Otherwise alpha is selected
    // nearest from input color. Neither path trusts NGX output alpha.
    ID3D11ShaderResourceView* alphaFull = nullptr;
    // Optional render-sized surface whose alpha replaces the input color alpha (same nearest selection). Used when
    // color is a vendor output (render-resolution NR) whose alpha is not trusted. Ignored when alphaFull is set.
    ID3D11ShaderResourceView* alphaRender = nullptr;
    float jitterPixels[2]{}; // current geometry jitter in render pixels (+right,+down)
    bool reset = true;
    // Contiguous pipeline evaluation serial, NOT host present/vsync count.
    // Camera/object motion must refer to the same last ACCEPTED pipeline sample.
    uint64_t frameIndex = 0;
};
struct Texture {
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
};
struct Outputs {
    Texture color, depth, motion, nrOutput; // RGBA16F, R32F, RG16F, RGBA16F; all exact output size
    Size size;
    float motionScale[2]{};  // 1/output dimensions, for NR normalized motion
    float jitterPixels[2]{}; // always zero: NR sees the unjittered SR result
    bool historyReset = true;
    bool srEvaluated = false; // MUST check: false means explicit spatial fallback
};
const char* shaderSource(); // offline compiler/testing; same source used by Pipeline
class Pipeline final {
public:
    // Links the existing createDlssUpscaler implementation; does NOT initialize
    // an independent NGX wrapper or shut down a session shared with the renderer.
    Pipeline(ID3D11Device*, ID3D11DeviceContext*);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    bool ready() const;
    const std::string& status() const;
    // Call BEFORE rendering reduced geometry. Success means actual NGX feature
    // creation succeeded, not just shader compilation. Changes reset SR and NR.
    bool configure(const Config&);
    static void jitter(uint64_t frameIndex, const Config&, float& x, float& y);
    // True = valid full-resolution outputs; outputs().srEvaluated distinguishes
    // real NGX evaluation from a spatial recovery when evaluation fails.
    // Uses the supplied motion field unchanged for SR (no camera reconstruction).
    bool resolve(const Inputs&);
    Outputs outputs() const; // borrowed until reconfigure/reset/destruction
    // Compose SR RGB, or NR RGB after caller evaluates into outputs().nrOutput.
    // Destination alpha is untouched. Caller draws HUD AFTER this operation.
    // Optional local sharpening is applied after SR/NR, before the HUD. It does
    // not alter vendor inputs/history or destination alpha.
    bool composeRgb(ID3D11RenderTargetView*, bool useNrOutput, float sharpness = 0.0f);
    // Main calls after SR-success/NR-failure (after any recovery composition).
    // Resets the next SR evaluation AND exposes historyReset=true for next NR.
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
// Exact nearest depth values are preserved, but nearest reconstructed guides
// are not new high-resolution geometry/disocclusion measurements. Do not claim
// per-object coverage beyond the motion field supplied by the renderer.
} // namespace gsr::sr_combined
