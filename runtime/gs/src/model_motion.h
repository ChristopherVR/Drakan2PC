#pragma once
#include "nr_guides.h"
#include <gsr/scene_models.h>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <cstring>

namespace gsr::motion {
// Advance only when the temporal consumer accepts a complete world. A rejected
// evaluation must never become the previous pose for the next accepted frame.
class Models {
    std::shared_ptr<const SceneModels> previous_;

public:
    void reset() { previous_.reset(); }
    void commit(std::shared_ptr<const SceneModels> scene) { previous_ = std::move(scene); }
    struct Frame {
        std::vector<nr::guides::ModelMotionVertex> vertices;
        std::vector<nr::guides::ModelMotionTexture> textures;
        unsigned matched = 0, rejected = 0;
    };
    Frame prepare(const SceneModels& scene, bool reset) const {
        Frame out;
        prepare(scene, reset, out);
        return out;
    }
    // Same as above into a caller-owned frame whose vectors keep their capacity across frames.
    void prepare(const SceneModels& scene, bool reset, Frame& out) const {
        prepareFrom(previous_.get(), scene, reset, out, scratch_);
    }
    const std::shared_ptr<const SceneModels>& previous() const { return previous_; }
    // Working memory of prepareFrom; one per thread that prepares.
    struct Scratch {
        std::vector<std::pair<uint32_t, const SceneModelInstance*>> previous;
        std::vector<uint32_t> current, ambiguous;
        std::unordered_map<uint32_t, uint32_t> textures;
    };
    // prepare() against an explicit previous scene (the committed one at the time of the call), so it can run on
    // another thread while the owner does not commit.
    static void prepareFrom(const SceneModels* previous_, const SceneModels& scene, bool reset, Frame& out,
                            Scratch& scratch_) {
        out.vertices.clear();
        out.textures.clear();
        out.matched = out.rejected = 0;
        if (reset || !previous_ || previous_->level != scene.level)
            return;
        // Object -> previous instance, and the objects that occur more than once in either frame (ambiguous), as
        // sorted vectors: no per-frame node allocations.
        auto& previous = scratch_.previous;
        previous.clear();
        previous.reserve(previous_->instances.size());
        for (const auto& i : previous_->instances)
            previous.push_back({i.object, &i});
        std::stable_sort(previous.begin(), previous.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        auto& ambiguous = scratch_.ambiguous;
        ambiguous.clear();
        for (size_t n = 1; n < previous.size(); ++n)
            if (previous[n].first == previous[n - 1].first)
                ambiguous.push_back(previous[n].first);
        auto& current = scratch_.current;
        current.clear();
        current.reserve(scene.instances.size());
        for (const auto& i : scene.instances)
            current.push_back(i.object);
        std::sort(current.begin(), current.end());
        for (size_t n = 1; n < current.size(); ++n)
            if (current[n] == current[n - 1])
                ambiguous.push_back(current[n]);
        std::sort(ambiguous.begin(), ambiguous.end());
        for (const auto& i : scene.instances) {
            const auto p = std::lower_bound(previous.begin(), previous.end(), i.object,
                                            [](const auto& entry, uint32_t object) { return entry.first < object; });
            if (std::binary_search(ambiguous.begin(), ambiguous.end(), i.object) || p == previous.end() ||
                p->first != i.object || !i.generation || i.generation != p->second->generation ||
                i.mesh >= scene.meshes.size() || p->second->mesh >= previous_->meshes.size() ||
                !std::isfinite(i.opacity) || !std::isfinite(p->second->opacity) || i.opacity < .999f ||
                p->second->opacity < .999f) {
                ++out.rejected;
                continue;
            }
            const auto& now = scene.meshes[i.mesh];
            const auto& before = previous_->meshes[p->second->mesh];
            if (!now || !before || !now->topology || now->topology != before->topology ||
                now->nativeId != before->nativeId || now->triangles.size() != before->triangles.size()) {
                ++out.rejected;
                continue;
            }
            // Static models already have exact camera motion from depth.
            if (now->identity == before->identity &&
                std::memcmp(i.transform, p->second->transform, sizeof(i.transform)) == 0)
                continue;
            if (out.vertices.size() + now->triangles.size() * 3 > 1500000) {
                ++out.rejected;
                continue;
            }
            const auto start = out.vertices.size();
            bool finite = true;
            for (size_t n = 0; n < now->triangles.size(); ++n) {
                const auto& a = now->triangles[n];
                const auto& b = before->triangles[n];
                uint32_t alphaTexture = ~0u;
                if (a.material < scene.textures.size()) {
                    const auto& tex = scene.textures[a.material];
                    if (!tex) {
                        finite = false;
                        break;
                    }
                    if ((tex->flags & 0xf0000) == 0x80000)
                        continue; // blended pixels have no unique surface motion
                    if (tex->flags & 0xf000a)
                        alphaTexture = a.material;
                }
                for (unsigned v = 0; v < 3; ++v) {
                    nr::guides::ModelMotionVertex dst;
                    dst.alphaTexture = alphaTexture;
                    for (unsigned r = 0; r < 3; ++r) {
                        const auto* t = i.transform;
                        const auto* old = p->second->transform;
                        dst.current[r] = t[r * 4] * a.position[v][0] + t[r * 4 + 1] * a.position[v][1] +
                                         t[r * 4 + 2] * a.position[v][2] + t[r * 4 + 3];
                        dst.previous[r] = old[r * 4] * b.position[v][0] + old[r * 4 + 1] * b.position[v][1] +
                                          old[r * 4 + 2] * b.position[v][2] + old[r * 4 + 3];
                        finite &= std::isfinite(dst.current[r]) && std::isfinite(dst.previous[r]);
                    }
                    dst.uv[0] = a.uv[v][0];
                    dst.uv[1] = a.uv[v][1];
                    finite &= std::isfinite(dst.uv[0]) && std::isfinite(dst.uv[1]);
                    out.vertices.push_back(dst);
                }
            }
            if (!finite) {
                out.vertices.resize(start);
                ++out.rejected;
            } else if (out.vertices.size() > start)
                ++out.matched;
        }
        // Only alpha-tested moving surfaces consume textures. Passing every
        // resident material allocates/uploads unused GPU array layers, including
        // static scenery and opaque actors whose motion needs no texels.
        auto& textures = scratch_.textures;
        textures.clear();
        for (auto& vertex : out.vertices)
            if (vertex.alphaTexture != ~0u) {
                const auto source = vertex.alphaTexture;
                auto [slot, inserted] = textures.emplace(source, uint32_t(out.textures.size()));
                if (inserted) {
                    const auto& t = scene.textures[source];
                    out.textures.push_back({t->identity, t->pixels});
                }
                vertex.alphaTexture = slot->second;
            }
    }

private:
    mutable Scratch scratch_;
};
} // namespace gsr::motion
