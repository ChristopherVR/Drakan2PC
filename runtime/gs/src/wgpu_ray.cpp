// The WebGPU ray pass, see wgpu_ray.h. Scene packing follows RayWater::Impl::execute (ray_water.cpp): the same material
// attribute layout ([static world][frame triangles][native models]), instance groups and masks (1 opaque, 2 water
// coverage, 4 native water), instance IDs (material base | opacity << 19) and camera-relative coordinates.
#include "wgpu_ray.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>

namespace gsr {
extern const char kRayTraceWgsl[]; // ray_trace.wgsl, embedded by CMake
}

namespace gsr::ray {
namespace {

using surface::Attribute;
using surface::kTriangleAttributes;
using surface::Point;
using surface::Triangle;

constexpr uint32_t kTriangleBudget = 500000;
constexpr uint32_t kTextureBudget = 768;
constexpr uint32_t kTexels = surface::Texture::size * surface::Texture::size;
constexpr uint32_t kStorageBuffers = 6;

struct CameraConstants {
    float projection[16], inverse[16], right[4], up[4], forward[4], sizeTime[4], jitter[4], limits[4];
    uint32_t quality[4];
};
struct LightConstants {
    uint32_t scene[4];
    float ambient[4], lightVector[4][4], lightColor[4][4];
    uint32_t effects[4];
    float zmap[4];
};
static_assert(sizeof(CameraConstants) <= 256 && sizeof(LightConstants) <= 256);

float asFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

} // namespace

// A small fixed set of threads that runs a batch of jobs together with the calling thread.
class WgpuRay::Workers {
public:
    explicit Workers(unsigned threads) {
        for (unsigned i = 0; i < threads; ++i)
            threads_.emplace_back([this] { loop(); });
    }
    ~Workers() {
        {
            std::lock_guard lock(m_);
            stop_ = true;
        }
        wake_.notify_all();
        for (auto& t : threads_)
            t.join();
    }
    // Returns once every job has run.
    void run(std::vector<std::function<void()>>& jobs) {
        if (jobs.empty())
            return;
        {
            std::lock_guard lock(m_);
            jobs_ = &jobs;
            next_ = 0;
            pending_ = jobs.size();
            ++generation_;
        }
        wake_.notify_all();
        work(&jobs);
        std::unique_lock lock(m_);
        done_.wait(lock, [&] { return pending_ == 0 && active_ == 0; });
        jobs_ = nullptr;
    }

private:
    void work(std::vector<std::function<void()>>* jobs) {
        for (size_t i; (i = next_.fetch_add(1)) < jobs->size();) {
            (*jobs)[i]();
            std::lock_guard lock(m_);
            if (--pending_ == 0)
                done_.notify_all();
        }
    }
    void loop() {
        uint64_t seen = 0;
        std::unique_lock lock(m_);
        for (;;) {
            wake_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                return;
            seen = generation_;
            auto* jobs = jobs_;
            if (!jobs)
                continue; // woke after the batch finished
            ++active_;
            lock.unlock();
            work(jobs);
            lock.lock();
            if (--active_ == 0)
                done_.notify_all();
        }
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable wake_, done_;
    std::vector<std::function<void()>>* jobs_ = nullptr;
    std::atomic<size_t> next_{0};
    size_t pending_ = 0, active_ = 0;
    uint64_t generation_ = 0;
    bool stop_ = false;
};

WgpuRay::~WgpuRay() = default;

WgpuRay::WgpuRay(rhi::Device& device) : gpu_(device) {
    if (!gpu_.compute(kStorageBuffers, 64ull << 20)) {
        status_ = "Ray tracing needs WebGPU compute shaders with large storage buffers (not available on this adapter)";
        return;
    }
    rhi::ProgramDesc d;
    d.wgsl = kRayTraceWgsl;
    d.uniformSlots = 2;
    d.textureSlots = 4;
    d.unfilterableMask = 0b1111;
    d.storageBuffers = kStorageBuffers;
    d.readWriteMask = 1u << 5; // Sky
    d.storageTextures = 2;
    d.storageFormat = rhi::Format::RGBA8;
    std::string error;
    d.computeEntry = "skyMain";
    d.label = "ray sky";
    sky_ = gpu_.createProgram(d, &error);
    if (sky_) {
        d.computeEntry = "lowMain";
        d.label = "ray low resolution";
        shadow_ = gpu_.createProgram(d, &error);
    }
    if (shadow_) {
        d.computeEntry = "main";
        d.label = "ray trace";
        main_ = gpu_.createProgram(d, &error);
    }
    skyBuffer_ = gpu_.createBuffer(7 * 16, "ray sky");
    dummy_ = gpu_.createTexture({1, 1, 1, rhi::Format::RGBA8, rhi::Sampled | rhi::CopyDst, "ray dummy"});
    if (dummy_) {
        const uint32_t white = ~0u;
        gpu_.writeTexture(*dummy_, 0, 0, 0, 1, 1, &white);
    }
    dummyStorage_ = gpu_.createTexture({1, 1, 1, rhi::Format::RGBA8, rhi::Storage, "ray dummy storage"});
    if (!sky_ || !shadow_ || !main_ || !skyBuffer_ || !dummy_ || !dummyStorage_) {
        status_ = "Ray tracing shader failed: " + error;
        return;
    }
    ready_ = true;
    status_ = "WebGPU compute ray tracing ready";
}

void WgpuRay::shareStaticWorld(std::shared_ptr<const std::vector<Triangle>> triangles, uint64_t geometryVersion,
                               uint64_t attributeVersion) {
    staticPublished_ = std::move(triangles);
    publishedGeometry_ = geometryVersion;
    publishedAttributes_ = attributeVersion;
}

WgpuRay::Blas WgpuRay::makeBlas(std::span<const Point> vertices) {
    Blas b;
    Bvh bvh = buildTriangles(vertices);
    b.nodes = std::move(bvh.nodes);
    b.tris.reserve(bvh.order.size() * 3);
    for (uint32_t i : bvh.order)
        for (unsigned j = 0; j < 3; ++j) {
            const Point& p = vertices[size_t(i) * 3 + j];
            b.tris.push_back({{p.x, p.y, p.z, j == 0 ? asFloat(i) : 0.0f}});
        }
    if (!b.nodes.empty()) {
        b.bounds.grow(b.nodes[0].min);
        b.bounds.grow(b.nodes[0].max);
    }
    return b;
}

// Appends a BLAS to this frame's node / triangle arrays (after the static region), rebasing its indices.
void WgpuRay::append(const Blas& blas, uint32_t& root) {
    const auto nodeBase = static_cast<uint32_t>(staticNodes_.size() + nodes.size());
    const auto triBase = static_cast<uint32_t>((staticTris_.size() + tris.size()) / 3);
    root = nodeBase;
    for (Node n : blas.nodes) {
        n.first += n.count ? triBase : nodeBase;
        nodes.push_back(n);
    }
    tris.insert(tris.end(), blas.tris.begin(), blas.tris.end());
}

bool WgpuRay::ensure(rhi::BufferPtr& buffer, uint64_t bytes, const char* label) {
    bytes = std::max<uint64_t>(bytes, 256);
    if (buffer && buffer->size() >= bytes)
        return false;
    buffer = gpu_.createBuffer(bytes + bytes / 2, label);
    return true; // replaced: the caller re-uploads what it kept there
}

bool WgpuRay::render(const PassFrame& f, std::span<const Triangle> triangles,
                     std::span<const surface::Texture> textures, const SceneModels* models,
                     const surface::RayOptions& options) {
    if (!ready_)
        return false;
    if (f.camera.orthographic) {
        status_ = "Ray tracing bypassed for an orthographic camera";
        return false;
    }
    if (!f.color || !f.depth || !f.width || !f.height || !std::isfinite(f.seconds))
        return false;
    ++pass_;
    const auto started = std::chrono::steady_clock::now();
    auto lap = started;
    std::string timing;
    auto stage = [&](const char* name) {
        const auto now = std::chrono::steady_clock::now();
        timing +=
            std::string(" ") + name + "=" +
            std::to_string(int(std::chrono::duration<double, std::micro>(now - lap).count() / 100) / 10.0).substr(0, 5);
        lap = now;
    };
    const std::span<const Triangle> staticTriangles =
        staticPublished_ ? std::span<const Triangle>(*staticPublished_) : std::span<const Triangle>();
    const uint32_t nativeTextureBase = uint32_t(textures.size());
    const size_t textureCount = textures.size() + (models ? models->textures.size() : 0);
    size_t totalTriangles = triangles.size() + staticTriangles.size();
    if (models)
        for (const auto& m : models->meshes)
            totalTriangles += m->triangles.size();
    if (totalTriangles > kTriangleBudget || textureCount > kTextureBudget) {
        status_ = "Ray scene exceeds the geometry/material budget";
        return false;
    }
    const bool frameWater = std::any_of(triangles.begin(), triangles.end(), [](const Triangle& t) { return t.water; });
    const bool staticWater =
        std::any_of(staticTriangles.begin(), staticTriangles.end(), [](const Triangle& t) { return t.water; });
    if (!frameWater && !staticWater && !options.shadows) {
        status_ = "Ray tracing bypassed: no verified water world geometry";
        return false;
    }
    const Point origin{f.camera.position[0], f.camera.position[1], f.camera.position[2]};

    // ---- CPU work, as parallel jobs: the static world's BVHs and normals on a new level, its material attributes when
    // they change, this frame's three triangle groups and every native model mesh new this pass. Assembly into the
    // buffer layout (static, frame, models, top level) follows on this thread.
    if (!workers_)
        workers_ = std::make_unique<Workers>(std::clamp(std::thread::hardware_concurrency(), 2u, 8u) - 1);
    std::vector<std::function<void()>> jobs;
    bool staticChanged = false;
    if (staticTriangles.empty()) {
        if (staticGeometry_ != ~0ull || !staticNodes_.empty()) {
            staticNodes_.clear();
            staticTris_.clear();
            staticAttributeData_.clear();
            staticPacked_.reset();
            staticCounts_[0] = staticCounts_[1] = staticCounts_[2] = 0;
            staticTextured_ = 0;
            staticGeometry_ = staticAttributes_ = ~0ull;
            staticTextureCount_ = ~size_t(0);
        }
    } else if (staticGeometry_ != publishedGeometry_) {
        // A new level: its BVHs in coordinates relative to the camera there.
        staticBase_ = origin;
        for (unsigned g = 0; g < 3; ++g)
            jobs.push_back([this, g, staticTriangles, origin] {
                std::vector<Point> positions;
                for (const auto& tri : staticTriangles)
                    if (surface::group(tri) == g)
                        for (const auto& p : tri.p)
                            positions.push_back(sub(p, origin));
                staticCounts_[g] = uint32_t(positions.size() / 3);
                staticBlas_[g] = makeBlas(positions);
            });
        jobs.push_back([this, staticTriangles] {
            staticNormals_.clear();
            surface::smoothNormals(
                staticTriangles.size(),
                [&](size_t i, Point(&p)[3]) { std::copy(staticTriangles[i].p, staticTriangles[i].p + 3, p); },
                staticNormals_);
        });
        workers_->run(jobs);
        jobs.clear();
        staticNodes_.clear();
        staticTris_.clear();
        for (unsigned g = 0; g < 3; ++g) {
            const auto nodeBase = uint32_t(staticNodes_.size()), triBase = uint32_t(staticTris_.size() / 3);
            staticRoots_[g] = nodeBase;
            for (Node n : staticBlas_[g].nodes) {
                n.first += n.count ? triBase : nodeBase;
                staticNodes_.push_back(n);
            }
            staticTris_.insert(staticTris_.end(), staticBlas_[g].tris.begin(), staticBlas_[g].tris.end());
            staticBlas_[g].nodes.clear(); // kept in staticNodes_ / staticTris_; the bounds stay
            staticBlas_[g].tris.clear();
        }
        staticSlot_.assign(staticTriangles.size(), 0);
        uint32_t slot = 0;
        for (unsigned g = 0; g < 3; ++g)
            for (size_t i = 0; i < staticTriangles.size(); ++i)
                if (surface::group(staticTriangles[i]) == g)
                    staticSlot_[i] = slot++;
        staticPacked_.reset();
        staticGeometry_ = publishedGeometry_;
        staticAttributes_ = ~0ull;
        staticChanged = true;
    }
    if (!staticTriangles.empty() &&
        (staticAttributes_ != publishedAttributes_ || staticTextureCount_ != textureCount)) {
        // Material projections change a few layers at a time: repack only the triangles that differ from the last
        // packed snapshot (or whose texture index crosses the texture count), in chunks.
        const auto* previous =
            staticPacked_ && staticPacked_->size() == staticTriangles.size() ? staticPacked_.get() : nullptr;
        if (!previous) {
            staticAttributeData_.assign(staticTriangles.size() * kTriangleAttributes, Attribute{});
            staticTextured_ = 0;
        }
        constexpr size_t kChunk = 8192;
        const size_t chunks = (staticTriangles.size() + kChunk - 1) / kChunk;
        staticChunkTextured_.assign(chunks, 0);
        staticChunkDirty_.assign(chunks, {SIZE_MAX, 0});
        const size_t oldTextures = previous ? staticTextureCount_ : 0;
        for (size_t c = 0; c < chunks; ++c)
            jobs.push_back([this, c, previous, staticTriangles, textureCount, oldTextures] {
                std::vector<Attribute> packed;
                int textured = 0;
                auto& dirty = staticChunkDirty_[c];
                const size_t end = std::min(staticTriangles.size(), (c + 1) * kChunk);
                for (size_t i = c * kChunk; i < end; ++i) {
                    const Triangle& t = staticTriangles[i];
                    if (previous) {
                        const Triangle& old = (*previous)[i];
                        if (std::memcmp(&old, &t, sizeof(Triangle)) == 0 &&
                            (t.texture < oldTextures) == (t.texture < textureCount))
                            continue;
                        textured -= old.texture < oldTextures ? 1 : 0;
                    }
                    packed.clear();
                    textured += int(surface::packAttributes(t, textureCount, packed, &staticNormals_[i * 3]));
                    const size_t slot = staticSlot_[i];
                    std::copy(packed.begin(), packed.end(), staticAttributeData_.begin() + slot * kTriangleAttributes);
                    dirty.first = std::min(dirty.first, slot);
                    dirty.second = std::max(dirty.second, slot + 1);
                }
                staticChunkTextured_[c] = textured;
            });
        staticRepack_ = true;
        staticAttributes_ = publishedAttributes_;
        staticTextureCount_ = textureCount;
    }
    // This frame's reconstructed triangles (camera relative), one job per group.
    // Each group in chunks of kFrameChunk triangles (draw order), one BLAS and instance per chunk, built in parallel.
    constexpr size_t kFrameChunk = 8192;
    uint32_t counts[3]{};
    std::vector<uint32_t> groupOf[3];
    for (uint32_t i = 0; i < triangles.size(); ++i)
        groupOf[surface::group(triangles[i])].push_back(i);
    struct FrameChunk {
        unsigned group;
        size_t first, count;
        Blas blas;
        std::vector<Attribute> attributes;
        uint32_t textured = 0;
    };
    std::vector<FrameChunk> frameChunks;
    for (unsigned g = 0; g < 3; ++g) {
        counts[g] = uint32_t(groupOf[g].size());
        for (size_t first = 0; first < groupOf[g].size(); first += kFrameChunk)
            frameChunks.push_back({g, first, std::min(kFrameChunk, groupOf[g].size() - first), {}, {}, 0});
    }
    for (auto& chunk : frameChunks)
        jobs.push_back([&chunk, &groupOf, triangles, origin, textureCount] {
            std::vector<Point> positions;
            positions.reserve(chunk.count * 3);
            chunk.attributes.reserve(chunk.count * kTriangleAttributes);
            for (size_t k = chunk.first; k < chunk.first + chunk.count; ++k) {
                const Triangle& tri = triangles[groupOf[chunk.group][k]];
                for (const auto& p : tri.p)
                    positions.push_back(sub(p, origin));
                chunk.textured += surface::packAttributes(tri, textureCount, chunk.attributes);
            }
            chunk.blas = makeBlas(positions);
        });
    // Native model meshes new this pass (a new pose is a new mesh). Welds are shared by topology: made here first.
    std::vector<Mesh*> meshList;
    if (models) {
        meshList.reserve(models->meshes.size());
        for (const auto& model : models->meshes) {
            Mesh& mesh = meshes_[model->identity];
            meshList.push_back(&mesh);
            if (mesh.source == model && mesh.textureBase == nativeTextureBase)
                continue;
            const auto corners = [model](size_t i, Point(&p)[3]) {
                for (unsigned j = 0; j < 3; ++j) {
                    const auto& v = model->triangles[i].position[j];
                    p[j] = {v[0], v[1], v[2]};
                }
            };
            const surface::CornerWelds* welds = nullptr;
            if (model->topology) {
                auto& shared = welds_[model->topology];
                if (shared.corners.size() != model->triangles.size() * 3)
                    surface::weldCorners(model->triangles.size(), corners, shared);
                welds = &shared;
            }
            jobs.push_back([&mesh, model, models, nativeTextureBase, welds, corners] {
                surface::CornerWelds unshared;
                if (!welds)
                    surface::weldCorners(model->triangles.size(), corners, unshared);
                std::vector<Point> normals;
                surface::smoothNormals(model->triangles.size(), corners, welds ? *welds : unshared, normals);
                mesh.attributes.clear();
                mesh.textured =
                    surface::packModelAttributes(*model, *models, nativeTextureBase, normals, mesh.attributes);
                if (mesh.source != model) {
                    std::vector<Point> positions;
                    positions.reserve(model->triangles.size() * 3);
                    for (const auto& t : model->triangles)
                        for (const auto& p : t.position)
                            positions.push_back({p[0], p[1], p[2]});
                    mesh.blas = makeBlas(positions);
                }
                mesh.source = model;
                mesh.textureBase = nativeTextureBase;
            });
        }
    }
    workers_->run(jobs);
    if (staticRepack_) {
        staticRepack_ = false;
        for (size_t c = 0; c < staticChunkDirty_.size(); ++c) {
            staticTextured_ = uint32_t(int(staticTextured_) + staticChunkTextured_[c]);
            if (staticChunkDirty_[c].second) {
                staticDirtyFirst_ = std::min(staticDirtyFirst_, staticChunkDirty_[c].first);
                staticDirtyEnd_ = std::max(staticDirtyEnd_, staticChunkDirty_[c].second);
            }
        }
        staticPacked_ = staticPublished_;
    }
    stage("build");

    // ---- assembly
    const uint32_t staticTotal = staticCounts_[0] + staticCounts_[1] + staticCounts_[2];
    nodes.clear();
    tris.clear();
    attributes.clear();
    instanceData.clear();
    std::vector<Box> boxes;
    uint32_t textured = staticTextured_;
    for (const auto& chunk : frameChunks) { // group order, then draw order: the material slots of the instances below
        attributes.insert(attributes.end(), chunk.attributes.begin(), chunk.attributes.end());
        textured += chunk.textured;
    }
    auto addInstance = [&](const float(&objectToWorld)[12], uint32_t root, uint32_t id, uint32_t mask,
                           const Box& local) {
        // world -> object: the inverse of the affine object -> world (camera relative).
        const float* m = objectToWorld;
        const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], g = m[6], h = m[8], i = m[9], k = m[10];
        const double det = a * (e * k - g * i) - b * (d * k - g * h) + c * (d * i - e * h);
        if (!(std::abs(det) > 1e-20) || !std::isfinite(det))
            return;
        const double inv[3][3] = {{(e * k - g * i) / det, (c * i - b * k) / det, (b * g - c * e) / det},
                                  {(g * h - d * k) / det, (a * k - c * h) / det, (c * d - a * g) / det},
                                  {(d * i - e * h) / det, (b * h - a * i) / det, (a * e - b * d) / det}};
        Instance inst{};
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col)
                inst.r[r][col] = float(inv[r][col]);
            inst.r[r][3] = float(-(inv[r][0] * m[3] + inv[r][1] * m[7] + inv[r][2] * m[11]));
        }
        inst.info[0] = root;
        inst.info[1] = id;
        inst.info[2] = mask;
        // World bounds: the eight corners of the local box.
        Box world;
        for (int corner = 0; corner < 8; ++corner) {
            const float p[3] = {corner & 1 ? local.max[0] : local.min[0], corner & 2 ? local.max[1] : local.min[1],
                                corner & 4 ? local.max[2] : local.min[2]};
            const float w[3] = {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3],
                                m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
                                m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
            world.grow(w);
        }
        instanceData.push_back(inst);
        boxes.push_back(world);
    };
    {
        // Static world first (translated from its base to the camera), then this frame's triangles.
        const Point t = sub(staticBase_, origin);
        const float translate[12] = {1, 0, 0, t.x, 0, 1, 0, t.y, 0, 0, 1, t.z};
        const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        uint32_t materialBase = 0;
        for (unsigned g = 0; g < 3; ++g) {
            if (staticCounts_[g])
                addInstance(translate, staticRoots_[g], (31u << 19) | materialBase, 1u << g, staticBlas_[g].bounds);
            materialBase += staticCounts_[g];
        }
        for (const auto& chunk : frameChunks) {
            uint32_t root = 0;
            append(chunk.blas, root);
            addInstance(identity, root, (31u << 19) | (materialBase + uint32_t(chunk.first)), 1u << chunk.group,
                        chunk.blas.bounds);
            if (chunk.first + chunk.count == counts[chunk.group])
                materialBase += counts[chunk.group];
        }
    }
    if (models) {
        std::vector<uint32_t> modelOffsets, modelRoots;
        modelOffsets.reserve(meshList.size());
        modelRoots.reserve(meshList.size());
        for (Mesh* mesh : meshList) {
            modelOffsets.push_back(staticTotal + uint32_t(attributes.size() / kTriangleAttributes));
            mesh->used = pass_;
            attributes.insert(attributes.end(), mesh->attributes.begin(), mesh->attributes.end());
            textured += mesh->textured;
            uint32_t root = 0;
            if (!mesh->blas.nodes.empty())
                append(mesh->blas, root);
            modelRoots.push_back(root);
        }
        for (const auto& source : models->instances) {
            if (source.mesh >= models->meshes.size() || !std::isfinite(source.opacity))
                continue;
            const auto& model = models->meshes[source.mesh];
            const Mesh& mesh = meshes_[model->identity];
            if (model->triangles.empty() || mesh.blas.nodes.empty())
                continue;
            const auto opacity = uint32_t(std::clamp(source.opacity, 0.f, 1.f) * 31 + .5f);
            if (!opacity || surface::cameraShell(*model, source.transform, origin))
                continue;
            float m[12];
            std::copy(source.transform, source.transform + 12, m);
            m[3] -= origin.x;
            m[7] -= origin.y;
            m[11] -= origin.z;
            addInstance(m, modelRoots[source.mesh], modelOffsets[source.mesh] | (opacity << 19), 1, mesh.blas.bounds);
        }
        std::erase_if(meshes_, [&](const auto& entry) { return entry.second.used + 120 < pass_; });
    }
    stage("models");
    if (instanceData.empty()) {
        status_ = "Ray tracing bypassed: an empty scene";
        return false;
    }
    // ---- top level over the instances (reordered into leaf order)
    uint32_t topRoot = 0;
    {
        Bvh top = build(boxes, 2);
        std::vector<Instance> ordered(instanceData.size());
        for (size_t i = 0; i < top.order.size(); ++i)
            ordered[i] = instanceData[top.order[i]];
        instanceData.swap(ordered);
        topRoot = static_cast<uint32_t>(staticNodes_.size() + nodes.size());
        for (Node n : top.nodes) {
            if (!n.count)
                n.first += topRoot;
            nodes.push_back(n);
        }
    }

    stage("top");
    // ---- uploads: static regions once (or after a buffer was replaced), this frame's regions every pass
    const uint64_t nodeBytes = (staticNodes_.size() + nodes.size()) * sizeof(Node);
    const uint64_t triBytes = (staticTris_.size() + tris.size()) * sizeof(Gpu4);
    const uint64_t attributeBytes = (staticAttributeData_.size() + attributes.size()) * sizeof(Attribute);
    const bool nodesReplaced = ensure(nodes_, nodeBytes, "ray nodes");
    const bool trisReplaced = ensure(tris_, triBytes, "ray triangles");
    const bool materialsReplaced = ensure(materials_, attributeBytes, "ray materials");
    ensure(instances_, instanceData.size() * sizeof(Instance), "ray instances");
    if (!nodes_ || !tris_ || !materials_ || !instances_) {
        status_ = "Ray tracing buffer allocation failed";
        return false;
    }
    if (staticChanged || nodesReplaced)
        gpu_.writeBuffer(*nodes_, 0, staticNodes_.data(), staticNodes_.size() * sizeof(Node));
    if (staticChanged || trisReplaced)
        gpu_.writeBuffer(*tris_, 0, staticTris_.data(), staticTris_.size() * sizeof(Gpu4));
    if (materialsReplaced) {
        staticDirtyFirst_ = 0;
        staticDirtyEnd_ = staticAttributeData_.size() / kTriangleAttributes;
    }
    if (staticDirtyEnd_ > staticDirtyFirst_) {
        gpu_.writeBuffer(*materials_, staticDirtyFirst_ * kTriangleAttributes * sizeof(Attribute),
                         staticAttributeData_.data() + staticDirtyFirst_ * kTriangleAttributes,
                         (staticDirtyEnd_ - staticDirtyFirst_) * kTriangleAttributes * sizeof(Attribute));
        staticDirtyFirst_ = SIZE_MAX;
        staticDirtyEnd_ = 0;
    }
    gpu_.writeBuffer(*nodes_, staticNodes_.size() * sizeof(Node), nodes.data(), nodes.size() * sizeof(Node));
    gpu_.writeBuffer(*tris_, staticTris_.size() * sizeof(Gpu4), tris.data(), tris.size() * sizeof(Gpu4));
    gpu_.writeBuffer(*materials_, staticAttributeData_.size() * sizeof(Attribute), attributes.data(),
                     attributes.size() * sizeof(Attribute));
    gpu_.writeBuffer(*instances_, 0, instanceData.data(), instanceData.size() * sizeof(Instance));
    // Texels: only the slots whose content changed (all of them after the buffer grew).
    {
        const auto needed = uint32_t(std::max<size_t>(textureCount, 1));
        if (!texels_ || texelCapacity_ < needed) {
            texelCapacity_ = std::min<uint32_t>(kTextureBudget, (needed + 63) & ~63u);
            texels_ = gpu_.createBuffer(uint64_t(texelCapacity_) * kTexels * 4, "ray texels");
            textureFingerprints_.clear();
            if (!texels_) {
                status_ = "Ray tracing texture allocation failed";
                return false;
            }
        }
        textureFingerprints_.resize(textureCount);
        for (size_t i = 0; i < textureCount; ++i) {
            uint64_t identity;
            std::span<const uint32_t> pixels;
            if (i < textures.size()) {
                identity = textures[i].identity;
                pixels = textures[i].pixels;
            } else {
                const auto& t = *models->textures[i - textures.size()];
                identity = t.identity;
                pixels = t.pixels;
            }
            if (pixels.size() != kTexels)
                continue;
            uint64_t fingerprint = identity;
            if (!fingerprint) {
                fingerprint = 14695981039346656037ull;
                for (uint32_t p : pixels) {
                    fingerprint ^= p;
                    fingerprint *= 1099511628211ull;
                }
            }
            if (textureFingerprints_[i] == fingerprint)
                continue;
            textureFingerprints_[i] = fingerprint;
            gpu_.writeBuffer(*texels_, uint64_t(i) * kTexels * 4, pixels.data(), uint64_t(kTexels) * 4);
        }
    }
    if (!output_ || output_->width() != f.width || output_->height() != f.height)
        output_ = gpu_.createTexture(
            {f.width, f.height, 1, rhi::Format::RGBA8, rhi::Storage | rhi::CopySrc | rhi::Sampled, "ray output"});

    stage("upload");
    // ---- constants
    CameraConstants cam{};
    std::copy(f.camera.viewToClip, f.camera.viewToClip + 16, cam.projection);
    std::copy(f.camera.clipToView, f.camera.clipToView + 16, cam.inverse);
    std::copy(f.camera.right, f.camera.right + 3, cam.right);
    std::copy(f.camera.up, f.camera.up + 3, cam.up);
    std::copy(f.camera.forward, f.camera.forward + 3, cam.forward);
    cam.sizeTime[0] = float(f.width);
    cam.sizeTime[1] = float(f.height);
    cam.sizeTime[2] = f.seconds;
    cam.sizeTime[3] = f.reflections ? 1.f : 0.f;
    cam.jitter[0] = f.jitter[0];
    cam.jitter[1] = f.jitter[1];
    cam.limits[0] = f.camera.nearPlane;
    cam.limits[1] = float(counts[0] + staticCounts_[0]);
    cam.limits[2] = origin.x;
    cam.limits[3] = origin.z;
    cam.quality[0] = std::clamp(f.sunSamples, 1u, 4u);
    cam.quality[1] = std::clamp(f.occlusionRays, 1u, 8u);
    cam.quality[2] = std::clamp(f.reflectionRays, 1u, 4u);
    cam.quality[3] = std::max(1u, f.shadowScale);
    const uint32_t lowW = (f.width + cam.quality[3] - 1) / cam.quality[3];
    const uint32_t lowH = (f.height + cam.quality[3] - 1) / cam.quality[3];
    LightConstants lit{};
    const uint32_t water = counts[2] + staticCounts_[2], coverage = counts[1] + staticCounts_[1];
    lit.scene[0] = uint32_t(textureCount);
    lit.scene[1] = surface::Texture::size;
    lit.scene[2] = f.coverageDebug;
    lit.scene[3] = topRoot;
    lit.effects[0] = options.water && (water || coverage);
    lit.effects[1] = options.shadows;
    lit.effects[3] = (water ? 1u : 0u) | (coverage ? 2u : 0u) | (options.requireWaterCoverage ? 4u : 0u);
    const uint64_t level = models ? models->level : 0;
    if (skyLevel_ != level) {
        lit.effects[3] |= 8;
        skyLevel_ = level;
    }
    if (models) {
        for (unsigned j = 0; j < 3; ++j)
            lit.ambient[j] =
                std::isfinite(models->lighting.ambient[j]) ? std::clamp(models->lighting.ambient[j], 0.f, 16.f) : 0.f;
        for (const auto& light : models->lighting.lights) {
            if (lit.effects[2] >= 4)
                break;
            bool valid = light.type <= SceneLight::Point;
            float lengthSquared = 0;
            for (unsigned j = 0; j < 3; ++j) {
                valid &= std::isfinite(light.vector[j]) && std::isfinite(light.color[j]) && light.color[j] >= 0 &&
                         light.color[j] <= 16;
                lengthSquared += light.vector[j] * light.vector[j];
            }
            if (light.type == SceneLight::Directional)
                valid &= std::isfinite(lengthSquared) && lengthSquared > 1e-12f;
            else
                valid &= std::isfinite(light.radiusSquared) && light.radiusSquared > 0;
            if (!valid)
                continue;
            const unsigned i = lit.effects[2]++;
            std::copy(light.vector, light.vector + 3, lit.lightVector[i]);
            std::copy(light.color, light.color + 3, lit.lightColor[i]);
            lit.lightVector[i][3] = light.type ? 1.f : 0.f;
            lit.lightColor[i][3] = light.radiusSquared;
            if (light.type) {
                lit.lightVector[i][0] -= origin.x;
                lit.lightVector[i][1] -= origin.y;
                lit.lightVector[i][2] -= origin.z;
            }
        }
    }
    lit.zmap[0] = f.zFar;
    lit.zmap[1] = f.zInvRange;
    lit.zmap[2] = 1e-7f; // sky epsilon (nr::guides::Frame::skyEpsilon)
    lit.zmap[3] = asFloat((f.x & 0xffffu) | (f.y << 16));

    // ---- dispatch: sky reduction, then the per-pixel trace
    // The low-resolution pass (shadows, water mask) runs when either effect is live this frame.
    const bool lowPass = (options.shadows && lit.effects[2]) || lit.effects[0];
    if (lowPass && (!shadowTex_ || shadowTex_->width() != lowW || shadowTex_->height() != lowH)) {
        shadowTex_ = gpu_.createTexture({lowW, lowH, 1, rhi::Format::RGBA8, rhi::Storage | rhi::Sampled, "ray low"});
        reflectTex_ =
            gpu_.createTexture({lowW, lowH, 1, rhi::Format::RGBA8, rhi::Storage | rhi::Sampled, "ray low reflection"});
    }
    gpu_.setUniforms(0, &cam, sizeof cam);
    gpu_.setUniforms(1, &lit, sizeof lit);
    gpu_.setTexture(0, f.color);
    gpu_.setTexture(1, f.depth);
    gpu_.setBuffer(0, nodes_.get());
    gpu_.setBuffer(1, tris_.get());
    gpu_.setBuffer(2, instances_.get());
    gpu_.setBuffer(3, materials_.get());
    gpu_.setBuffer(4, texels_.get());
    gpu_.setBuffer(5, skyBuffer_.get());
    gpu_.setTexture(2, dummy_.get());
    gpu_.setTexture(3, dummy_.get());
    gpu_.setStorageTexture(0, output_.get());
    gpu_.setStorageTexture(1, dummyStorage_.get());
    gpu_.setProgram(sky_.get());
    gpu_.dispatch(1, 1);
    if (lowPass && shadowTex_ && reflectTex_) {
        gpu_.setStorageTexture(0, shadowTex_.get());
        gpu_.setStorageTexture(1, reflectTex_.get());
        gpu_.setProgram(shadow_.get());
        gpu_.dispatch((lowW + 7) / 8, (lowH + 7) / 8);
        gpu_.setStorageTexture(0, output_.get());
        gpu_.setStorageTexture(1, dummyStorage_.get());
        gpu_.setTexture(2, shadowTex_.get());
        gpu_.setTexture(3, reflectTex_.get());
    }
    gpu_.setProgram(main_.get());
    gpu_.dispatch((f.width + 7) / 8, (f.height + 7) / 8);
    gpu_.setTexture(2, nullptr);
    gpu_.setTexture(3, nullptr);
    gpu_.setProgram(nullptr);

    status_ =
        "Ray scene traced: " + std::to_string(counts[0] + staticCounts_[0]) + " opaque (" +
        std::to_string(staticCounts_[0]) + " resident) / " + std::to_string(water) + " native water / " +
        std::to_string(coverage) + " water coverage triangles; " + std::to_string(textured) + " textured / " +
        std::to_string(textureCount) + " material textures; " + std::to_string(models ? models->instances.size() : 0) +
        " model instances; shadows=" +
        (options.shadows
             ? (lit.effects[2] ? std::to_string(lit.effects[2]) + " native lights" : "waiting for native lights")
             : "off") +
        "; CPU" + timing + " total " +
        std::to_string(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()) +
        " ms";
    return true;
}

} // namespace gsr::ray
