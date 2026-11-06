#include "input/CpuAffinity.h"

#include <cctype>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace drakan_input {

namespace {
std::string trimLower(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    std::string r(s.substr(b, e - b));
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
int popcount64(uint64_t v) {
    int n = 0;
    for (; v; v &= v - 1)
        ++n;
    return n;
}
bool parseNumber(const std::string& digits, int base, uint64_t& out) {
    if (digits.empty() || digits.size() > (base == 16 ? 16u : 3u))
        return false;
    uint64_t v = 0;
    for (char c : digits) {
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = 10 + c - 'a';
        else
            return false;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
    }
    out = v;
    return true;
}
} // namespace

bool parseAffinity(std::string_view text, AffinitySpec& out, std::string* error) {
    out = {};
    const std::string t = trimLower(text);
    if (t.empty() || t == "auto" || t == "all" || t == "default")
        return true;
    auto fail = [&](const char* why) {
        if (error)
            *error = std::string("cpu_affinity '") + std::string(text) + "': " + why +
                     ". Use auto, a hex mask such as 0xFFF (the first 12 logical processors), or cores:8.";
        return false;
    };
    if (t.rfind("cores:", 0) == 0) {
        uint64_t n = 0;
        if (!parseNumber(trimLower(t.substr(6)), 10, n) || n == 0 || n > 64)
            return fail("the core count must be 1 to 64");
        out = {AffinitySpec::Kind::Cores, n};
        return true;
    }
    if (t.rfind("0x", 0) == 0) {
        uint64_t m = 0;
        if (!parseNumber(t.substr(2), 16, m))
            return fail("not a hex mask of up to 16 digits");
        if (m == 0)
            return fail("the mask selects no processor");
        out = {AffinitySpec::Kind::Mask, m};
        return true;
    }
    return fail("a plain number is ambiguous, write a mask with the 0x prefix or cores:<n>");
}

uint64_t resolveAffinity(const AffinitySpec& spec, uint64_t allowed, std::string* note) {
    if (spec.kind == AffinitySpec::Kind::Auto || allowed == 0)
        return 0;
    uint64_t want = 0;
    if (spec.kind == AffinitySpec::Kind::Mask) {
        want = spec.value & allowed;
        if (want == 0) {
            if (note)
                *note = "the requested mask has no processor this process may use; the affinity is left unchanged";
            return 0;
        }
        if (want != spec.value && note)
            *note = "the mask was reduced to the processors this process may use";
    } else {
        int left = static_cast<int>(spec.value);
        for (int bit = 0; bit < 64 && left > 0; ++bit)
            if (allowed >> bit & 1) {
                want |= 1ull << bit;
                --left;
            }
        if (left > 0 && note)
            *note = "fewer logical processors are available than requested; using all of them";
    }
    return want == allowed ? 0 : want;
}

std::string applyProcessAffinity(std::string_view text) {
    AffinitySpec spec;
    std::string err;
    if (!parseAffinity(text, spec, &err))
        return "[drakan] " + err + " Ignored.";
    if (spec.kind == AffinitySpec::Kind::Auto)
        return {};
#if defined(_WIN32)
    DWORD_PTR process = 0, system = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process, &system))
        return "[drakan] cpu_affinity: cannot read the process affinity. Ignored.";
    std::string note;
    const uint64_t mask = resolveAffinity(spec, static_cast<uint64_t>(process), &note);
    char buf[160];
    if (mask == 0) {
        return note.empty() ? "" : "[drakan] cpu_affinity: " + note + ".";
    }
    if (!SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(mask))) {
        std::snprintf(buf, sizeof buf, "[drakan] cpu_affinity: Windows refused mask 0x%llX. Ignored.",
                      static_cast<unsigned long long>(mask));
        return buf;
    }
    std::snprintf(buf, sizeof buf, "[drakan] cpu_affinity: running on %d logical processors (mask 0x%llX)%s%s.",
                  popcount64(mask), static_cast<unsigned long long>(mask), note.empty() ? "" : "; ", note.c_str());
    return buf;
#else
    (void)popcount64;
    return "[drakan] cpu_affinity is only supported on Windows. Ignored.";
#endif
}

} // namespace drakan_input
