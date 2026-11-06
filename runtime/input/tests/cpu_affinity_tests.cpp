// CPU affinity setting: parsing, resolution against the allowed set, and a live round trip on Windows.
#include "input/CpuAffinity.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace drakan_input;

static void check(bool ok, const char* name) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}
static AffinitySpec parse(const char* text, bool expectOk, std::string* err = nullptr) {
    AffinitySpec s;
    std::string e;
    const bool ok = parseAffinity(text, s, &e);
    check(ok == expectOk, text);
    if (err)
        *err = e;
    return s;
}

int main() {
    for (const char* t : {"", "auto", "AUTO", " all ", "default"})
        check(parse(t, true).kind == AffinitySpec::Kind::Auto, t);
    check(parse("0xFFF", true).value == 0xFFF, "0xFFF");
    check(parse("0Xff", true).value == 0xFF, "0Xff");
    check(parse(" 0x1 ", true).kind == AffinitySpec::Kind::Mask, "0x1 with spaces");
    check(parse("0xFFFFFFFFFFFFFFFF", true).value == ~0ull, "64 bits");
    check(parse("cores:8", true).kind == AffinitySpec::Kind::Cores, "cores:8");
    check(parse("cores:8", true).value == 8, "cores:8 value");

    std::string err;
    for (const char* t :
         {"0x0", "0x", "0xZZ", "0x10000000000000000", "12", "FFF", "cores:0", "cores:65", "cores:x", "cores:"}) {
        parse(t, false, &err);
        check(err.find("cpu_affinity") != std::string::npos && err.find("0xFFF") != std::string::npos, t);
    }
    parse("12", false, &err);
    check(err.find("ambiguous") != std::string::npos, "a plain number explains itself");

    std::string note;
    const uint64_t allowed = 0xFFFF; // 16 logical processors
    check(resolveAffinity({AffinitySpec::Kind::Auto, 0}, allowed, &note) == 0, "auto changes nothing");
    check(resolveAffinity({AffinitySpec::Kind::Mask, 0xFFF}, allowed, &note) == 0xFFF, "0xFFF on 16 cpus");
    check(note.empty(), "no note when exact");
    check(resolveAffinity({AffinitySpec::Kind::Mask, 0xFFFFF}, allowed, &note) == 0,
          "mask covering everything = unchanged");
    check(!note.empty(), "clipped mask is explained");
    note.clear();
    check(resolveAffinity({AffinitySpec::Kind::Mask, 0xF0000}, allowed, &note) == 0, "disjoint mask is ignored");
    check(note.find("no processor") != std::string::npos, "disjoint mask is explained");
    note.clear();
    check(resolveAffinity({AffinitySpec::Kind::Cores, 4}, allowed, &note) == 0xF, "cores:4");
    check(resolveAffinity({AffinitySpec::Kind::Cores, 4}, 0xAAAA, &note) == 0xAA,
          "cores:4 on sparse set takes the first four");
    note.clear();
    check(resolveAffinity({AffinitySpec::Kind::Cores, 32}, allowed, &note) == 0, "more cores than exist = unchanged");
    check(!note.empty(), "too many cores is explained");
    check(resolveAffinity({AffinitySpec::Kind::Mask, 0xFF}, 0, &note) == 0, "unknown allowed set = unchanged");

    check(applyProcessAffinity("auto").empty(), "auto applies silently");
    check(applyProcessAffinity("banana").find("Ignored") != std::string::npos, "bad text is reported");
#if defined(_WIN32)
    // Live: restrict this test process to its lowest allowed processor, then confirm.
    {
        const std::string msg = applyProcessAffinity("cores:1");
        std::fprintf(stderr, "%s\n", msg.c_str());
        // On a one-processor machine nothing changes (message empty); otherwise it must report 1 processor.
        check(msg.empty() || msg.find("1 logical processors") != std::string::npos, "cores:1 applied");
    }
#endif
    std::puts("cpu_affinity_test ok");
    return 0;
}
