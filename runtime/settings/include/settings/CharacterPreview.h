// Original character geometry projected each frame and rasterized by the overlay GPU at output resolution.
// No sprite sheets, captures, platform graphics headers, or game world are needed by the title menu.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gameui/Draw.h"
#include "imgui.h"

namespace dsettings {
class CharacterPreview {
public:
    using Upload = std::function<ImTextureID(const void*, uint32_t, uint32_t)>;
    using Release = std::function<void(ImTextureID)>;
    static std::shared_ptr<CharacterPreview> load(const std::string& path);
    static std::shared_ptr<CharacterPreview> decode(const std::vector<uint8_t>& bytes);
    bool upload(const Upload& upload, const Release& release);
    void release(const Release& release);
    bool ready() const { return !gpu_.empty(); }
    void draw(ImDrawList* list, const gameui::Rect& rect, double time, float fade) const;
    size_t triangleCount() const { return triangles_.size(); }

private:
    struct Vec3 {
        float x, y, z;
    };
    struct Triangle {
        uint32_t index[3], texture;
        float uv[6];
    };
    struct Texture {
        uint32_t w, h;
        std::vector<uint8_t> rgba;
    };
    std::vector<Vec3> vertices_, normals_;
    std::vector<Triangle> triangles_;
    std::vector<Texture> textures_;
    std::vector<ImTextureID> gpu_;
    Vec3 center_{};
    float height_ = 1, radius_ = 1;
};
} // namespace dsettings
