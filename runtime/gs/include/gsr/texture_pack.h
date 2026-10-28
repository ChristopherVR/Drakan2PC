#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gsr {
struct PackImage {
    uint32_t width = 0, height = 0;
    uint32_t scaleX = 1, scaleY = 1;
    std::vector<uint32_t> pixels;
};

// Content-addressed replacements. Background I/O never holds up a draw; callers
// retain the original image until the replacement is ready.
class TexturePack {
public:
    explicit TexturePack(const std::string& directory, size_t budgetBytes = 128u * 1024u * 1024u);
    ~TexturePack();
    bool empty() const;
    static std::array<uint64_t, 2> hashes(const std::vector<uint32_t>& original, uint32_t width, uint32_t height);
    // Precomputed hashes are valid only while the decoded source is unchanged.
    std::shared_ptr<const PackImage> findHashed(const std::vector<uint32_t>& original, uint32_t width, uint32_t height,
                                                uint64_t hash, uint64_t rgbHash);
    std::shared_ptr<const PackImage> find(const std::vector<uint32_t>& original, uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gsr
