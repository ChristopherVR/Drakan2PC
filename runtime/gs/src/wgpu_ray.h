// The WebGPU renderer's ray pass: modern water, reflections, soft shadows and ambient occlusion traced in WGSL compute
// (ray_trace.wgsl) over CPU-built BVHs (ray_bvh.h). The scene and its packing are the D3D11 DXR pass's (ray_scene.h,
// ray_water.cpp); this runs synchronously on the renderer thread, between the world draws and the HUD.
#pragma once
#include "guide_camera.h"
#include "ray_bvh.h"
#include "ray_normals.h"
#include "ray_scene.h"
#include "rhi.h"

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gsr::ray {

struct PassFrame {
    rhi::Texture* color = nullptr; // the GS target (RGBA8) and its GS depth (R32F)
    rhi::Texture* depth = nullptr;
    uint32_t x = 0, y = 0, width = 0, height = 0; // the frame's region in both, in render pixels
    float zFar = 0, zInvRange = 0;                // GS depth -> [0, 1] over [far, near]
    guide::FrameCamera camera;
    float jitter[2]{}; // the world raster's sub-pixel jitter (render pixels)
    float seconds = 0;
    bool reflections = true;
    uint32_t coverageDebug = 0;
    // Rays per pixel: sun samples (1-4), occlusion rays, reflection rays (1-4); and render pixels per shadow pixel.
    uint32_t sunSamples = 4, occlusionRays = 4, reflectionRays = 4, shadowScale = 1;
};

class WgpuRay {
public:
    explicit WgpuRay(rhi::Device& device);
    ~WgpuRay();
    bool ready() const { return ready_; }
    std::string status() const { return status_; }
    void shareStaticWorld(std::shared_ptr<const std::vector<surface::Triangle>> triangles, uint64_t geometryVersion,
                          uint64_t attributeVersion);
    // Traces the frame into output() (width x height at 0, 0). False (status() says why) when the pass was bypassed.
    bool render(const PassFrame& frame, std::span<const surface::Triangle> triangles,
                std::span<const surface::Texture> textures, const SceneModels* models,
                const surface::RayOptions& options);
    rhi::Texture* output() const { return output_.get(); }

private:
    struct Gpu4 {
        float v[4];
    };
    struct Blas {
        std::vector<Node> nodes;
        std::vector<Gpu4> tris; // three per triangle in leaf order, first.w = local primitive (bits)
        Box bounds;
    };
    struct Instance {
        float r[3][4];    // world -> object rows
        uint32_t info[4]; // root node, instance id (material base | opacity << 19), mask, 0
    };
    struct Mesh {
        std::shared_ptr<const SceneModelMesh> source;
        Blas blas;
        std::vector<surface::Attribute> attributes;
        uint32_t textured = 0;
        uint32_t textureBase = ~0u;
        uint64_t used = 0;
    };
    class Workers;
    static Blas makeBlas(std::span<const surface::Point> vertices);
    void append(const Blas& blas, uint32_t& root);
    bool ensure(rhi::BufferPtr& buffer, uint64_t bytes, const char* label);

    rhi::Device& gpu_;
    std::unique_ptr<Workers> workers_;
    bool ready_ = false;
    std::string status_ = "Ray tracing not started";
    rhi::ProgramPtr sky_, shadow_, main_;
    rhi::BufferPtr nodes_, tris_, instances_, materials_, texels_, skyBuffer_;
    rhi::TexturePtr output_, shadowTex_, reflectTex_, dummy_, dummyStorage_;
    // Static (resident) world: three BLAS (by group) in base-relative coordinates, uploaded at the start of the node /
    // triangle buffers and of the material attributes.
    std::shared_ptr<const std::vector<surface::Triangle>> staticPublished_;
    uint64_t publishedGeometry_ = 0, publishedAttributes_ = 0;
    uint64_t staticGeometry_ = ~0ull, staticAttributes_ = ~0ull;
    size_t staticTextureCount_ = ~size_t(0);
    surface::Point staticBase_{};
    uint32_t staticCounts_[3]{}, staticTextured_ = 0;
    Blas staticBlas_[3];
    std::vector<Node> staticNodes_;
    std::vector<Gpu4> staticTris_;
    uint32_t staticRoots_[3]{};
    std::vector<surface::Attribute> staticAttributeData_;
    std::vector<surface::Point> staticNormals_; // welded vertex normals of the static world (per geometry version)
    // The static triangles last packed, each triangle's attribute slot (group order) and the slots to upload.
    std::shared_ptr<const std::vector<surface::Triangle>> staticPacked_;
    std::vector<uint32_t> staticSlot_;
    size_t staticDirtyFirst_ = SIZE_MAX, staticDirtyEnd_ = 0; // in slots
    std::vector<int> staticChunkTextured_;
    std::vector<std::pair<size_t, size_t>> staticChunkDirty_;
    bool staticRepack_ = false;
    // Per frame
    std::vector<Node> nodes;
    std::vector<Gpu4> tris;
    std::vector<surface::Attribute> attributes;
    std::vector<Instance> instanceData;
    std::unordered_map<uint64_t, Mesh> meshes_;
    std::unordered_map<uint64_t, surface::CornerWelds> welds_;
    uint64_t pass_ = 0;
    std::vector<uint64_t> textureFingerprints_;
    uint32_t texelCapacity_ = 0; // textures the texel buffer holds
    uint64_t skyLevel_ = ~0ull;
};

} // namespace gsr::ray
