#include "settings/CharacterPreview.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace dsettings {
namespace {
constexpr size_t kMaxBytes = 64 * 1024 * 1024;
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}
    uint32_t u32() {
        if (at_ + 4 > bytes_.size())
            throw std::runtime_error("truncated model");
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= uint32_t(bytes_[at_++]) << (8 * i);
        return value;
    }
    float f32() {
        const float value = std::bit_cast<float>(u32());
        if (!std::isfinite(value) || std::abs(value) > 1e8f)
            throw std::runtime_error("invalid coordinate");
        return value;
    }
    std::vector<uint8_t> pixels(size_t count) {
        if (count > kMaxBytes || count > bytes_.size() - at_)
            throw std::runtime_error("invalid texture");
        std::vector<uint8_t> result(bytes_.begin() + at_, bytes_.begin() + at_ + count);
        at_ += count;
        return result;
    }
    bool end() const { return at_ == bytes_.size(); }

private:
    const std::vector<uint8_t>& bytes_;
    size_t at_ = 0;
};
} // namespace

std::shared_ptr<CharacterPreview> CharacterPreview::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto length = file.tellg();
    if (length <= 0 || length > std::streamoff(kMaxBytes))
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return {};
    return decode(bytes);
}

std::shared_ptr<CharacterPreview> CharacterPreview::decode(const std::vector<uint8_t>& bytes) {
    if (bytes.size() > kMaxBytes)
        return {};
    try {
        Reader reader(bytes);
        if (reader.u32() != 0x4d504344u || reader.u32() != 1) // DCPM, version 1
            return {};
        const uint32_t nv = reader.u32(), nt = reader.u32(), nm = reader.u32();
        if (!nv || nv > 100000 || !nt || nt > 200000 || !nm || nm > 64)
            return {};
        auto result = std::make_shared<CharacterPreview>();
        result->vertices_.reserve(nv);
        result->normals_.resize(nv);
        for (uint32_t i = 0; i < nv; ++i)
            result->vertices_.push_back({reader.f32(), reader.f32(), reader.f32()});
        Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
        for (uint32_t i = 0; i < nt; ++i) {
            Triangle triangle{};
            for (uint32_t& index : triangle.index) {
                index = reader.u32();
                if (index >= nv)
                    return {};
                const Vec3 p = result->vertices_[index];
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
            triangle.texture = reader.u32();
            if (triangle.texture >= nm)
                return {};
            for (float& uv : triangle.uv)
                uv = reader.f32();
            result->triangles_.push_back(triangle);
            const auto a = result->vertices_[triangle.index[0]], b = result->vertices_[triangle.index[1]],
                       c = result->vertices_[triangle.index[2]];
            const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
            const Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
            for (uint32_t index : triangle.index) {
                auto& normal = result->normals_[index];
                normal.x += n.x;
                normal.y += n.y;
                normal.z += n.z;
            }
        }
        result->center_ = {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
        result->height_ = std::max(1e-6f, high.y - low.y);
        result->radius_ = 1e-6f;
        for (const auto& triangle : result->triangles_)
            for (uint32_t index : triangle.index) {
                const Vec3 p = result->vertices_[index];
                result->radius_ =
                    std::max(result->radius_, std::hypot(p.x - result->center_.x, p.z - result->center_.z));
            }
        for (auto& n : result->normals_) {
            const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (length > 1e-12f) {
                n.x /= length;
                n.y /= length;
                n.z /= length;
            }
        }
        for (uint32_t i = 0; i < nm; ++i) {
            Texture texture{reader.u32(), reader.u32(), {}};
            if (!texture.w || !texture.h || texture.w > 2048 || texture.h > 2048)
                return {};
            texture.rgba = reader.pixels(size_t(texture.w) * texture.h * 4);
            result->textures_.push_back(std::move(texture));
        }
        return reader.end() ? result : nullptr;
    } catch (const std::exception&) {
        return {};
    }
}

bool CharacterPreview::upload(const Upload& uploadTexture, const Release& releaseTexture) {
    if (ready())
        return true;
    for (const auto& texture : textures_) {
        const ImTextureID id = uploadTexture(texture.rgba.data(), texture.w, texture.h);
        if (!id) {
            release(releaseTexture);
            return false;
        }
        gpu_.push_back(id);
    }
    return ready();
}

void CharacterPreview::release(const Release& releaseTexture) {
    for (ImTextureID texture : gpu_)
        releaseTexture(texture);
    gpu_.clear();
}

void CharacterPreview::draw(ImDrawList* list, const gameui::Rect& rect, double time, float fade) const {
    if (!ready() || !list || rect.w < 2 || rect.h < 2)
        return;
    constexpr float kPi = 3.141592654f, kElevation = 0.10f;
    const float angle = kPi + 0.3f + static_cast<float>(std::fmod(std::max(0.0, time) * 0.35, 2 * kPi));
    const float sn = std::sin(angle), cs = std::cos(angle), se = std::sin(kElevation), ce = std::cos(kElevation);
    const float scale = std::min((rect.w - 12) / (radius_ * 2), (rect.h - 12) / (height_ * ce + radius_ * 2 * se));
    struct Projected {
        ImVec2 point;
        float depth;
        ImU32 color;
    };
    std::vector<Projected> projected;
    projected.reserve(vertices_.size());
    const int alpha = static_cast<int>(std::clamp(fade, 0.0f, 1.0f) * 255);
    for (size_t i = 0; i < vertices_.size(); ++i) {
        const auto p = vertices_[i], n = normals_[i];
        const float x = p.x - center_.x, y = p.y - center_.y, z = p.z - center_.z;
        const float zr = -x * sn + z * cs;
        const float light =
            std::max(0.0f, (n.x * cs + n.z * sn) * -0.35f + n.y * 0.55f + (-n.x * sn + n.z * cs) * 0.75f);
        const int shade = static_cast<int>(std::clamp(0.78f + 0.22f * light, 0.0f, 1.0f) * 255);
        projected.push_back({{rect.cx() + (x * cs + z * sn) * scale, rect.cy() - (y * ce - zr * se) * scale},
                             y * se + zr * ce,
                             IM_COL32(shade, shade, shade, alpha)});
    }
    std::vector<size_t> order(triangles_.size());
    std::iota(order.begin(), order.end(), 0);
    const auto depth = [&](size_t i) {
        const auto& t = triangles_[i];
        return projected[t.index[0]].depth + projected[t.index[1]].depth + projected[t.index[2]].depth;
    };
    // Orthographic mesh preview: depth-order native GPU triangles, preserving alpha hair / clothing layers.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return depth(a) < depth(b); });
    list->PushClipRect({rect.x, rect.y}, {rect.right(), rect.bottom()}, true);
    ImTextureID current = 0;
    for (size_t i : order) {
        const auto& triangle = triangles_[i];
        const ImTextureID texture = gpu_[triangle.texture];
        if (texture != current) {
            if (current)
                list->PopTextureID();
            list->PushTextureID(texture);
            current = texture;
        }
        list->PrimReserve(3, 3);
        const ImDrawIdx base = static_cast<ImDrawIdx>(list->_VtxCurrentIdx);
        list->PrimWriteIdx(base);
        list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
        list->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto& p = projected[triangle.index[corner]];
            list->PrimWriteVtx(p.point, {triangle.uv[corner * 2], triangle.uv[corner * 2 + 1]}, p.color);
        }
    }
    if (current)
        list->PopTextureID();
    list->PopClipRect();
}
} // namespace dsettings
