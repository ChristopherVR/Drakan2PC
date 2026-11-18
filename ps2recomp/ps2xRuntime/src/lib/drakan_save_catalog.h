#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace drakan_save_catalog {

struct Migration {
    std::string sourceBuild;
    std::vector<uint8_t> original;
    std::vector<uint8_t> state;
    std::vector<uint8_t> bookReturn;
};

inline bool isHistoricalBuild(std::string_view build) {
    return build == "d248f6c701888bf8" || build == "daf6d8d09586c549";
}

inline std::string sha256(std::span<const uint8_t> input) {
    constexpr uint32_t constants[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };
    std::array<uint32_t, 8> hash = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    const auto compress = [&](std::span<const uint8_t, 64> block) {
        std::array<uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i)
            for (size_t j = 0; j < 4; ++j)
                words[i] = (words[i] << 8) | block[4 * i + j];
        for (size_t i = 16; i < words.size(); ++i) {
            const uint32_t x = words[i - 15], y = words[i - 2];
            const uint32_t sigma0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
            const uint32_t sigma1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
            words[i] = words[i - 16] + sigma0 + words[i - 7] + sigma1;
        }
        auto [a, b, c, d, e, f, g, h] = hash;
        for (size_t i = 0; i < words.size(); ++i) {
            const uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + sum1 + choose + constants[i] + words[i];
            const uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + sum0 + majority;
        }
        const std::array result{a, b, c, d, e, f, g, h};
        for (size_t i = 0; i < hash.size(); ++i)
            hash[i] += result[i];
    };
    const uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8;
    while (input.size() >= 64) {
        compress(input.first<64>());
        input = input.subspan(64);
    }
    std::array<uint8_t, 64> tail{};
    std::copy(input.begin(), input.end(), tail.begin());
    tail[input.size()] = 0x80;
    if (input.size() >= 56) {
        compress(tail);
        tail.fill(0);
    }
    for (size_t i = 0; i < 8; ++i)
        tail[63 - i] = static_cast<uint8_t>(bitLength >> (8 * i));
    compress(tail);

    std::string hex;
    hex.reserve(64);
    for (const uint32_t word : hash)
        for (int shift = 28; shift >= 0; shift -= 4)
            hex += "0123456789abcdef"[(word >> shift) & 0xf];
    return hex;
}

inline std::vector<uint8_t> readBytes(const std::filesystem::path& path, size_t limit = 32 * 1024 * 1024) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size <= 0 || static_cast<std::uintmax_t>(size) > limit)
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.seekg(0).read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        file.peek() != std::char_traits<char>::eof())
        return {};
    return bytes;
}

inline std::vector<uint8_t> readAsset(const std::filesystem::path& root, std::string_view hash, std::string_view name) {
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != hash.npos ||
        name != std::string(hash) + ".bin")
        return {};
    auto bytes = readBytes(root / name);
    if (bytes.empty() || sha256(bytes) != hash)
        return {};
    return bytes;
}

inline std::optional<Migration> find(const std::filesystem::path& catalog, const std::filesystem::path& original,
                                     std::string_view sourceBuild, std::string_view targetBuild, bool isBookSave) {
    if (!sourceBuild.empty() && !isHistoricalBuild(sourceBuild))
        return {};
    const auto index = readBytes(catalog / "index.tsv", 256 * 1024);
    if (index.empty())
        return {};
    const std::string filename = original.filename().string();
    std::vector<uint8_t> originalBytes;
    std::string fingerprint;
    std::string_view remaining(reinterpret_cast<const char*>(index.data()), index.size());
    std::optional<Migration> found;
    while (!remaining.empty()) {
        const size_t end = remaining.find('\n');
        std::string_view line = remaining.substr(0, end);
        remaining.remove_prefix(end == remaining.npos ? remaining.size() : end + 1);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        std::array<std::string_view, 8> fields{};
        size_t fieldCount = 0;
        while (fieldCount < fields.size()) {
            const size_t tab = line.find('\t');
            fields[fieldCount++] = line.substr(0, tab);
            line.remove_prefix(tab == line.npos ? line.size() : tab + 1);
            if (tab == line.npos)
                break;
            if (fieldCount == fields.size())
                ++fieldCount;
        }
        const auto& [source, target, file, originalHash, replacementHash, replacementAsset, returnHash, returnAsset] =
            fields;
        if (fieldCount != fields.size() || !isHistoricalBuild(source) || target != targetBuild || file != filename ||
            (!sourceBuild.empty() && source != sourceBuild))
            continue;
        if (fingerprint.empty()) {
            originalBytes = readBytes(original);
            if (originalBytes.empty())
                return {};
            fingerprint = sha256(originalBytes);
        }
        if (originalHash != fingerprint)
            continue;
        if (found || (!isBookSave && (!returnHash.empty() || !returnAsset.empty())))
            return {};
        auto state = readAsset(catalog, replacementHash, replacementAsset);
        auto bookReturn = isBookSave ? readAsset(catalog, returnHash, returnAsset) : std::vector<uint8_t>{};
        if (state.empty() || (isBookSave && bookReturn.empty()))
            return {};
        found = Migration{std::string(source), std::move(originalBytes), std::move(state), std::move(bookReturn)};
    }
    return found;
}
} // namespace drakan_save_catalog
