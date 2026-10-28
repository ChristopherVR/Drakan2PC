#include "rt_upload.h"
#include <cstdio>
#include <vector>

int main() {
    size_t checks = 0;
    for (uint32_t width : {1u, 7u, 64u, 128u, 512u})
        for (uint32_t rows : {1u, 3u, 32u, 64u})
            for (uint32_t scale = 1; scale <= 8; ++scale) {
                std::vector<uint32_t> input(size_t(width) * rows);
                for (size_t i = 0; i < input.size(); ++i)
                    input[i] = uint32_t(i * 2654435761u); // all channels, including alpha
                const size_t pitch = size_t(width) * scale, count = pitch * rows * scale;
                std::vector<uint32_t> output(count + 2, 0xdeadbeefu);
                gsr::expandRenderTargetRows(input.data(), output.data() + 1, width, rows, scale);
                if (output.front() != 0xdeadbeefu || output.back() != 0xdeadbeefu)
                    return 1;
                for (uint32_t y = 0; y < rows * scale; ++y)
                    for (size_t x = 0; x < pitch; ++x) {
                        ++checks;
                        if (output[1 + size_t(y) * pitch + x] != input[size_t(y / scale) * width + x / scale]) {
                            std::fprintf(stderr, "Mismatch: width=%u rows=%u scale=%u x=%zu y=%u\n", width, rows, scale,
                                         x, y);
                            return 1;
                        }
                    }
            }
    // Empty dimensions must not dereference pointers.
    gsr::expandRenderTargetRows(nullptr, nullptr, 0, 1, 4);
    gsr::expandRenderTargetRows(nullptr, nullptr, 1, 0, 4);
    gsr::expandRenderTargetRows(nullptr, nullptr, 1, 1, 0);
    std::printf("%zu exact pixel checks passed\n", checks);
}
