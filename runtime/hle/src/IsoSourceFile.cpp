#include "hle/IsoFs.h"

#include <cstdio>
#include <limits>
#include <mutex>

namespace hle {
namespace {
class FileIsoSource final : public IsoSource {
    FILE* file_;
    mutable std::mutex mutex_;

public:
    explicit FileIsoSource(FILE* file) : file_(file) {}
    ~FileIsoSource() override { std::fclose(file_); }
    bool readAt(u64 offset, void* destination, size_t size) const override {
        std::lock_guard lock(mutex_);
        if (offset > static_cast<u64>(std::numeric_limits<int64_t>::max()))
            return false;
#if defined(_WIN32)
        if (_fseeki64(file_, static_cast<int64_t>(offset), SEEK_SET) != 0)
#else
        if (fseeko(file_, static_cast<off_t>(offset), SEEK_SET) != 0)
#endif
            return false;
        return std::fread(destination, 1, size, file_) == size;
    }
};
} // namespace

std::shared_ptr<const IsoSource> openIsoSource(const std::string& path, std::string* error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (error)
            *error = "cannot open " + path;
        return {};
    }
    return std::make_shared<FileIsoSource>(file);
}
} // namespace hle
