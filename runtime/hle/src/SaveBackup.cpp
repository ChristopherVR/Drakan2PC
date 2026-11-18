#include "hle/SaveBackup.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

namespace drakan_save_backup {
namespace {
namespace fs = std::filesystem;
std::mutex g_archiveMutex;

bool sameFile(const fs::path& first, const fs::path& second, uint64_t& hash, std::span<const uint8_t> expected = {}) {
    std::ifstream a(first, std::ios::binary), b(second, std::ios::binary);
    if (!a || !b)
        return false;
    std::array<char, 4096> x, y;
    std::size_t offset = 0;
    do {
        a.read(x.data(), x.size());
        b.read(y.data(), y.size());
        const auto count = a.gcount();
        if (count != b.gcount() || !std::equal(x.begin(), x.begin() + count, y.begin()))
            return false;
        if (!expected.empty() &&
            (static_cast<std::size_t>(count) > expected.size() - offset ||
             std::memcmp(x.data(), expected.data() + offset, static_cast<std::size_t>(count)) != 0))
            return false;
        offset += static_cast<std::size_t>(count);
        for (auto i = x.begin(); i != x.begin() + count; ++i)
            hash = (hash ^ static_cast<unsigned char>(*i)) * 1099511628211ull;
    } while (a && b);
    return a.eof() && b.eof() && !a.bad() && !b.bad() && (expected.empty() || offset == expected.size());
}

struct PendingArchive {
    fs::path path;
    const std::vector<fs::path>& files;

    ~PendingArchive() {
        if (path.empty())
            return;
        std::error_code ec;
        for (const fs::path& file : files)
            fs::remove(path / file.filename(), ec);
        fs::remove(path, ec);
    }
};

bool publish(const fs::path& root, const fs::path& label, const std::vector<fs::path>& files, const fs::path& primary,
             std::span<const uint8_t> expected) {
    std::lock_guard<std::mutex> lock(g_archiveMutex);
    const fs::path base = root.parent_path() / "save-backups" / "ootf-auto" / root.filename() / label;
    fs::create_directories(base);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temp = base / (".pending-" + std::to_string(stamp));
    for (unsigned i = 1; !fs::create_directory(temp); ++i)
        temp = base / (".pending-" + std::to_string(stamp) + "-" + std::to_string(i));
    PendingArchive pending{temp, files};
    uint64_t hash = 14695981039346656037ull;
    for (const fs::path& file : files) {
        if (fs::symlink_status(file).type() != fs::file_type::regular)
            return false;
        const auto name = file.filename().generic_u8string();
        for (const char8_t c : name)
            hash = (hash ^ c) * 1099511628211ull;
        hash *= 1099511628211ull;
        const fs::path copy = temp / file.filename();
        fs::copy_file(file, copy);
        if (!sameFile(file, copy, hash, file == primary ? expected : std::span<const uint8_t>{}))
            return false;
    }
    char key[24];
    std::snprintf(key, sizeof key, "%016llx", static_cast<unsigned long long>(hash));
    for (unsigned i = 0;; ++i) {
        const fs::path target = base / (i == 0 ? std::string(key) : std::string(key) + "-" + std::to_string(i));
        std::error_code ec;
        fs::rename(temp, target, ec);
        if (!ec) {
            pending.path.clear();
            std::fprintf(stderr, "[saves] original archived in %s\n", target.string().c_str());
            return true;
        }
        if (!fs::is_directory(target))
            return false;
        uint64_t unused = 0;
        if (std::all_of(files.begin(), files.end(), [&](const fs::path& file) {
                const fs::path name = file.filename();
                return sameFile(temp / name, target / name, unused);
            }))
            return true;
    }
}

bool archive(const fs::path& cardRoot, const fs::path& original, bool wholeSlot, std::span<const uint8_t> expected) {
    try {
        fs::path root = fs::absolute(cardRoot).lexically_normal();
        if (!root.has_filename())
            root = root.parent_path();
        const fs::path file = fs::absolute(original).lexically_normal();
        const fs::path directory = file.parent_path();
        if (directory != root && directory.parent_path() != root)
            return false;
        if (fs::symlink_status(directory).type() != fs::file_type::directory ||
            fs::symlink_status(file).type() != fs::file_type::regular)
            return false;
        std::vector<fs::path> files;
        if (wholeSlot && directory != root) {
            for (const auto& entry : fs::directory_iterator(directory)) {
                const auto kind = entry.symlink_status().type();
                if (kind == fs::file_type::regular)
                    files.push_back(entry.path());
                else
                    return false;
            }
            if (std::find(files.begin(), files.end(), file) == files.end())
                return false;
            std::sort(files.begin(), files.end());
            return publish(root, directory.filename(), files, file, expected);
        }
        files.push_back(file);
        const fs::path record = fs::path(file.native() + fs::path(".mods").native());
        if (fs::symlink_status(record).type() != fs::file_type::not_found)
            files.push_back(record);
        const fs::path label =
            directory == root ? fs::path("level-states") / file.filename() : directory.filename() / file.filename();
        return publish(root, label, files, file, expected);
    } catch (const fs::filesystem_error&) {
        return false;
    }
}
} // namespace

bool archiveOriginal(const fs::path& cardRoot, const fs::path& original, std::span<const uint8_t> expected) {
    return archive(cardRoot, original, false, expected);
}

bool archiveSlot(const fs::path& cardRoot, const fs::path& primary, std::span<const uint8_t> expected) {
    return archive(cardRoot, primary, true, expected);
}

} // namespace drakan_save_backup
