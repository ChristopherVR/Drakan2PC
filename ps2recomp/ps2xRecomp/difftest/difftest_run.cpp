// Differential tester, part 2: run every generated case (the C++ the recompiler emits) against an independent R5900
// reference interpreter on randomized register / memory states and report every difference.
//
//   difftest_run [--states N] [--seed S] [--filter substr] [--show K] [--verbose]
//
// The reference interpreter below is written from the architecture (R5900 user manual / PCSX2 interpreter semantics),
// not from the emitter. Compared per case: all 32 GPRs (128 bit), HI/LO/HI1/LO1, SA, FPR bits, FPU accumulator, FCR31
// condition bit, the memory window the case may touch, the final pc, and the sequence of runtime events (calls, jumps,
// traps, syscalls, exceptions).
#include "difftest_common.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// ============================================================================================================================================
// Stubs for the PS2Runtime members the emitted code references (the object itself is never constructed: it is zeroed
// storage).
// ============================================================================================================================================
static std::vector<std::string> g_events;
#include <cstdarg>
static void ev(const char* fmt, ...) {
    char b[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    g_events.emplace_back(b);
}

const uint32_t g_ps2RecompiledFunctionTableBase = 0x100000u;
const uint32_t g_ps2RecompiledFunctionTableEnd = 0x100000u;
const uint32_t g_ps2RecompiledFunctionTableSlotCount = 0u;
PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[1] = {nullptr};
bool g_ps2DispatchTrace = false;
bool g_ps2Unwinding = false;

bool PS2Runtime::dispatchGuestBranch(uint8_t*, R5900Context* ctx, uint32_t targetPc, uint32_t sourcePc,
                                     uint32_t fallthroughPc, GuestBranchKind kind, const char*) {
    (void)sourcePc;
    if (kind == GuestBranchKind::DirectCall || kind == GuestBranchKind::IndirectCall) {
        ev("CALL t=%08x ret=%08x", targetPc, fallthroughPc);
        ctx->pc = fallthroughPc; // the callee "returns" at once
        return true;
    }
    ctx->pc = targetPc;
    return false; // jump: the function ends here
}
void PS2Runtime::SignalException(R5900Context*, PS2Exception e) {
    ev("EXC %d", (int)e);
}
void PS2Runtime::handleSyscall(uint8_t*, R5900Context*, uint32_t code) {
    ev("SYSCALL %05x", code);
}
void PS2Runtime::handleSyscall(uint8_t*, R5900Context*) {
    ev("SYSCALL");
}
void PS2Runtime::handleBreak(uint8_t*, R5900Context*) {
    ev("BREAK");
}
void PS2Runtime::handleTrap(uint8_t*, R5900Context*) {
    ev("TRAP");
}
void PS2Runtime::handleTLBR(uint8_t*, R5900Context*) {
    ev("TLBR");
}
void PS2Runtime::handleTLBWI(uint8_t*, R5900Context*) {
    ev("TLBWI");
}
void PS2Runtime::handleTLBWR(uint8_t*, R5900Context*) {
    ev("TLBWR");
}
void PS2Runtime::handleTLBP(uint8_t*, R5900Context*) {
    ev("TLBP");
}
void PS2Runtime::clearLLBit(R5900Context*) {}
bool PS2Runtime::cpFlush(uint32_t) noexcept {
    return false;
}
void PS2Runtime::reportBadResume(uint32_t pc, uint32_t fs) noexcept {
    ev("BADRESUME %08x %08x", pc, fs);
}
void PS2Runtime::traceTransfer(R5900Context*, GuestBranchKind, uint32_t, uint32_t) {}
uint32_t PS2Runtime::readEeCount(R5900Context*) {
    return 0x12345678u;
}
void PS2Runtime::vu0StartMicroProgram(uint8_t*, R5900Context*, uint32_t a) {
    ev("VU0START %x", a);
}
void PS2Runtime::executeVU0Microprogram(uint8_t*, R5900Context*, uint32_t a) {
    ev("VU0EXEC %x", a);
}
uint8_t PS2Runtime::Load8(uint8_t*, R5900Context*, uint32_t) {
    std::abort();
}
uint16_t PS2Runtime::Load16(uint8_t*, R5900Context*, uint32_t) {
    std::abort();
}
uint32_t PS2Runtime::Load32(uint8_t*, R5900Context*, uint32_t) {
    std::abort();
}
uint64_t PS2Runtime::Load64(uint8_t*, R5900Context*, uint32_t) {
    std::abort();
}
__m128i PS2Runtime::Load128(uint8_t*, R5900Context*, uint32_t) {
    std::abort();
}
void PS2Runtime::Store8(uint8_t*, R5900Context*, uint32_t, uint8_t) {
    std::abort();
}
void PS2Runtime::Store16(uint8_t*, R5900Context*, uint32_t, uint16_t) {
    std::abort();
}
void PS2Runtime::Store32(uint8_t*, R5900Context*, uint32_t, uint32_t) {
    std::abort();
}
void PS2Runtime::Store64(uint8_t*, R5900Context*, uint32_t, uint64_t) {
    std::abort();
}
void PS2Runtime::Store128(uint8_t*, R5900Context*, uint32_t, __m128i) {
    std::abort();
}
void ps2WatchWriteHit(uint32_t, uint32_t, uint64_t, const R5900Context*) {}
void ps2WatchFlush() {}

// ============================================================================================================================================
// Random numbers and state
// ============================================================================================================================================
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0xABCDEFull) {
        for (int i = 0; i < 4; ++i)
            next64();
    }
    uint64_t next64() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s * 0x2545F4914F6CDD1Dull;
    }
    uint32_t next() { return static_cast<uint32_t>(next64() >> 32); }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

struct U128 {
    uint64_t d[2];
    uint32_t& w(int i) { return reinterpret_cast<uint32_t*>(d)[i]; }
    uint16_t& h(int i) { return reinterpret_cast<uint16_t*>(d)[i]; }
    uint8_t& b(int i) { return reinterpret_cast<uint8_t*>(d)[i]; }
    int32_t& sw(int i) { return reinterpret_cast<int32_t*>(d)[i]; }
    int16_t& sh(int i) { return reinterpret_cast<int16_t*>(d)[i]; }
    int8_t& sb(int i) { return reinterpret_cast<int8_t*>(d)[i]; }
    bool operator==(const U128& o) const { return d[0] == o.d[0] && d[1] == o.d[1]; }
};

struct Ref {
    U128 r[32];
    uint64_t hi, lo, hi1, lo1;
    uint32_t sa;
    uint32_t f[32];
    uint32_t acc;
    uint32_t fcr31;
    uint32_t pc;
    uint32_t vf[32][4]; // VU0 macro mode registers
    uint32_t vacc[4];
    uint16_t vi[16];
    uint32_t vq, vI;
    uint32_t status; // COP0 Status (EI / DI)
};

static constexpr uint32_t BASE = 0x00200000u;
static constexpr uint32_t WIN = 0x00100000u, WINSZ = 0x2000u;
static uint8_t *g_ramE, *g_ramR;
static constexpr uint32_t RAMSZ = 32u << 20;

static const uint64_t kSpecial64[] = {0,
                                      1,
                                      ~0ull,
                                      0x7fffffffull,
                                      0x80000000ull,
                                      0xffffffffull,
                                      0xffffffff80000000ull,
                                      0x7fffffffffffffffull,
                                      0x8000000000000000ull,
                                      0x100000000ull,
                                      0x00000001ffffffffull,
                                      0x0000ffffull,
                                      0xffffffffffff8000ull,
                                      0xffffull,
                                      0x8000ull,
                                      0x7fffull,
                                      2,
                                      3,
                                      0xfffffffffffffffeull,
                                      0x00ff00ff00ff00ffull};
static const uint32_t kSpecialF[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x40000000, 0x7f7fffff,
                                     0xff7fffff, 0x7f800000, 0xff800000, 0x7fffffff, 0xffffffff, 0x00800000,
                                     0x007fffff, 0x00000001, 0x80000001, 0x4f000000, 0x4effffff, 0xcf000000,
                                     0x4e800000, 0x3f000000, 0x41200000, 0x7f000000, 0x3fc00000, 0xc0490fdb,
                                     0x42f60000, 0x4b800000, 0x4b7fffff, 0x7f800001, 0x3f7fffff, 0x33800000};

static uint64_t randGpr(Rng& g) {
    switch (g.below(10)) {
        case 0:
        case 1:
            return g.next64();
        case 2:
        case 3:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(g.next())));
        case 4:
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(g.below(17)) - 8));
        case 5:
        case 6:
            return kSpecial64[g.below(sizeof(kSpecial64) / sizeof(kSpecial64[0]))];
        case 7:
            return g.next();
        case 8:
            return (static_cast<uint64_t>(g.next() & 0xFFu) << 32) | g.next();
        default:
            return static_cast<uint64_t>(static_cast<int64_t>(
                static_cast<int32_t>(kSpecial64[g.below(sizeof(kSpecial64) / sizeof(kSpecial64[0]))])));
    }
}
static uint32_t randFloatBits(Rng& g, bool allowDenorm) {
    switch (g.below(8)) {
        case 0:
        case 1: {
            uint32_t b = kSpecialF[g.below(sizeof(kSpecialF) / sizeof(kSpecialF[0]))];
            if (!allowDenorm && (b & 0x7f800000u) == 0)
                b &= 0x80000000u;
            return b;
        }
        case 2: {
            uint32_t b = g.next();
            if (!allowDenorm && (b & 0x7f800000u) == 0)
                b &= 0x80000000u;
            return b;
        }
        default: {
            const uint32_t e = 100 + g.below(60);
            return (g.next() & 0x80000000u) | (e << 23) | (g.next() & 0x7FFFFFu);
        }
    }
}

static void randomState(Rng& g, Ref& r, bool allowDenorm) {
    for (int i = 0; i < 32; ++i) {
        r.r[i].d[0] = randGpr(g);
        r.r[i].d[1] = (g.below(3) == 0) ? randGpr(g) : g.next64();
    }
    r.r[0].d[0] = r.r[0].d[1] = 0;
    r.hi = randGpr(g);
    r.lo = randGpr(g);
    r.hi1 = randGpr(g);
    r.lo1 = randGpr(g);
    r.sa = g.below(4) == 0 ? 0 : g.below(128);
    for (int i = 0; i < 32; ++i)
        r.f[i] = randFloatBits(g, allowDenorm);
    r.acc = randFloatBits(g, allowDenorm);
    r.fcr31 = g.next() & 0x0183FFFFu;
    r.pc = BASE;
    auto vuf = [&]() -> uint32_t {
        if (g.below(12) == 0)
            return (g.next() & 0x80000000u);  // +-0
        const uint32_t e = 118 + g.below(20); // normal range: no overflow / underflow in one operation
        return (g.next() & 0x80000000u) | (e << 23) | (g.next() & 0x7FFFFFu);
    };
    for (int i = 0; i < 32; ++i)
        for (int k = 0; k < 4; ++k)
            r.vf[i][k] = vuf();
    for (int k = 0; k < 4; ++k)
        r.vacc[k] = vuf();
    for (int i = 0; i < 16; ++i)
        r.vi[i] = static_cast<uint16_t>(g.next());
    r.vi[0] = 0;
    r.vq = vuf();
    r.vI = vuf();
    r.vf[0][0] = r.vf[0][1] = r.vf[0][2] = 0;
    r.vf[0][3] = 0x3F800000u; // vf0 = (0, 0, 0, 1)
    r.status = g.next() & 0xFF57FFFFu;
}

static void toCtx(const Ref& r, R5900Context& c) {
    for (int i = 0; i < 32; ++i)
        std::memcpy(&c.r[i], &r.r[i], 16);
    c.hi = r.hi;
    c.lo = r.lo;
    c.hi1 = r.hi1;
    c.lo1 = r.lo1;
    c.sa = r.sa;
    for (int i = 0; i < 32; ++i)
        std::memcpy(&c.f[i], &r.f[i], 4);
    std::memcpy(&c.f_acc, &r.acc, 4);
    c.fcr31 = r.fcr31;
    c.pc = BASE;
    for (int i = 0; i < 32; ++i)
        std::memcpy(&c.vu0_vf[i], r.vf[i], 16);
    std::memcpy(&c.vu0_acc, r.vacc, 16);
    for (int i = 0; i < 16; ++i)
        c.vi[i] = r.vi[i];
    std::memcpy(&c.vu0_q, &r.vq, 4);
    std::memcpy(&c.vu0_i, &r.vI, 4);
    c.cop0_status = r.status;
}
static void fromCtx(const R5900Context& c, Ref& r) {
    for (int i = 0; i < 32; ++i)
        std::memcpy(&r.r[i], &c.r[i], 16);
    r.hi = c.hi;
    r.lo = c.lo;
    r.hi1 = c.hi1;
    r.lo1 = c.lo1;
    r.sa = c.sa;
    for (int i = 0; i < 32; ++i)
        std::memcpy(&r.f[i], &c.f[i], 4);
    std::memcpy(&r.acc, &c.f_acc, 4);
    r.fcr31 = c.fcr31;
    r.pc = c.pc;
    for (int i = 0; i < 32; ++i)
        std::memcpy(r.vf[i], &c.vu0_vf[i], 16);
    std::memcpy(r.vacc, &c.vu0_acc, 16);
    for (int i = 0; i < 16; ++i)
        r.vi[i] = c.vi[i];
    std::memcpy(&r.vq, &c.vu0_q, 4);
    std::memcpy(&r.vI, &c.vu0_i, 4);
    r.status = c.cop0_status;
}

// ============================================================================================================================================
// PS2 float model (no NaN / Inf, exponent 255 is a normal exponent, exponent 0 is zero, overflow saturates to
// 0x7FFFFFFF)
// ============================================================================================================================================
static double ps2d(uint32_t b) {
    const int e = (b >> 23) & 255;
    const double sign = (b & 0x80000000u) ? -1.0 : 1.0;
    if (e == 0)
        return sign * 0.0;
    const double m = 1.0 + static_cast<double>(b & 0x7FFFFFu) / 8388608.0;
    return sign * std::ldexp(m, e - 127);
}
static uint32_t d2ps(double d) {
    const uint32_t sign = std::signbit(d) ? 0x80000000u : 0u;
    const double mag = std::fabs(d);
    if (mag == 0.0)
        return sign;
    if (mag >= 0x1p129)
        return sign | 0x7FFFFFFFu;
    if (mag < 0x1p-126)
        return sign;
    if (mag >= 0x1p128) {
        const float h = static_cast<float>(mag * 0.5);
        uint32_t hb;
        std::memcpy(&hb, &h, 4);
        if ((hb & 0x7F800000u) == 0x7F800000u)
            return sign | 0x7FFFFFFFu;
        return sign | (hb + 0x00800000u);
    }
    const float fl = static_cast<float>(mag);
    uint32_t b;
    std::memcpy(&b, &fl, 4);
    if ((b & 0x7F800000u) == 0x7F800000u)
        return sign | 0x7F800000u; // rounded up to exactly 2^128: a valid PS2 value
    return sign | b;
}
static int64_t ulpDist(uint32_t a, uint32_t b) {
    auto key = [](uint32_t x) -> int64_t {
        return (x & 0x80000000u) ? -static_cast<int64_t>(x & 0x7FFFFFFFu) : static_cast<int64_t>(x & 0x7FFFFFFFu);
    };
    const int64_t d = key(a) - key(b);
    return d < 0 ? -d : d;
}

// ============================================================================================================================================
// Reference interpreter
// ============================================================================================================================================
struct Mem {
    uint8_t* p;
    uint8_t* at(uint32_t a) { return p + (a & 0x1FFFFFFFu) % RAMSZ; }
    uint8_t r8(uint32_t a) { return *at(a); }
    uint16_t r16(uint32_t a) {
        uint16_t v;
        std::memcpy(&v, at(a), 2);
        return v;
    }
    uint32_t r32(uint32_t a) {
        uint32_t v;
        std::memcpy(&v, at(a), 4);
        return v;
    }
    uint64_t r64(uint32_t a) {
        uint64_t v;
        std::memcpy(&v, at(a), 8);
        return v;
    }
    void w8(uint32_t a, uint8_t v) { *at(a) = v; }
    void w16(uint32_t a, uint16_t v) { std::memcpy(at(a), &v, 2); }
    void w32(uint32_t a, uint32_t v) { std::memcpy(at(a), &v, 4); }
    void w64(uint32_t a, uint64_t v) { std::memcpy(at(a), &v, 8); }
};

static inline uint64_t sx32(uint32_t v) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)));
}

// Executes one non-control-flow instruction. Returns false when the instruction is not part of the reference (reserved
// / system).
static bool refExec(Ref& R, Mem& M, uint32_t w, bool& skip) {
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31, sa = (w >> 6) & 31,
                   fn = w & 63;
    const int32_t simm = static_cast<int16_t>(w & 0xFFFF);
    const uint32_t imm = w & 0xFFFF;
    auto S = [&](int i) -> int64_t { return static_cast<int64_t>(R.r[i].d[0]); };
    auto U = [&](int i) -> uint64_t { return R.r[i].d[0]; };
    auto setd = [&](uint32_t n, uint64_t v) {
        if (n)
            R.r[n].d[0] = v;
    };
    auto setq = [&](uint32_t n, const U128& v) {
        if (n)
            R.r[n] = v;
    };
    auto Q = [&](uint32_t n) -> U128 {
        U128 v = R.r[n];
        if (!n)
            v.d[0] = v.d[1] = 0;
        return v;
    };
    auto ea = [&]() -> uint32_t { return static_cast<uint32_t>(U(rs)) + static_cast<uint32_t>(simm); };
    skip = false;
    switch (op) {
        case 0x00: // SPECIAL
            switch (fn) {
                case 0x00:
                    setd(rd, sx32(static_cast<uint32_t>(U(rt)) << sa));
                    return true;
                case 0x02:
                    setd(rd, sx32(static_cast<uint32_t>(U(rt)) >> sa));
                    return true;
                case 0x03:
                    setd(rd, sx32(static_cast<uint32_t>(static_cast<int32_t>(U(rt)) >> sa)));
                    return true;
                case 0x04:
                    setd(rd, sx32(static_cast<uint32_t>(U(rt)) << (U(rs) & 31)));
                    return true;
                case 0x06:
                    setd(rd, sx32(static_cast<uint32_t>(U(rt)) >> (U(rs) & 31)));
                    return true;
                case 0x07:
                    setd(rd, sx32(static_cast<uint32_t>(static_cast<int32_t>(U(rt)) >> (U(rs) & 31))));
                    return true;
                case 0x0A:
                    if (U(rt) == 0)
                        setd(rd, U(rs));
                    return true; // MOVZ (64 bit move)
                case 0x0B:
                    if (U(rt) != 0)
                        setd(rd, U(rs));
                    return true; // MOVN
                case 0x0C:
                    ev("SYSCALL %05x", (w >> 6) & 0xFFFFF);
                    return true;
                case 0x0D:
                    ev("BREAK");
                    return true;
                case 0x0F:
                    return true; // SYNC
                case 0x10:
                    setd(rd, R.hi);
                    return true;
                case 0x11:
                    R.hi = U(rs);
                    return true;
                case 0x12:
                    setd(rd, R.lo);
                    return true;
                case 0x13:
                    R.lo = U(rs);
                    return true;
                case 0x14:
                    setd(rd, U(rt) << (U(rs) & 63));
                    return true;
                case 0x16:
                    setd(rd, U(rt) >> (U(rs) & 63));
                    return true;
                case 0x17:
                    setd(rd, static_cast<uint64_t>(S(rt) >> (U(rs) & 63)));
                    return true;
                case 0x18: {
                    const int64_t p = static_cast<int64_t>(static_cast<int32_t>(U(rs))) * static_cast<int32_t>(U(rt));
                    R.lo = sx32(static_cast<uint32_t>(p));
                    R.hi = sx32(static_cast<uint32_t>(p >> 32));
                    setd(rd, R.lo);
                    return true;
                }
                case 0x19: {
                    const uint64_t p =
                        static_cast<uint64_t>(static_cast<uint32_t>(U(rs))) * static_cast<uint32_t>(U(rt));
                    R.lo = sx32(static_cast<uint32_t>(p));
                    R.hi = sx32(static_cast<uint32_t>(p >> 32));
                    setd(rd, R.lo);
                    return true;
                }
                case 0x1A: {
                    const int32_t a = static_cast<int32_t>(U(rs)), b = static_cast<int32_t>(U(rt));
                    if (b == 0) {
                        R.lo = a < 0 ? 1 : ~0ull;
                        R.hi = sx32(static_cast<uint32_t>(a));
                    } else if (a == INT32_MIN && b == -1) {
                        R.lo = sx32(0x80000000u);
                        R.hi = 0;
                    } else {
                        R.lo = sx32(static_cast<uint32_t>(a / b));
                        R.hi = sx32(static_cast<uint32_t>(a % b));
                    }
                    return true;
                }
                case 0x1B: {
                    const uint32_t a = static_cast<uint32_t>(U(rs)), b = static_cast<uint32_t>(U(rt));
                    if (b == 0) {
                        R.lo = ~0ull;
                        R.hi = sx32(a);
                    } else {
                        R.lo = sx32(a / b);
                        R.hi = sx32(a % b);
                    }
                    return true;
                }
                case 0x20: {
                    const int64_t r = static_cast<int64_t>(static_cast<int32_t>(U(rs))) + static_cast<int32_t>(U(rt));
                    if (r != static_cast<int32_t>(r))
                        ev("EXC 12");
                    else
                        setd(rd, sx32(static_cast<uint32_t>(r)));
                    return true;
                }
                case 0x21:
                    setd(rd, sx32(static_cast<uint32_t>(U(rs)) + static_cast<uint32_t>(U(rt))));
                    return true;
                case 0x22: {
                    const int64_t r = static_cast<int64_t>(static_cast<int32_t>(U(rs))) - static_cast<int32_t>(U(rt));
                    if (r != static_cast<int32_t>(r))
                        ev("EXC 12");
                    else
                        setd(rd, sx32(static_cast<uint32_t>(r)));
                    return true;
                }
                case 0x23:
                    setd(rd, sx32(static_cast<uint32_t>(U(rs)) - static_cast<uint32_t>(U(rt))));
                    return true;
                case 0x24:
                    setd(rd, U(rs) & U(rt));
                    return true;
                case 0x25:
                    setd(rd, U(rs) | U(rt));
                    return true;
                case 0x26:
                    setd(rd, U(rs) ^ U(rt));
                    return true;
                case 0x27:
                    setd(rd, ~(U(rs) | U(rt)));
                    return true;
                case 0x28:
                    setd(rd, R.sa);
                    return true; // MFSA
                case 0x29:
                    skip = true;
                    return false; // MTSA: semantics of the upper bits not verified
                case 0x2A:
                    setd(rd, S(rs) < S(rt) ? 1 : 0);
                    return true;
                case 0x2B:
                    setd(rd, U(rs) < U(rt) ? 1 : 0);
                    return true;
                case 0x2C: {
                    const int64_t a = S(rs), b = S(rt),
                                  r = static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
                    if (((a ^ b) >= 0) && ((a ^ r) < 0))
                        ev("EXC 12");
                    else
                        setd(rd, static_cast<uint64_t>(r));
                    return true;
                }
                case 0x2D:
                    setd(rd, U(rs) + U(rt));
                    return true;
                case 0x2E: {
                    const int64_t a = S(rs), b = S(rt),
                                  r = static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
                    if (((a ^ b) < 0) && ((a ^ r) < 0))
                        ev("EXC 12");
                    else
                        setd(rd, static_cast<uint64_t>(r));
                    return true;
                }
                case 0x2F:
                    setd(rd, U(rs) - U(rt));
                    return true;
                case 0x30:
                    if (S(rs) >= S(rt))
                        ev("TRAP");
                    return true;
                case 0x31:
                    if (U(rs) >= U(rt))
                        ev("TRAP");
                    return true;
                case 0x32:
                    if (S(rs) < S(rt))
                        ev("TRAP");
                    return true;
                case 0x33:
                    if (U(rs) < U(rt))
                        ev("TRAP");
                    return true;
                case 0x34:
                    if (U(rs) == U(rt))
                        ev("TRAP");
                    return true;
                case 0x36:
                    if (U(rs) != U(rt))
                        ev("TRAP");
                    return true;
                case 0x38:
                    setd(rd, U(rt) << sa);
                    return true;
                case 0x3A:
                    setd(rd, U(rt) >> sa);
                    return true;
                case 0x3B:
                    setd(rd, static_cast<uint64_t>(S(rt) >> sa));
                    return true;
                case 0x3C:
                    setd(rd, U(rt) << (sa + 32));
                    return true;
                case 0x3E:
                    setd(rd, U(rt) >> (sa + 32));
                    return true;
                case 0x3F:
                    setd(rd, static_cast<uint64_t>(S(rt) >> (sa + 32)));
                    return true;
                default:
                    return false; // JR / JALR handled by the caller; the rest is reserved on the R5900
            }
        case 0x01: // REGIMM (non-branch part)
            switch (rt) {
                case 0x08:
                    if (S(rs) >= static_cast<int64_t>(simm))
                        ev("TRAP");
                    return true;
                case 0x09:
                    if (U(rs) >= static_cast<uint64_t>(static_cast<int64_t>(simm)))
                        ev("TRAP");
                    return true;
                case 0x0A:
                    if (S(rs) < static_cast<int64_t>(simm))
                        ev("TRAP");
                    return true;
                case 0x0B:
                    if (U(rs) < static_cast<uint64_t>(static_cast<int64_t>(simm)))
                        ev("TRAP");
                    return true;
                case 0x0C:
                    if (S(rs) == static_cast<int64_t>(simm))
                        ev("TRAP");
                    return true;
                case 0x0E:
                    if (S(rs) != static_cast<int64_t>(simm))
                        ev("TRAP");
                    return true;
                case 0x18:
                    R.sa = (((static_cast<uint32_t>(U(rs)) & 0xF) ^ (imm & 0xF))) * 8;
                    return true; // MTSAB
                case 0x19:
                    R.sa = (((static_cast<uint32_t>(U(rs)) & 0x7) ^ (imm & 0x7))) * 16;
                    return true; // MTSAH
                default:
                    return false;
            }
        case 0x08: {
            const int64_t r = static_cast<int64_t>(static_cast<int32_t>(U(rs))) + simm;
            if (r != static_cast<int32_t>(r))
                ev("EXC 12");
            else
                setd(rt, sx32(static_cast<uint32_t>(r)));
            return true;
        }
        case 0x09:
            setd(rt, sx32(static_cast<uint32_t>(U(rs)) + static_cast<uint32_t>(simm)));
            return true;
        case 0x0A:
            setd(rt, S(rs) < static_cast<int64_t>(simm) ? 1 : 0);
            return true;
        case 0x0B:
            setd(rt, U(rs) < static_cast<uint64_t>(static_cast<int64_t>(simm)) ? 1 : 0);
            return true;
        case 0x0C:
            setd(rt, U(rs) & imm);
            return true;
        case 0x0D:
            setd(rt, U(rs) | imm);
            return true;
        case 0x0E:
            setd(rt, U(rs) ^ imm);
            return true;
        case 0x0F:
            setd(rt, sx32(imm << 16));
            return true;
        case 0x18: {
            const int64_t a = S(rs), b = simm,
                          r = static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
            if (((a ^ b) >= 0) && ((a ^ r) < 0))
                ev("EXC 12");
            else
                setd(rt, static_cast<uint64_t>(r));
            return true;
        } // DADDI
        case 0x19:
            setd(rt, U(rs) + static_cast<uint64_t>(static_cast<int64_t>(simm)));
            return true; // DADDIU
        // ---- loads / stores (little endian)
        case 0x20:
            setd(rt, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(M.r8(ea())))));
            return true;
        case 0x21:
            setd(rt, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(M.r16(ea())))));
            return true;
        case 0x23:
            setd(rt, sx32(M.r32(ea())));
            return true;
        case 0x24:
            setd(rt, M.r8(ea()));
            return true;
        case 0x25:
            setd(rt, M.r16(ea()));
            return true;
        case 0x27:
            setd(rt, M.r32(ea()));
            return true;
        case 0x37:
            setd(rt, M.r64(ea()));
            return true;
        case 0x1E: {
            const uint32_t a = ea() & ~15u;
            U128 v;
            v.d[0] = M.r64(a);
            v.d[1] = M.r64(a + 8);
            setq(rt, v);
            return true;
        }
        case 0x1F: {
            const uint32_t a = ea() & ~15u;
            const U128 v = Q(rt);
            M.w64(a, v.d[0]);
            M.w64(a + 8, v.d[1]);
            return true;
        }
        case 0x28:
            M.w8(ea(), static_cast<uint8_t>(U(rt)));
            return true;
        case 0x29:
            M.w16(ea(), static_cast<uint16_t>(U(rt)));
            return true;
        case 0x2B:
            M.w32(ea(), static_cast<uint32_t>(U(rt)));
            return true;
        case 0x3F:
            M.w64(ea(), U(rt));
            return true;
        case 0x22: // LWL
        {
            const uint32_t a = ea(), o = a & 3, base = a & ~3u;
            uint32_t reg = static_cast<uint32_t>(U(rt));
            for (uint32_t i = 0; i <= o; ++i) {
                const uint32_t bi = 3 - (o - i);
                reg = (reg & ~(0xFFu << (8 * bi))) | (static_cast<uint32_t>(M.r8(base + i)) << (8 * bi));
            }
            setd(rt, sx32(reg));
            return true;
        }
        case 0x26: // LWR
        {
            const uint32_t a = ea(), o = a & 3, base = a & ~3u;
            uint32_t reg = static_cast<uint32_t>(U(rt));
            for (uint32_t i = o; i < 4; ++i) {
                const uint32_t bi = i - o;
                reg = (reg & ~(0xFFu << (8 * bi))) | (static_cast<uint32_t>(M.r8(base + i)) << (8 * bi));
            }
            if (o == 0)
                setd(rt, sx32(reg));
            else
                setd(rt, (U(rt) & 0xFFFFFFFF00000000ull) | reg);
            return true;
        }
        case 0x2A: // SWL
        {
            const uint32_t a = ea(), o = a & 3, base = a & ~3u;
            const uint32_t reg = static_cast<uint32_t>(U(rt));
            for (uint32_t i = 0; i <= o; ++i)
                M.w8(base + i, static_cast<uint8_t>(reg >> (8 * (3 - (o - i)))));
            return true;
        }
        case 0x2E: // SWR
        {
            const uint32_t a = ea(), o = a & 3, base = a & ~3u;
            const uint32_t reg = static_cast<uint32_t>(U(rt));
            for (uint32_t i = o; i < 4; ++i)
                M.w8(base + i, static_cast<uint8_t>(reg >> (8 * (i - o))));
            return true;
        }
        case 0x1A: // LDL
        {
            const uint32_t a = ea(), o = a & 7, base = a & ~7u;
            uint64_t reg = U(rt);
            for (uint32_t i = 0; i <= o; ++i) {
                const uint32_t bi = 7 - (o - i);
                reg = (reg & ~(0xFFull << (8 * bi))) | (static_cast<uint64_t>(M.r8(base + i)) << (8 * bi));
            }
            setd(rt, reg);
            return true;
        }
        case 0x1B: // LDR
        {
            const uint32_t a = ea(), o = a & 7, base = a & ~7u;
            uint64_t reg = U(rt);
            for (uint32_t i = o; i < 8; ++i) {
                const uint32_t bi = i - o;
                reg = (reg & ~(0xFFull << (8 * bi))) | (static_cast<uint64_t>(M.r8(base + i)) << (8 * bi));
            }
            setd(rt, reg);
            return true;
        }
        case 0x2C: // SDL
        {
            const uint32_t a = ea(), o = a & 7, base = a & ~7u;
            for (uint32_t i = 0; i <= o; ++i)
                M.w8(base + i, static_cast<uint8_t>(U(rt) >> (8 * (7 - (o - i)))));
            return true;
        }
        case 0x2D: // SDR
        {
            const uint32_t a = ea(), o = a & 7, base = a & ~7u;
            for (uint32_t i = o; i < 8; ++i)
                M.w8(base + i, static_cast<uint8_t>(U(rt) >> (8 * (i - o))));
            return true;
        }
        case 0x2F:
            return true; // CACHE
        case 0x33:
            return true; // PREF
        case 0x36: {
            const uint32_t a = ea() & ~15u;
            if (rt) {
                uint64_t d0 = M.r64(a), d1 = M.r64(a + 8);
                std::memcpy(&R.vf[rt][0], &d0, 8);
                std::memcpy(&R.vf[rt][2], &d1, 8);
            }
            return true;
        } // LQC2
        case 0x3E: {
            const uint32_t a = ea() & ~15u;
            uint64_t d0, d1;
            std::memcpy(&d0, &R.vf[rt][0], 8);
            std::memcpy(&d1, &R.vf[rt][2], 8);
            M.w64(a, d0);
            M.w64(a + 8, d1);
            return true;
        } // SQC2
        case 0x31:
            R.f[rt] = M.r32(ea());
            return true; // LWC1
        case 0x39:
            M.w32(ea(), R.f[rt]);
            return true; // SWC1

        // ---- COP0: EI / DI only (the other COP0 registers are host state: Count is wall-clock driven, TLB ops are not
        // modelled)
        case 0x10:
            if (rs == 16 && fn == 0x38) {
                R.status |= 0x10000u;
                return true;
            } // EI
            if (rs == 16 && fn == 0x39) {
                R.status &= ~0x10000u;
                return true;
            } // DI
            return false;
        // ---- COP2 (VU0 macro mode). Float inputs are restricted to the normal range by the harness, so plain IEEE
        // arithmetic is the model.
        case 0x12: {
            const uint32_t fmt = rs;
            if (fmt == 0x01) {
                if (rt) {
                    std::memcpy(&R.r[rt].d[0], &R.vf[rd][0], 16);
                }
                return true;
            } // QMFC2
            if (fmt == 0x05) {
                if (rd) {
                    std::memcpy(&R.vf[rd][0], &R.r[rt].d[0], 16);
                    if (!rt)
                        std::memset(&R.vf[rd][0], 0, 16);
                }
                return true;
            } // QMTC2 (vf0 is constant)
            if (fmt == 0x02) {
                if (rd < 16) {
                    setd(rt, R.vi[rd]);
                    return true;
                }
                skip = true;
                return false;
            } // CFC2 (VI registers)
            if (fmt == 0x06) {
                if (rd < 16) {
                    if (rd)
                        R.vi[rd] = static_cast<uint16_t>(U(rt));
                    return true;
                }
                skip = true;
                return false;
            } // CTC2 (VI registers)
            if (fmt < 0x10)
                return false;
            const uint32_t dest = (w >> 21) & 15, ft = rt, fs = rd, fd = sa;
            auto F = [](uint32_t b) -> double {
                return (((b >> 23) & 255) == 0) ? 0.0 : static_cast<double>(*reinterpret_cast<float*>(&b));
            };
            auto Fb = [](double d) -> uint32_t {
                float f = static_cast<float>(d);
                uint32_t b;
                std::memcpy(&b, &f, 4);
                if ((b & 0x7F800000u) == 0)
                    b &= 0x80000000u; // flush denormals
                return b;
            };
            auto lane = [&](uint32_t i) { return ((dest >> (3 - i)) & 1) != 0; };
            // operand selection
            auto bcv = [&](uint32_t reg, uint32_t k, uint32_t bc) -> uint32_t {
                (void)k;
                return R.vf[reg][bc];
            };
            auto doVec = [&](int opk /*0 add 1 sub 2 mul 3 max 4 mini 5 madd 6 msub*/, int src /*0 vf 1 bc 2 q 3 i*/,
                             uint32_t bc, bool toAcc, uint32_t dst) {
                uint32_t res[4];
                for (uint32_t k = 0; k < 4; ++k) {
                    const double a = F(R.vf[fs][k]);
                    const double b =
                        src == 0 ? F(R.vf[ft][k]) : (src == 1 ? F(bcv(ft, k, bc)) : (src == 2 ? F(R.vq) : F(R.vI)));
                    const uint32_t braw =
                        src == 0 ? R.vf[ft][k] : (src == 1 ? bcv(ft, k, bc) : (src == 2 ? R.vq : R.vI));
                    const uint32_t araw = R.vf[fs][k];
                    const double acc = F(R.vacc[k]);
                    switch (opk) {
                        case 0:
                            res[k] = Fb(a + b);
                            break;
                        case 1:
                            res[k] = Fb(a - b);
                            break;
                        case 2:
                            res[k] = Fb(a * b);
                            break;
                        case 3:
                            res[k] = (a > b) ? araw : braw;
                            break;
                        case 4:
                            res[k] = (a < b) ? araw : braw;
                            break;
                        case 5:
                            res[k] = Fb(acc + F(Fb(a * b)));
                            break;
                        default:
                            res[k] = Fb(acc - F(Fb(a * b)));
                            break;
                    }
                }
                for (uint32_t k = 0; k < 4; ++k) {
                    if (!lane(k))
                        continue;
                    if (toAcc)
                        R.vacc[k] = res[k];
                    else if (dst)
                        R.vf[dst][k] = res[k];
                    else if (dst == 0 && false) {
                    }
                }
            };
            const uint32_t f6 = fn;
            if (f6 < 0x3C) {
                auto dstOk = [&](uint32_t d) { return d; };
                (void)dstOk;
                if (f6 <= 0x03) {
                    doVec(0, 1, f6 & 3, false, fd);
                    return true;
                }
                if (f6 <= 0x07) {
                    doVec(1, 1, f6 & 3, false, fd);
                    return true;
                }
                if (f6 <= 0x0B) {
                    doVec(5, 1, f6 & 3, false, fd);
                    return true;
                }
                if (f6 <= 0x0F) {
                    doVec(6, 1, f6 & 3, false, fd);
                    return true;
                }
                if (f6 <= 0x13) {
                    doVec(3, 1, f6 & 3, false, fd);
                    return true;
                }
                if (f6 <= 0x17) {
                    doVec(4, 1, f6 & 3, false, fd);
                    return true;
                }
                if (f6 <= 0x1B) {
                    doVec(2, 1, f6 & 3, false, fd);
                    return true;
                }
                switch (f6) {
                    case 0x1C:
                        doVec(2, 2, 0, false, fd);
                        return true; // MULq
                    case 0x1D:
                        doVec(3, 3, 0, false, fd);
                        return true; // MAXi
                    case 0x1E:
                        doVec(2, 3, 0, false, fd);
                        return true; // MULi
                    case 0x1F:
                        doVec(4, 3, 0, false, fd);
                        return true; // MINIi
                    case 0x20:
                        doVec(0, 2, 0, false, fd);
                        return true; // ADDq
                    case 0x21:
                        doVec(5, 2, 0, false, fd);
                        return true; // MADDq
                    case 0x22:
                        doVec(0, 3, 0, false, fd);
                        return true; // ADDi
                    case 0x23:
                        doVec(5, 3, 0, false, fd);
                        return true; // MADDi
                    case 0x24:
                        doVec(1, 2, 0, false, fd);
                        return true; // SUBq
                    case 0x25:
                        doVec(6, 2, 0, false, fd);
                        return true; // MSUBq
                    case 0x26:
                        doVec(1, 3, 0, false, fd);
                        return true; // SUBi
                    case 0x27:
                        doVec(6, 3, 0, false, fd);
                        return true; // MSUBi
                    case 0x28:
                        doVec(0, 0, 0, false, fd);
                        return true; // ADD
                    case 0x29:
                        doVec(5, 0, 0, false, fd);
                        return true; // MADD
                    case 0x2A:
                        doVec(2, 0, 0, false, fd);
                        return true; // MUL
                    case 0x2B:
                        doVec(3, 0, 0, false, fd);
                        return true; // MAX
                    case 0x2C:
                        doVec(1, 0, 0, false, fd);
                        return true; // SUB
                    case 0x2D:
                        doVec(6, 0, 0, false, fd);
                        return true; // MSUB
                    case 0x2F:
                        doVec(4, 0, 0, false, fd);
                        return true; // MINI
                    case 0x2E:       // OPMSUB: xyz only
                    {
                        const uint32_t r0 = Fb(F(R.vacc[0]) - F(Fb(F(R.vf[fs][1]) * F(R.vf[ft][2]))));
                        const uint32_t r1 = Fb(F(R.vacc[1]) - F(Fb(F(R.vf[fs][2]) * F(R.vf[ft][0]))));
                        const uint32_t r2 = Fb(F(R.vacc[2]) - F(Fb(F(R.vf[fs][0]) * F(R.vf[ft][1]))));
                        if (fd) {
                            R.vf[fd][0] = r0;
                            R.vf[fd][1] = r1;
                            R.vf[fd][2] = r2;
                        } // the dest field is not used: always xyz
                        return true;
                    }
                    case 0x30:
                        if (fd)
                            R.vi[fd] = static_cast<uint16_t>(R.vi[fs] + R.vi[ft]);
                        return true; // IADD
                    case 0x31:
                        if (fd)
                            R.vi[fd] = static_cast<uint16_t>(R.vi[fs] - R.vi[ft]);
                        return true; // ISUB
                    case 0x32: {
                        const int imm = (fd & 0x10) ? static_cast<int>(fd) - 32 : static_cast<int>(fd);
                        if (ft)
                            R.vi[ft] = static_cast<uint16_t>(R.vi[fs] + imm);
                        return true;
                    } // IADDI
                    case 0x34:
                        if (fd)
                            R.vi[fd] = R.vi[fs] & R.vi[ft];
                        return true; // IAND
                    case 0x35:
                        if (fd)
                            R.vi[fd] = R.vi[fs] | R.vi[ft];
                        return true; // IOR
                    default:
                        skip = true;
                        return false;
                }
            }
            // Special 2: op = (fd << 2) | (funct & 3)
            const uint32_t op2 = (fd << 2) | (f6 & 3);
            if (op2 <= 0x03) {
                doVec(0, 1, op2 & 3, true, 0);
                return true;
            }
            if (op2 <= 0x07) {
                doVec(1, 1, op2 & 3, true, 0);
                return true;
            }
            if (op2 <= 0x0B) {
                doVec(5, 1, op2 & 3, true, 0);
                return true;
            }
            if (op2 <= 0x0F) {
                doVec(6, 1, op2 & 3, true, 0);
                return true;
            }
            if (op2 >= 0x18 && op2 <= 0x1B) {
                doVec(2, 1, op2 & 3, true, 0);
                return true;
            }
            switch (op2) {
                case 0x10:
                case 0x11:
                case 0x12:
                case 0x13: // ITOFn : vf[ft] = fs / 2^n
                {
                    static const int sh[4] = {0, 4, 12, 15};
                    for (uint32_t k = 0; k < 4; ++k)
                        if (lane(k) && ft)
                            R.vf[ft][k] = Fb(static_cast<double>(static_cast<int32_t>(R.vf[fs][k])) /
                                             static_cast<double>(1 << sh[op2 & 3]));
                    return true;
                }
                case 0x14:
                case 0x15:
                case 0x16:
                case 0x17: // FTOIn : vf[ft] = trunc(fs * 2^n)
                {
                    static const int sh[4] = {0, 4, 12, 15};
                    for (uint32_t k = 0; k < 4; ++k)
                        if (lane(k) && ft) {
                            const double v = std::trunc(F(R.vf[fs][k]) * static_cast<double>(1 << sh[op2 & 3]));
                            int32_t o = v >= 2147483647.0 ? INT32_MAX
                                                          : (v <= -2147483648.0 ? INT32_MIN : static_cast<int32_t>(v));
                            R.vf[ft][k] = static_cast<uint32_t>(o);
                        }
                    return true;
                }
                case 0x1C:
                    doVec(2, 2, 0, true, 0);
                    return true; // MULAq
                case 0x1E:
                    doVec(2, 3, 0, true, 0);
                    return true; // MULAi
                case 0x1D:
                    for (uint32_t k = 0; k < 4; ++k)
                        if (lane(k) && ft)
                            R.vf[ft][k] = R.vf[fs][k] & 0x7FFFFFFFu;
                    return true; // ABS
                case 0x20:
                    doVec(0, 2, 0, true, 0);
                    return true; // ADDAq
                case 0x21:
                    doVec(5, 2, 0, true, 0);
                    return true; // MADDAq
                case 0x22:
                    doVec(0, 3, 0, true, 0);
                    return true; // ADDAi
                case 0x23:
                    doVec(5, 3, 0, true, 0);
                    return true; // MADDAi
                case 0x24:
                    doVec(1, 2, 0, true, 0);
                    return true; // SUBAq
                case 0x25:
                    doVec(6, 2, 0, true, 0);
                    return true; // MSUBAq
                case 0x26:
                    doVec(1, 3, 0, true, 0);
                    return true; // SUBAi
                case 0x27:
                    doVec(6, 3, 0, true, 0);
                    return true; // MSUBAi
                case 0x28:
                    doVec(0, 0, 0, true, 0);
                    return true; // ADDA
                case 0x29:
                    doVec(5, 0, 0, true, 0);
                    return true; // MADDA
                case 0x2A:
                    doVec(2, 0, 0, true, 0);
                    return true; // MULA
                case 0x2C:
                    doVec(1, 0, 0, true, 0);
                    return true; // SUBA
                case 0x2D:
                    doVec(6, 0, 0, true, 0);
                    return true; // MSUBA
                case 0x2E:       // OPMULA
                    R.vacc[0] = Fb(F(R.vf[fs][1]) * F(R.vf[ft][2]));
                    R.vacc[1] = Fb(F(R.vf[fs][2]) * F(R.vf[ft][0]));
                    R.vacc[2] = Fb(F(R.vf[fs][0]) * F(R.vf[ft][1]));
                    return true;
                case 0x2F:
                    return true; // NOP
                case 0x38:
                case 0x39:
                case 0x3A: // VDIV / VSQRT / VRSQRT
                {
                    const uint32_t fsf = (w >> 21) & 3, ftf = (w >> 23) & 3;
                    const uint32_t A = R.vf[fs][fsf], B = R.vf[ft][ftf];
                    auto clampMax = [](uint32_t b) -> uint32_t {
                        return ((b & 0x7F800000u) == 0x7F800000u) ? ((b & 0x80000000u) | 0x7F7FFFFFu) : b;
                    };
                    if (op2 == 0x38)
                        R.vq = ((B >> 23) & 255) == 0 ? (((A ^ B) & 0x80000000u) | 0x7F7FFFFFu)
                                                      : clampMax(d2ps(F(A) / F(B)));
                    else if (op2 == 0x39)
                        R.vq = ((B >> 23) & 255) == 0 ? 0u : clampMax(d2ps(std::sqrt(std::fabs(F(B)))));
                    else
                        R.vq = ((B >> 23) & 255) == 0 ? ((A & 0x80000000u) | 0x7F7FFFFFu)
                                                      : clampMax(d2ps(F(A) / std::sqrt(std::fabs(F(B)))));
                    return true;
                }
                case 0x3B:
                    return true; // VWAITQ
                case 0x30:
                    for (uint32_t k = 0; k < 4; ++k)
                        if (lane(k) && ft)
                            R.vf[ft][k] = R.vf[fs][k];
                    return true; // MOVE
                case 0x31: {
                    uint32_t t[4];
                    for (int k = 0; k < 4; ++k)
                        t[k] = R.vf[fs][(k + 1) & 3];
                    for (uint32_t k = 0; k < 4; ++k)
                        if (lane(k) && ft)
                            R.vf[ft][k] = t[k];
                    return true;
                } // MR32 (fs may equal ft)
                case 0x3C: {
                    const uint32_t fsf = (w >> 21) & 3;
                    if (ft)
                        R.vi[ft] = static_cast<uint16_t>(R.vf[fs][fsf]);
                    return true;
                } // MTIR (low 16 bits of the float bits)
                case 0x3D: // MFIR: vf[ft].dest = sign extended vi[fs]
                    for (uint32_t k = 0; k < 4; ++k)
                        if (lane(k) && ft)
                            R.vf[ft][k] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(R.vi[fs])));
                    return true;
                default:
                    skip = true;
                    return false; // VDIV / VSQRT / VRSQRT / VCLIP / load-store through VI / R register ops: not
                                  // modelled
            }
        }
        // ---- COP1
        case 0x11: {
            const uint32_t fmt = rs, ft = rt, fs = rd, fd = sa;
            if (fmt == 0x00) {
                setd(rt, sx32(R.f[fs]));
                return true;
            } // MFC1
            if (fmt == 0x04) {
                R.f[fs] = static_cast<uint32_t>(U(rt));
                return true;
            } // MTC1
            if (fmt == 0x02) {
                if (fs == 31)
                    setd(rt, sx32(R.fcr31));
                else if (fs == 0)
                    setd(rt, 0x2E00);
                else
                    skip = true;
                return !skip;
            } // CFC1
            if (fmt == 0x06) {
                if (fs == 31)
                    R.fcr31 = static_cast<uint32_t>(U(rt)) & 0x0183FFFFu;
                else
                    skip = true;
                return !skip;
            } // CTC1
            // W
            if (fmt == 0x14) {
                if (fn == 0x20) {
                    R.f[fd] = d2ps(static_cast<double>(static_cast<int32_t>(R.f[fs])));
                    return true;
                } // CVT.S.W
                return false;
            }
            if (fmt != 0x10)
                return false;
            const uint32_t A = R.f[fs], B = R.f[ft];
            switch (fn) {
                case 0x00:
                    R.f[fd] = d2ps(ps2d(A) + ps2d(B));
                    return true;
                case 0x01:
                    R.f[fd] = d2ps(ps2d(A) - ps2d(B));
                    return true;
                case 0x02:
                    R.f[fd] = d2ps(ps2d(A) * ps2d(B));
                    return true;
                case 0x03:
                    if (((B >> 23) & 255) == 0)
                        R.f[fd] = ((A ^ B) & 0x80000000u) | 0x7FFFFFFFu;
                    else
                        R.f[fd] = d2ps(ps2d(A) / ps2d(B));
                    return true;
                case 0x04:
                    R.f[fd] = (((B >> 23) & 255) == 0) ? 0u : d2ps(std::sqrt(std::fabs(ps2d(B))));
                    return true;
                case 0x05:
                    R.f[fd] = A & 0x7FFFFFFFu;
                    return true;
                case 0x06:
                    R.f[fd] = A;
                    return true;
                case 0x07:
                    R.f[fd] = A ^ 0x80000000u;
                    return true;
                case 0x16:
                    if (((B >> 23) & 255) == 0)
                        R.f[fd] = (A & 0x80000000u) | 0x7FFFFFFFu;
                    else
                        R.f[fd] = d2ps(ps2d(A) / std::sqrt(std::fabs(ps2d(B))));
                    return true;
                case 0x18:
                    R.acc = d2ps(ps2d(A) + ps2d(B));
                    return true;
                case 0x19:
                    R.acc = d2ps(ps2d(A) - ps2d(B));
                    return true;
                case 0x1A:
                    R.acc = d2ps(ps2d(A) * ps2d(B));
                    return true;
                case 0x1C:
                    R.f[fd] = d2ps(ps2d(R.acc) + ps2d(d2ps(ps2d(A) * ps2d(B))));
                    return true;
                case 0x1D:
                    R.f[fd] = d2ps(ps2d(R.acc) - ps2d(d2ps(ps2d(A) * ps2d(B))));
                    return true;
                case 0x1E:
                    R.acc = d2ps(ps2d(R.acc) + ps2d(d2ps(ps2d(A) * ps2d(B))));
                    return true;
                case 0x1F:
                    R.acc = d2ps(ps2d(R.acc) - ps2d(d2ps(ps2d(A) * ps2d(B))));
                    return true;
                case 0x24: // CVT.W.S
                {
                    if ((A & 0x7F800000u) <= 0x4E800000u)
                        R.f[fd] = static_cast<uint32_t>(static_cast<int32_t>(ps2d(A)));
                    else
                        R.f[fd] = (A & 0x80000000u) ? 0x80000000u : 0x7FFFFFFFu;
                    return true;
                }
                case 0x28: {
                    const double a = ps2d(A), b = ps2d(B);
                    if (a == b) {
                        skip = (A != B);
                    }
                    R.f[fd] = a > b ? A : B;
                    return true;
                } // MAX (ties with different bits: ambiguous, see caller)
                case 0x29: {
                    const double a = ps2d(A), b = ps2d(B);
                    if (a == b) {
                        skip = (A != B);
                    }
                    R.f[fd] = a < b ? A : B;
                    return true;
                } // MIN
                case 0x30:
                    R.fcr31 &= ~0x00800000u;
                    return true;
                case 0x32:
                    if (ps2d(A) == ps2d(B))
                        R.fcr31 |= 0x00800000u;
                    else
                        R.fcr31 &= ~0x00800000u;
                    return true;
                case 0x34:
                    if (ps2d(A) < ps2d(B))
                        R.fcr31 |= 0x00800000u;
                    else
                        R.fcr31 &= ~0x00800000u;
                    return true;
                case 0x36:
                    if (ps2d(A) <= ps2d(B))
                        R.fcr31 |= 0x00800000u;
                    else
                        R.fcr31 &= ~0x00800000u;
                    return true;
                default:
                    return false;
            }
        }
        // ---- MMI
        case 0x1C: {
            auto LO = [&](int i) -> uint32_t {
                return i < 2 ? reinterpret_cast<uint32_t*>(&R.lo)[i] : reinterpret_cast<uint32_t*>(&R.lo1)[i - 2];
            };
            auto HI = [&](int i) -> uint32_t {
                return i < 2 ? reinterpret_cast<uint32_t*>(&R.hi)[i] : reinterpret_cast<uint32_t*>(&R.hi1)[i - 2];
            };
            auto setLO = [&](int i, uint32_t v) {
                if (i < 2)
                    reinterpret_cast<uint32_t*>(&R.lo)[i] = v;
                else
                    reinterpret_cast<uint32_t*>(&R.lo1)[i - 2] = v;
            };
            auto setHI = [&](int i, uint32_t v) {
                if (i < 2)
                    reinterpret_cast<uint32_t*>(&R.hi)[i] = v;
                else
                    reinterpret_cast<uint32_t*>(&R.hi1)[i - 2] = v;
            };
            U128 A = Q(rs), B = Q(rt), D = Q(rd);
            auto done = [&](const U128& v) {
                setq(rd, v);
                return true;
            };
            switch (fn) {
                case 0x00:
                case 0x20: // MADD / MADD1
                {
                    const bool p1 = fn == 0x20;
                    uint64_t &hh = p1 ? R.hi1 : R.hi, &ll = p1 ? R.lo1 : R.lo;
                    const uint64_t acc =
                        (static_cast<uint64_t>(static_cast<uint32_t>(hh)) << 32) | static_cast<uint32_t>(ll);
                    const uint64_t t = acc + static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(A.w(0))) *
                                                                   static_cast<int32_t>(B.w(0)));
                    ll = sx32(static_cast<uint32_t>(t));
                    hh = sx32(static_cast<uint32_t>(t >> 32));
                    setd(rd, ll);
                    return true;
                }
                case 0x01:
                case 0x21: // MADDU / MADDU1
                {
                    const bool p1 = fn == 0x21;
                    uint64_t &hh = p1 ? R.hi1 : R.hi, &ll = p1 ? R.lo1 : R.lo;
                    const uint64_t acc =
                        (static_cast<uint64_t>(static_cast<uint32_t>(hh)) << 32) | static_cast<uint32_t>(ll);
                    const uint64_t t = acc + static_cast<uint64_t>(A.w(0)) * B.w(0);
                    ll = sx32(static_cast<uint32_t>(t));
                    hh = sx32(static_cast<uint32_t>(t >> 32));
                    setd(rd, ll);
                    return true;
                }
                case 0x04: // PLZCW
                {
                    U128 v = D;
                    for (int i = 0; i < 2; ++i) {
                        uint32_t x = A.w(i);
                        const uint32_t sign = x >> 31;
                        int n = 0;
                        for (int b = 30; b >= 0 && ((x >> b) & 1) == sign; --b)
                            ++n;
                        v.w(i) = static_cast<uint32_t>(n);
                    }
                    return done(v);
                }
                case 0x10:
                    setd(rd, R.hi1);
                    return true;
                case 0x11:
                    R.hi1 = U(rs);
                    return true;
                case 0x12:
                    setd(rd, R.lo1);
                    return true;
                case 0x13:
                    R.lo1 = U(rs);
                    return true;
                case 0x18: {
                    const int64_t p = static_cast<int64_t>(static_cast<int32_t>(U(rs))) * static_cast<int32_t>(U(rt));
                    R.lo1 = sx32(static_cast<uint32_t>(p));
                    R.hi1 = sx32(static_cast<uint32_t>(p >> 32));
                    setd(rd, R.lo1);
                    return true;
                }
                case 0x19: {
                    const uint64_t p =
                        static_cast<uint64_t>(static_cast<uint32_t>(U(rs))) * static_cast<uint32_t>(U(rt));
                    R.lo1 = sx32(static_cast<uint32_t>(p));
                    R.hi1 = sx32(static_cast<uint32_t>(p >> 32));
                    setd(rd, R.lo1);
                    return true;
                }
                case 0x1A: {
                    const int32_t a = static_cast<int32_t>(U(rs)), b = static_cast<int32_t>(U(rt));
                    if (b == 0) {
                        R.lo1 = a < 0 ? 1 : ~0ull;
                        R.hi1 = sx32(static_cast<uint32_t>(a));
                    } else if (a == INT32_MIN && b == -1) {
                        R.lo1 = sx32(0x80000000u);
                        R.hi1 = 0;
                    } else {
                        R.lo1 = sx32(static_cast<uint32_t>(a / b));
                        R.hi1 = sx32(static_cast<uint32_t>(a % b));
                    }
                    return true;
                }
                case 0x1B: {
                    const uint32_t a = static_cast<uint32_t>(U(rs)), b = static_cast<uint32_t>(U(rt));
                    if (b == 0) {
                        R.lo1 = ~0ull;
                        R.hi1 = sx32(a);
                    } else {
                        R.lo1 = sx32(a / b);
                        R.hi1 = sx32(a % b);
                    }
                    return true;
                }
                case 0x30: // PMFHL
                {
                    U128 v = D;
                    switch (sa) {
                        case 0:
                            v.w(0) = LO(0);
                            v.w(1) = HI(0);
                            v.w(2) = LO(2);
                            v.w(3) = HI(2);
                            break;
                        case 1:
                            v.w(0) = LO(1);
                            v.w(1) = HI(1);
                            v.w(2) = LO(3);
                            v.w(3) = HI(3);
                            break;
                        case 2:
                            for (int k = 0; k < 2; ++k) {
                                const int64_t t =
                                    static_cast<int64_t>((static_cast<uint64_t>(HI(2 * k)) << 32) | LO(2 * k));
                                uint64_t o;
                                if (t >= 0x7FFFFFFFll)
                                    o = 0x7FFFFFFFull;
                                else if (t <= -0x80000000ll)
                                    o = 0xFFFFFFFF80000000ull;
                                else
                                    o = sx32(LO(2 * k));
                                v.d[k] = o;
                            }
                            break;
                        case 3:
                            v.h(0) = static_cast<uint16_t>(LO(0));
                            v.h(1) = static_cast<uint16_t>(LO(1));
                            v.h(2) = static_cast<uint16_t>(HI(0));
                            v.h(3) = static_cast<uint16_t>(HI(1));
                            v.h(4) = static_cast<uint16_t>(LO(2));
                            v.h(5) = static_cast<uint16_t>(LO(3));
                            v.h(6) = static_cast<uint16_t>(HI(2));
                            v.h(7) = static_cast<uint16_t>(HI(3));
                            break;
                        case 4: {
                            auto sat = [](uint32_t x) -> uint16_t {
                                const int32_t s = static_cast<int32_t>(x);
                                return s > 0x7FFF ? 0x7FFF : (s < -0x8000 ? 0x8000 : static_cast<uint16_t>(s));
                            };
                            v.h(0) = sat(LO(0));
                            v.h(1) = sat(LO(1));
                            v.h(2) = sat(HI(0));
                            v.h(3) = sat(HI(1));
                            v.h(4) = sat(LO(2));
                            v.h(5) = sat(LO(3));
                            v.h(6) = sat(HI(2));
                            v.h(7) = sat(HI(3));
                            break;
                        }
                        default:
                            skip = true;
                            return false;
                    }
                    return done(v);
                }
                case 0x31: // PMTHL (LW only)
                    if (sa != 0) {
                        skip = true;
                        return false;
                    }
                    setLO(0, A.w(0));
                    setHI(0, A.w(1));
                    setLO(2, A.w(2));
                    setHI(2, A.w(3));
                    return true;
                case 0x34: {
                    U128 v = D;
                    for (int i = 0; i < 8; ++i)
                        v.h(i) = static_cast<uint16_t>(B.h(i) << (sa & 15));
                    return done(v);
                }
                case 0x36: {
                    U128 v = D;
                    for (int i = 0; i < 8; ++i)
                        v.h(i) = static_cast<uint16_t>(B.h(i) >> (sa & 15));
                    return done(v);
                }
                case 0x37: {
                    U128 v = D;
                    for (int i = 0; i < 8; ++i)
                        v.sh(i) = static_cast<int16_t>(B.sh(i) >> (sa & 15));
                    return done(v);
                }
                case 0x3C: {
                    U128 v = D;
                    for (int i = 0; i < 4; ++i)
                        v.w(i) = B.w(i) << sa;
                    return done(v);
                }
                case 0x3E: {
                    U128 v = D;
                    for (int i = 0; i < 4; ++i)
                        v.w(i) = B.w(i) >> sa;
                    return done(v);
                }
                case 0x3F: {
                    U128 v = D;
                    for (int i = 0; i < 4; ++i)
                        v.sw(i) = B.sw(i) >> sa;
                    return done(v);
                }
                case 0x08: // MMI0
                {
                    U128 v = D;
                    switch (sa) {
                        case 0x00:
                            for (int i = 0; i < 4; ++i)
                                v.w(i) = A.w(i) + B.w(i);
                            break;
                        case 0x01:
                            for (int i = 0; i < 4; ++i)
                                v.w(i) = A.w(i) - B.w(i);
                            break;
                        case 0x02:
                            for (int i = 0; i < 4; ++i)
                                v.w(i) = A.sw(i) > B.sw(i) ? ~0u : 0u;
                            break;
                        case 0x03:
                            for (int i = 0; i < 4; ++i)
                                v.sw(i) = std::max(A.sw(i), B.sw(i));
                            break;
                        case 0x04:
                            for (int i = 0; i < 8; ++i)
                                v.h(i) = A.h(i) + B.h(i);
                            break;
                        case 0x05:
                            for (int i = 0; i < 8; ++i)
                                v.h(i) = A.h(i) - B.h(i);
                            break;
                        case 0x06:
                            for (int i = 0; i < 8; ++i)
                                v.h(i) = A.sh(i) > B.sh(i) ? 0xFFFF : 0;
                            break;
                        case 0x07:
                            for (int i = 0; i < 8; ++i)
                                v.sh(i) = std::max(A.sh(i), B.sh(i));
                            break;
                        case 0x08:
                            for (int i = 0; i < 16; ++i)
                                v.b(i) = A.b(i) + B.b(i);
                            break;
                        case 0x09:
                            for (int i = 0; i < 16; ++i)
                                v.b(i) = A.b(i) - B.b(i);
                            break;
                        case 0x0A:
                            for (int i = 0; i < 16; ++i)
                                v.b(i) = A.sb(i) > B.sb(i) ? 0xFF : 0;
                            break;
                        case 0x10:
                            for (int i = 0; i < 4; ++i) {
                                const int64_t t = static_cast<int64_t>(A.sw(i)) + B.sw(i);
                                v.sw(i) =
                                    t > INT32_MAX ? INT32_MAX : (t < INT32_MIN ? INT32_MIN : static_cast<int32_t>(t));
                            }
                            break;
                        case 0x11:
                            for (int i = 0; i < 4; ++i) {
                                const int64_t t = static_cast<int64_t>(A.sw(i)) - B.sw(i);
                                v.sw(i) =
                                    t > INT32_MAX ? INT32_MAX : (t < INT32_MIN ? INT32_MIN : static_cast<int32_t>(t));
                            }
                            break;
                        case 0x12:
                            v.w(0) = B.w(0);
                            v.w(1) = A.w(0);
                            v.w(2) = B.w(1);
                            v.w(3) = A.w(1);
                            break;
                        case 0x13:
                            v.w(0) = B.w(0);
                            v.w(1) = B.w(2);
                            v.w(2) = A.w(0);
                            v.w(3) = A.w(2);
                            break;
                        case 0x14:
                            for (int i = 0; i < 8; ++i) {
                                const int32_t t = static_cast<int32_t>(A.sh(i)) + B.sh(i);
                                v.sh(i) = t > 32767 ? 32767 : (t < -32768 ? -32768 : static_cast<int16_t>(t));
                            }
                            break;
                        case 0x15:
                            for (int i = 0; i < 8; ++i) {
                                const int32_t t = static_cast<int32_t>(A.sh(i)) - B.sh(i);
                                v.sh(i) = t > 32767 ? 32767 : (t < -32768 ? -32768 : static_cast<int16_t>(t));
                            }
                            break;
                        case 0x16:
                            for (int i = 0; i < 4; ++i) {
                                v.h(2 * i) = B.h(i);
                                v.h(2 * i + 1) = A.h(i);
                            }
                            break;
                        case 0x17:
                            for (int i = 0; i < 4; ++i) {
                                v.h(i) = B.h(2 * i);
                                v.h(4 + i) = A.h(2 * i);
                            }
                            break;
                        case 0x18:
                            for (int i = 0; i < 16; ++i) {
                                const int32_t t = static_cast<int32_t>(A.sb(i)) + B.sb(i);
                                v.sb(i) = t > 127 ? 127 : (t < -128 ? -128 : static_cast<int8_t>(t));
                            }
                            break;
                        case 0x19:
                            for (int i = 0; i < 16; ++i) {
                                const int32_t t = static_cast<int32_t>(A.sb(i)) - B.sb(i);
                                v.sb(i) = t > 127 ? 127 : (t < -128 ? -128 : static_cast<int8_t>(t));
                            }
                            break;
                        case 0x1A:
                            for (int i = 0; i < 8; ++i) {
                                v.b(2 * i) = B.b(i);
                                v.b(2 * i + 1) = A.b(i);
                            }
                            break;
                        case 0x1B:
                            for (int i = 0; i < 8; ++i) {
                                v.b(i) = B.b(2 * i);
                                v.b(8 + i) = A.b(2 * i);
                            }
                            break;
                        case 0x1E:
                            for (int i = 0; i < 4; ++i) {
                                const uint32_t x = B.w(i);
                                v.w(i) = ((x & 0x1F) << 3) | (((x >> 5) & 0x1F) << 11) | (((x >> 10) & 0x1F) << 19) |
                                         (((x >> 15) & 1) << 31);
                            }
                            break;
                        case 0x1F:
                            for (int i = 0; i < 4; ++i) {
                                const uint32_t x = B.w(i);
                                v.w(i) = ((x >> 3) & 0x1F) | (((x >> 11) & 0x1F) << 5) | (((x >> 19) & 0x1F) << 10) |
                                         (((x >> 31) & 1) << 15);
                            }
                            break;
                        default:
                            skip = true;
                            return false;
                    }
                    return done(v);
                }
                case 0x28: // MMI1
                {
                    U128 v = D;
                    switch (sa) {
                        case 0x01:
                            for (int i = 0; i < 4; ++i)
                                v.w(i) = (B.w(i) == 0x80000000u)
                                             ? 0x7FFFFFFFu
                                             : static_cast<uint32_t>(B.sw(i) < 0 ? -B.sw(i) : B.sw(i));
                            break;
                        case 0x02:
                            for (int i = 0; i < 4; ++i)
                                v.w(i) = A.w(i) == B.w(i) ? ~0u : 0u;
                            break;
                        case 0x03:
                            for (int i = 0; i < 4; ++i)
                                v.sw(i) = std::min(A.sw(i), B.sw(i));
                            break;
                        case 0x04:
                            for (int i = 0; i < 4; ++i) {
                                v.h(i) = A.h(i) - B.h(i);
                                v.h(4 + i) = A.h(4 + i) + B.h(4 + i);
                            }
                            break;
                        case 0x05:
                            for (int i = 0; i < 8; ++i)
                                v.h(i) = (B.h(i) == 0x8000) ? 0x7FFF
                                                            : static_cast<uint16_t>(B.sh(i) < 0 ? -B.sh(i) : B.sh(i));
                            break;
                        case 0x06:
                            for (int i = 0; i < 8; ++i)
                                v.h(i) = A.h(i) == B.h(i) ? 0xFFFF : 0;
                            break;
                        case 0x07:
                            for (int i = 0; i < 8; ++i)
                                v.sh(i) = std::min(A.sh(i), B.sh(i));
                            break;
                        case 0x0A:
                            for (int i = 0; i < 16; ++i)
                                v.b(i) = A.b(i) == B.b(i) ? 0xFF : 0;
                            break;
                        case 0x10:
                            for (int i = 0; i < 4; ++i) {
                                const uint64_t t = static_cast<uint64_t>(A.w(i)) + B.w(i);
                                v.w(i) = t > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(t);
                            }
                            break;
                        case 0x11:
                            for (int i = 0; i < 4; ++i)
                                v.w(i) = A.w(i) < B.w(i) ? 0u : A.w(i) - B.w(i);
                            break;
                        case 0x12:
                            v.w(0) = B.w(2);
                            v.w(1) = A.w(2);
                            v.w(2) = B.w(3);
                            v.w(3) = A.w(3);
                            break;
                        case 0x14:
                            for (int i = 0; i < 8; ++i) {
                                const uint32_t t = static_cast<uint32_t>(A.h(i)) + B.h(i);
                                v.h(i) = t > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(t);
                            }
                            break;
                        case 0x15:
                            for (int i = 0; i < 8; ++i)
                                v.h(i) = A.h(i) < B.h(i) ? 0 : static_cast<uint16_t>(A.h(i) - B.h(i));
                            break;
                        case 0x16:
                            for (int i = 0; i < 4; ++i) {
                                v.h(2 * i) = B.h(4 + i);
                                v.h(2 * i + 1) = A.h(4 + i);
                            }
                            break;
                        case 0x18:
                            for (int i = 0; i < 16; ++i) {
                                const uint32_t t = static_cast<uint32_t>(A.b(i)) + B.b(i);
                                v.b(i) = t > 255 ? 255 : static_cast<uint8_t>(t);
                            }
                            break;
                        case 0x19:
                            for (int i = 0; i < 16; ++i)
                                v.b(i) = A.b(i) < B.b(i) ? 0 : static_cast<uint8_t>(A.b(i) - B.b(i));
                            break;
                        case 0x1A:
                            for (int i = 0; i < 8; ++i) {
                                v.b(2 * i) = B.b(8 + i);
                                v.b(2 * i + 1) = A.b(8 + i);
                            }
                            break;
                        case 0x1B: // QFSRV: (rs:rt) >> SA
                        {
                            const uint32_t s = R.sa & 0x7F;
                            U128 out{};
                            for (uint32_t bit = 0; bit < 128; ++bit) {
                                const uint32_t src =
                                    bit + s; // bit index in the 256 bit value rt (0..127) : rs (128..255)
                                uint32_t val = 0;
                                if (src < 128)
                                    val = (B.d[src / 64] >> (src % 64)) & 1;
                                else if (src < 256)
                                    val = (A.d[(src - 128) / 64] >> ((src - 128) % 64)) & 1;
                                out.d[bit / 64] |= static_cast<uint64_t>(val) << (bit % 64);
                            }
                            v = out;
                            break;
                        }
                        default:
                            skip = true;
                            return false;
                    }
                    return done(v);
                }
                case 0x09: // MMI2
                {
                    U128 v = D;
                    switch (sa) {
                        case 0x00:
                        case 0x04:
                        case 0x0C: // PMADDW / PMSUBW / PMULTW
                        {
                            for (int k = 0; k < 2; ++k) {
                                const int li = 2 * k;
                                uint64_t t = static_cast<uint64_t>(static_cast<int64_t>(A.sw(li)) * B.sw(li));
                                if (sa != 0x0C) {
                                    const uint64_t acc = (static_cast<uint64_t>(HI(li)) << 32) | LO(li);
                                    t = sa == 0x00 ? acc + t : acc - t;
                                }
                                uint64_t &ll = k ? R.lo1 : R.lo, &hh = k ? R.hi1 : R.hi;
                                ll = sx32(static_cast<uint32_t>(t));
                                hh = sx32(static_cast<uint32_t>(t >> 32));
                                v.d[k] = static_cast<uint64_t>(t);
                            }
                            if (rd == 0)
                                return true;
                            return done(v);
                        }
                        case 0x02:
                            for (int k = 0; k < 2; ++k)
                                v.d[k] = sx32(B.w(2 * k) << (A.w(2 * k) & 31));
                            break;
                        case 0x03:
                            for (int k = 0; k < 2; ++k)
                                v.d[k] = sx32(B.w(2 * k) >> (A.w(2 * k) & 31));
                            break;
                        case 0x08:
                            v.d[0] = R.hi;
                            v.d[1] = R.hi1;
                            break;
                        case 0x09:
                            v.d[0] = R.lo;
                            v.d[1] = R.lo1;
                            break;
                        case 0x0A:
                            v.h(0) = B.h(0);
                            v.h(1) = A.h(4);
                            v.h(2) = B.h(1);
                            v.h(3) = A.h(5);
                            v.h(4) = B.h(2);
                            v.h(5) = A.h(6);
                            v.h(6) = B.h(3);
                            v.h(7) = A.h(7);
                            break;
                        case 0x0D: // PDIVW
                            for (int k = 0; k < 2; ++k) {
                                const int32_t a = A.sw(2 * k), b = B.sw(2 * k);
                                uint64_t &ll = k ? R.lo1 : R.lo, &hh = k ? R.hi1 : R.hi;
                                if (b == 0) {
                                    ll = a < 0 ? 1 : ~0ull;
                                    hh = sx32(static_cast<uint32_t>(a));
                                } else if (a == INT32_MIN && b == -1) {
                                    ll = sx32(0x80000000u);
                                    hh = 0;
                                } else {
                                    ll = sx32(static_cast<uint32_t>(a / b));
                                    hh = sx32(static_cast<uint32_t>(a % b));
                                }
                            }
                            return true;
                        case 0x0E:
                            v.d[0] = B.d[0];
                            v.d[1] = A.d[0];
                            break;
                        case 0x12:
                            v.d[0] = A.d[0] & B.d[0];
                            v.d[1] = A.d[1] & B.d[1];
                            break;
                        case 0x13:
                            v.d[0] = A.d[0] ^ B.d[0];
                            v.d[1] = A.d[1] ^ B.d[1];
                            break;
                        case 0x1A:
                            v.h(0) = B.h(2);
                            v.h(1) = B.h(1);
                            v.h(2) = B.h(0);
                            v.h(3) = B.h(3);
                            v.h(4) = B.h(6);
                            v.h(5) = B.h(5);
                            v.h(6) = B.h(4);
                            v.h(7) = B.h(7);
                            break;
                        case 0x1B:
                            v.h(0) = B.h(3);
                            v.h(1) = B.h(2);
                            v.h(2) = B.h(1);
                            v.h(3) = B.h(0);
                            v.h(4) = B.h(7);
                            v.h(5) = B.h(6);
                            v.h(6) = B.h(5);
                            v.h(7) = B.h(4);
                            break;
                        case 0x1E:
                            v.w(0) = B.w(2);
                            v.w(1) = B.w(1);
                            v.w(2) = B.w(0);
                            v.w(3) = B.w(3);
                            break;
                        case 0x1F:
                            v.w(0) = B.w(1);
                            v.w(1) = B.w(2);
                            v.w(2) = B.w(0);
                            v.w(3) = B.w(3);
                            break;
                        case 0x10:
                        case 0x14:
                        case 0x1C: // PMADDH / PMSUBH / PMULTH
                        {
                            const int mode = sa == 0x1C ? 0 : (sa == 0x10 ? 1 : 2);
                            for (int i = 0; i < 8; ++i) {
                                const int32_t p = static_cast<int32_t>(A.sh(i)) * static_cast<int32_t>(B.sh(i));
                                // order of the destination words: LO0 LO1 HI0 HI1 LO2 LO3 HI2 HI3
                                const bool isHi = (i == 2 || i == 3 || i == 6 || i == 7);
                                const int wi = (i < 4) ? (i & 1) : 2 + (i & 1);
                                const uint32_t cur = isHi ? HI(wi) : LO(wi);
                                const uint32_t nv = mode == 0 ? static_cast<uint32_t>(p)
                                                              : (mode == 1 ? cur + static_cast<uint32_t>(p)
                                                                           : cur - static_cast<uint32_t>(p));
                                if (isHi)
                                    setHI(wi, nv);
                                else
                                    setLO(wi, nv);
                            }
                            v.w(0) = LO(0);
                            v.w(1) = HI(0);
                            v.w(2) = LO(2);
                            v.w(3) = HI(2);
                            if (rd == 0)
                                return true;
                            return done(v);
                        }
                        default:
                            skip = true;
                            return false; // PHMADH / PHMSBH / PDIVBW: reference not trusted
                    }
                    return done(v);
                }
                case 0x29: // MMI3
                {
                    U128 v = D;
                    switch (sa) {
                        case 0x00:
                        case 0x0C: // PMADDUW / PMULTUW
                        {
                            for (int k = 0; k < 2; ++k) {
                                const int li = 2 * k;
                                uint64_t t = static_cast<uint64_t>(A.w(li)) * B.w(li);
                                if (sa == 0x00)
                                    t += (static_cast<uint64_t>(HI(li)) << 32) | LO(li);
                                uint64_t &ll = k ? R.lo1 : R.lo, &hh = k ? R.hi1 : R.hi;
                                ll = sx32(static_cast<uint32_t>(t));
                                hh = sx32(static_cast<uint32_t>(t >> 32));
                                v.d[k] = t;
                            }
                            if (rd == 0)
                                return true;
                            return done(v);
                        }
                        case 0x03:
                            for (int k = 0; k < 2; ++k)
                                v.d[k] = sx32(static_cast<uint32_t>(B.sw(2 * k) >> (A.w(2 * k) & 31)));
                            break;
                        case 0x08:
                            R.hi = A.d[0];
                            R.hi1 = A.d[1];
                            return true;
                        case 0x09:
                            R.lo = A.d[0];
                            R.lo1 = A.d[1];
                            return true;
                        case 0x0A:
                            v.h(0) = B.h(0);
                            v.h(1) = A.h(0);
                            v.h(2) = B.h(2);
                            v.h(3) = A.h(2);
                            v.h(4) = B.h(4);
                            v.h(5) = A.h(4);
                            v.h(6) = B.h(6);
                            v.h(7) = A.h(6);
                            break;
                        case 0x0D: // PDIVUW
                            for (int k = 0; k < 2; ++k) {
                                const uint32_t a = A.w(2 * k), b = B.w(2 * k);
                                uint64_t &ll = k ? R.lo1 : R.lo, &hh = k ? R.hi1 : R.hi;
                                if (b == 0) {
                                    ll = ~0ull;
                                    hh = sx32(a);
                                } else {
                                    ll = sx32(a / b);
                                    hh = sx32(a % b);
                                }
                            }
                            return true;
                        case 0x0E:
                            v.d[0] = A.d[1];
                            v.d[1] = B.d[1];
                            break;
                        case 0x12:
                            v.d[0] = A.d[0] | B.d[0];
                            v.d[1] = A.d[1] | B.d[1];
                            break;
                        case 0x13:
                            v.d[0] = ~(A.d[0] | B.d[0]);
                            v.d[1] = ~(A.d[1] | B.d[1]);
                            break;
                        case 0x1A:
                            v.h(0) = B.h(0);
                            v.h(1) = B.h(2);
                            v.h(2) = B.h(1);
                            v.h(3) = B.h(3);
                            v.h(4) = B.h(4);
                            v.h(5) = B.h(6);
                            v.h(6) = B.h(5);
                            v.h(7) = B.h(7);
                            break;
                        case 0x1B:
                            for (int i = 0; i < 4; ++i) {
                                v.h(i) = B.h(0);
                                v.h(4 + i) = B.h(4);
                            }
                            break;
                        case 0x1E:
                            v.w(0) = B.w(0);
                            v.w(1) = B.w(2);
                            v.w(2) = B.w(1);
                            v.w(3) = B.w(3);
                            break;
                        default:
                            skip = true;
                            return false;
                    }
                    return done(v);
                }
                default:
                    skip = true;
                    return false;
            }
        }
        default:
            return false;
    }
}

// ============================================================================================================================================
// Program-level reference (branches, delay slots, likely nullification, calls)
// ============================================================================================================================================
static bool refRun(Ref& R, Mem& M, const uint32_t* words, int n, bool& skip) {
    const uint32_t END = BASE + 4u * static_cast<uint32_t>(n);
    int idx = 0;
    skip = false;
    int guard = 0;
    while (idx < n && guard++ < 64) {
        const uint32_t w = words[idx];
        const uint32_t pc = BASE + 4u * idx;
        const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31, fn = w & 63;
        const int32_t simm = static_cast<int16_t>(w & 0xFFFF);
        auto S = [&](uint32_t i) -> int64_t { return i ? static_cast<int64_t>(R.r[i].d[0]) : 0; };
        auto U = [&](uint32_t i) -> uint64_t { return i ? R.r[i].d[0] : 0; };
        bool isBranch = false, likely = false, taken = false, link31Always = false, link31IfTaken = false;
        bool isJump = false, isJR = false, isJALR = false, isJAL = false;
        uint32_t target = pc + 4 + (static_cast<uint32_t>(simm) << 2);
        switch (op) {
            case 0x04:
                isBranch = true;
                taken = U(rs) == U(rt);
                break;
            case 0x05:
                isBranch = true;
                taken = U(rs) != U(rt);
                break;
            case 0x06:
                isBranch = true;
                taken = S(rs) <= 0;
                break;
            case 0x07:
                isBranch = true;
                taken = S(rs) > 0;
                break;
            case 0x14:
                isBranch = likely = true;
                taken = U(rs) == U(rt);
                break;
            case 0x15:
                isBranch = likely = true;
                taken = U(rs) != U(rt);
                break;
            case 0x16:
                isBranch = likely = true;
                taken = S(rs) <= 0;
                break;
            case 0x17:
                isBranch = likely = true;
                taken = S(rs) > 0;
                break;
            case 0x01:
                switch (rt) {
                    case 0x00:
                        isBranch = true;
                        taken = S(rs) < 0;
                        break;
                    case 0x01:
                        isBranch = true;
                        taken = S(rs) >= 0;
                        break;
                    case 0x02:
                        isBranch = likely = true;
                        taken = S(rs) < 0;
                        break;
                    case 0x03:
                        isBranch = likely = true;
                        taken = S(rs) >= 0;
                        break;
                    case 0x10:
                        isBranch = true;
                        taken = S(rs) < 0;
                        link31Always = true;
                        break;
                    case 0x11:
                        isBranch = true;
                        taken = S(rs) >= 0;
                        link31Always = true;
                        break;
                    case 0x12:
                        isBranch = likely = true;
                        taken = S(rs) < 0;
                        link31Always = true;
                        break;
                    case 0x13:
                        isBranch = likely = true;
                        taken = S(rs) >= 0;
                        link31Always = true;
                        break;
                    default:
                        break;
                }
                break;
            case 0x11:
                if (rs == 8) {
                    isBranch = true;
                    likely = rt >= 2;
                    const bool c = (R.fcr31 & 0x00800000u) != 0;
                    taken = (rt & 1) ? c : !c;
                }
                break;
            case 0x12:
                if (rs == 8) {
                    skip = true;
                    return false;
                } // BC2: VU0 condition not modelled
                break;
            case 0x02:
                isJump = true;
                target = (pc & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
                break;
            case 0x03:
                isJump = isJAL = true;
                target = (pc & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
                break;
            case 0x00:
                if (fn == 0x08) {
                    isJump = isJR = true;
                    target = static_cast<uint32_t>(U(rs));
                } else if (fn == 0x09) {
                    isJump = isJALR = true;
                    target = static_cast<uint32_t>(U(rs));
                }
                break;
            default:
                break;
        }
        (void)link31IfTaken;
        if (!isBranch && !isJump) {
            bool sk = false;
            if (!refExec(R, M, w, sk)) {
                skip = true;
                return false;
            }
            if (sk)
                skip = true;
            ++idx;
            continue;
        }
        if (idx + 1 >= n) {
            skip = true;
            return false;
        }
        const uint32_t delay = words[idx + 1];
        if (isBranch && link31Always)
            R.r[31].d[0] = pc + 8;
        if (isJAL)
            R.r[31].d[0] = pc + 8;
        if (isJALR && rd)
            R.r[rd].d[0] = pc + 8;
        const bool runDelay = !(likely && !taken);
        if (runDelay) {
            bool sk = false;
            if (!refExec(R, M, delay, sk)) {
                skip = true;
                return false;
            }
            if (sk)
                skip = true;
        }
        if (isBranch) {
            if (taken) {
                if (target >= BASE && target < END) {
                    idx = static_cast<int>((target - BASE) / 4);
                    continue;
                }
                R.pc = target;
                return true;
            }
            idx += 2;
            continue;
        }
        if (isJAL || isJALR) {
            ev("CALL t=%08x ret=%08x", target, pc + 8);
            idx += 2;
            continue;
        }
        // J / JR
        if (isJR || target < BASE || target >= END) {
            R.pc = target;
            return true;
        }
        idx = static_cast<int>((target - BASE) / 4); // J inside the function
    }
    R.pc = END;
    return true;
}

// ============================================================================================================================================
// Driver
// ============================================================================================================================================
// MADD/MSUB(A).S: the hardware rounds once, a host float op sequence twice; the difference is relative to the larger of
// |acc| and |fs*ft| when the result cancels, and saturation edge cases near the largest exponent differ as well. Not
// used by Drakan (no MADD.S in the game).
static bool fmaTolerated(const Ref& init, uint32_t w, uint32_t e, uint32_t r) {
    const uint32_t fs = (w >> 11) & 31, ft = (w >> 16) & 31;
    const double a = std::fabs(ps2d(init.acc)), p = std::fabs(ps2d(init.f[fs]) * ps2d(init.f[ft]));
    const double big = std::max(a, p);
    if (((e >> 23) & 255) >= 254 || ((r >> 23) & 255) >= 254 || big >= 0x1p126)
        return true;
    return std::fabs(ps2d(e) - ps2d(r)) <= big * 0x1p-22;
}

static bool isMemOp(uint32_t w, int& size) {
    const uint32_t op = w >> 26;
    switch (op) {
        case 0x20:
        case 0x24:
        case 0x28:
            size = 1;
            return true;
        case 0x21:
        case 0x25:
        case 0x29:
            size = 2;
            return true;
        case 0x23:
        case 0x27:
        case 0x2B:
        case 0x31:
        case 0x39:
            size = 4;
            return true;
        case 0x22:
        case 0x26:
        case 0x2A:
        case 0x2E:
            size = 0;
            return true; // unaligned word
        case 0x37:
        case 0x3F:
            size = 8;
            return true;
        case 0x1A:
        case 0x1B:
        case 0x2C:
        case 0x2D:
            size = 0;
            return true;
        case 0x1E:
        case 0x1F:
        case 0x36:
        case 0x3E:
            size = 0;
            return true; // LQ / SQ / LQC2 / SQC2: low 4 bits ignored (any address)
        default:
            return false;
    }
}

struct Stats {
    uint64_t runs = 0, mismatches = 0, skipped = 0, tolerated = 0;
};

int main(int argc, char** argv) {
    uint32_t states = 64;
    uint64_t seed = 1;
    std::string filter;
    int show = 3;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--states" && i + 1 < argc)
            states = std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc)
            seed = std::strtoull(argv[++i], nullptr, 0);
        else if (a == "--filter" && i + 1 < argc)
            filter = argv[++i];
        else if (a == "--show" && i + 1 < argc)
            show = std::atoi(argv[++i]);
        else if (a == "--verbose")
            verbose = true;
    }
    _mm_setcsr(_mm_getcsr() | 0x8040u); // DAZ + FTZ like the runtime's EE thread (EeScheduler::run)
    g_ramE = static_cast<uint8_t*>(std::calloc(RAMSZ, 1));
    g_ramR = static_cast<uint8_t*>(std::calloc(RAMSZ, 1));
    alignas(PS2Runtime) static unsigned char rtStorage[sizeof(PS2Runtime)];
    std::memset(rtStorage, 0, sizeof rtStorage);
    static_assert(sizeof(PS2Runtime) <= sizeof rtStorage, "Increase stub runtime storage");
    PS2Runtime* rt = reinterpret_cast<PS2Runtime*>(rtStorage);
    static R5900Context cx;
    Rng g(seed);

    struct ShapeStat {
        Stats st;
        std::vector<std::string> samples;
    };
    std::map<std::string, ShapeStat> byShape;
    uint64_t total = 0, bad = 0;

    for (size_t ci = 0; ci < g_dtCaseCount; ++ci) {
        const DtCase& c = g_dtCases[ci];
        if (std::getenv("DT_TRACE")) {
            std::fprintf(stderr, "case %s\n", c.name);
            std::fflush(stderr);
        }
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos &&
            std::string(c.shape).find(filter) == std::string::npos)
            continue;
        ShapeStat& ss = byShape[c.shape];
        const uint32_t w0 = c.words[0];
        const uint32_t op0 = w0 >> 26, fn0 = w0 & 63;
        const bool fpuTest = (op0 == 0x11);
        for (uint32_t st = 0; st < states; ++st) {
            Ref init;
            randomState(g, init, st % 8 == 0 /* some states with denormal inputs */);
            const bool allowDenormInputs = (st % 8 == 0);
            // case preparation
            int msz = 0;
            bool skipState = false;
            uint32_t eaTarget = 0;
            for (int wi = 0; wi < c.nwords; ++wi) {
                const uint32_t w = c.words[wi];
                if (isMemOp(w, msz)) {
                    uint32_t rs = (w >> 21) & 31;
                    if (rs == 0) {
                        skipState = true;
                        continue;
                    } // base $zero: address = sign extended imm, outside RAM: not testable
                    const int32_t simm = static_cast<int16_t>(w & 0xFFFF);
                    uint32_t off = 0x400 + g.below(0x1000);
                    if (msz == 8)
                        off &= ~7u;
                    else if (msz == 4)
                        off &= ~3u;
                    else if (msz == 2)
                        off &= ~1u;
                    static const uint32_t segs[] = {0x00000000u, 0x80000000u, 0xA0000000u, 0x20000000u};
                    eaTarget = WIN + off;
                    init.r[rs].d[0] = static_cast<uint64_t>(segs[g.below(4)] + eaTarget - static_cast<uint32_t>(simm));
                    if (g.below(2))
                        init.r[rs].d[0] = sx32(static_cast<uint32_t>(init.r[rs].d[0]));
                    // the delay slot / later words may use other registers; fine
                }
            }
            if (skipState) {
                ss.st.skipped++;
                continue;
            }
            // JR / JALR targets
            if (op0 == 0 && (fn0 == 8 || fn0 == 9)) {
                const uint32_t rs = (w0 >> 21) & 31;
                if (rs)
                    init.r[rs].d[0] = sx32(g.below(2) ? BASE + 16 : BASE + 0x1000 + 4 * g.below(64));
            }
            // fill the memory window with identical random data
            for (uint32_t i = 0; i < WINSZ; i += 4) {
                const uint32_t v = g.next();
                std::memcpy(g_ramE + WIN + i, &v, 4);
                std::memcpy(g_ramR + WIN + i, &v, 4);
            }
            Ref R = init;
            Mem M{g_ramR};
            g_events.clear();
            bool skip = false;
            const bool ok = refRun(R, M, c.words, c.nwords, skip);
            std::vector<std::string> evRef = g_events;
            do {
                if (!ok) {
                    ss.st.skipped++;
                    break;
                }
                // run the emitted code
                toCtx(init, cx);
                g_events.clear();
                cx.pc = BASE;
                try {
                    c.fn(g_ramE, &cx, rt);
                } catch (const std::exception& e) {
                    ss.st.runs++;
                    ss.st.mismatches++;
                    ++bad;
                    ++total;
                    if (static_cast<int>(ss.samples.size()) < show)
                        ss.samples.push_back(std::string("  ") + c.name + ": emitted code throws: " + e.what());
                    break;
                }
                std::vector<std::string> evEmit = g_events;
                Ref E;
                fromCtx(cx, E);
                ss.st.runs++;
                ++total;
                // compare
                std::string diff;
                char buf[200];
                for (int i = 0; i < 32; ++i)
                    if (!(E.r[i] == R.r[i])) {
                        std::snprintf(buf, sizeof buf, " r%d: emit=%016llx_%016llx ref=%016llx_%016llx;", i,
                                      (unsigned long long)E.r[i].d[1], (unsigned long long)E.r[i].d[0],
                                      (unsigned long long)R.r[i].d[1], (unsigned long long)R.r[i].d[0]);
                        diff += buf;
                    }
                if (E.hi != R.hi) {
                    std::snprintf(buf, sizeof buf, " hi: emit=%llx ref=%llx;", (unsigned long long)E.hi,
                                  (unsigned long long)R.hi);
                    diff += buf;
                }
                if (E.lo != R.lo) {
                    std::snprintf(buf, sizeof buf, " lo: emit=%llx ref=%llx;", (unsigned long long)E.lo,
                                  (unsigned long long)R.lo);
                    diff += buf;
                }
                if (E.hi1 != R.hi1) {
                    std::snprintf(buf, sizeof buf, " hi1: emit=%llx ref=%llx;", (unsigned long long)E.hi1,
                                  (unsigned long long)R.hi1);
                    diff += buf;
                }
                if (E.lo1 != R.lo1) {
                    std::snprintf(buf, sizeof buf, " lo1: emit=%llx ref=%llx;", (unsigned long long)E.lo1,
                                  (unsigned long long)R.lo1);
                    diff += buf;
                }
                if (E.sa != R.sa) {
                    std::snprintf(buf, sizeof buf, " sa: emit=%x ref=%x;", E.sa, R.sa);
                    diff += buf;
                }
                bool fpTolerated = false;
                for (int i = 0; i < 32; ++i)
                    if (E.f[i] != R.f[i]) {
                        // 1 ulp tolerance for the operations where the hardware (and the reference) round differently
                        // from a host float op
                        const uint32_t sub = (w0 & 0x3F);
                        const bool tolOp =
                            fpuTest && (((w0 >> 21) & 31) == 0x10 || ((w0 >> 21) & 31) == 0x14) &&
                            (sub == 0x03 || sub == 0x04 || sub == 0x16 || sub == 0x1C || sub == 0x1D || sub == 0x20);
                        if (tolOp && ulpDist(E.f[i], R.f[i]) <= 1) {
                            fpTolerated = true;
                            continue;
                        }
                        if (fpuTest && (sub == 0x1C || sub == 0x1D) && fmaTolerated(init, w0, E.f[i], R.f[i])) {
                            fpTolerated = true;
                            continue;
                        }
                        std::snprintf(buf, sizeof buf, " f%d: emit=%08x ref=%08x;", i, E.f[i], R.f[i]);
                        diff += buf;
                    }
                if (E.acc != R.acc) {
                    const uint32_t sub = (w0 & 0x3F);
                    if (fpuTest && (sub == 0x1E || sub == 0x1F) &&
                        (ulpDist(E.acc, R.acc) <= 1 || fmaTolerated(init, w0, E.acc, R.acc)))
                        fpTolerated = true;
                    else {
                        std::snprintf(buf, sizeof buf, " acc: emit=%08x ref=%08x;", E.acc, R.acc);
                        diff += buf;
                    }
                }
                const uint32_t fmask = (op0 == 0x11 && ((w0 >> 21) & 31) == 6) ? 0x0183FFFFu : 0x00800000u;
                if ((E.fcr31 & fmask) != (R.fcr31 & fmask)) {
                    std::snprintf(buf, sizeof buf, " fcr31: emit=%08x ref=%08x;", E.fcr31, R.fcr31);
                    diff += buf;
                }
                bool vuTol = false;
                auto vuLaneEq = [&](uint32_t a, uint32_t b) -> bool {
                    if (a == b)
                        return true;
                    if ((a & 0x7F800000u) == 0 && (b & 0x7F800000u) == 0)
                        return true; // +-0 (sign of zero not modelled)
                    if (op0 == 0x12 && ulpDist(a, b) <= 1) {
                        vuTol = true;
                        return true;
                    } // MADD / MSUB round once on the hardware model
                    return false;
                };
                auto vuEq = [&](const uint32_t* a, const uint32_t* b) {
                    bool ok = true;
                    for (int k = 0; k < 4; ++k)
                        ok &= vuLaneEq(a[k], b[k]);
                    return ok;
                };
                for (int i = 0; i < 32; ++i)
                    if (!vuEq(E.vf[i], R.vf[i])) {
                        std::snprintf(buf, sizeof buf, " vf%d: emit=%08x,%08x,%08x,%08x ref=%08x,%08x,%08x,%08x;", i,
                                      E.vf[i][0], E.vf[i][1], E.vf[i][2], E.vf[i][3], R.vf[i][0], R.vf[i][1],
                                      R.vf[i][2], R.vf[i][3]);
                        diff += buf;
                    }
                if (!vuEq(E.vacc, R.vacc)) {
                    std::snprintf(buf, sizeof buf, " vacc: emit=%08x,%08x,%08x,%08x ref=%08x,%08x,%08x,%08x;",
                                  E.vacc[0], E.vacc[1], E.vacc[2], E.vacc[3], R.vacc[0], R.vacc[1], R.vacc[2],
                                  R.vacc[3]);
                    diff += buf;
                }
                if (E.vq != R.vq && !(ulpDist(E.vq, R.vq) <= 1)) {
                    std::snprintf(buf, sizeof buf, " vq: emit=%08x ref=%08x;", E.vq, R.vq);
                    diff += buf;
                }
                for (int i = 0; i < 16; ++i)
                    if (E.vi[i] != R.vi[i]) {
                        std::snprintf(buf, sizeof buf, " vi%d: emit=%04x ref=%04x;", i, E.vi[i], R.vi[i]);
                        diff += buf;
                    }
                if (E.status != R.status) {
                    std::snprintf(buf, sizeof buf, " status: emit=%08x ref=%08x;", E.status, R.status);
                    diff += buf;
                }
                if (E.pc != R.pc) {
                    std::snprintf(buf, sizeof buf, " pc: emit=%08x ref=%08x;", E.pc, R.pc);
                    diff += buf;
                }
                if (std::memcmp(g_ramE + WIN, g_ramR + WIN, WINSZ) != 0) {
                    uint32_t k = 0;
                    while (g_ramE[WIN + k] == g_ramR[WIN + k])
                        ++k;
                    std::snprintf(buf, sizeof buf, " mem[+%x]: emit=%02x ref=%02x;", k, g_ramE[WIN + k],
                                  g_ramR[WIN + k]);
                    diff += buf;
                }
                if (evEmit != evRef) {
                    diff += " events: emit=[";
                    for (auto& s : evEmit)
                        diff += s + "|";
                    diff += "] ref=[";
                    for (auto& s : evRef)
                        diff += s + "|";
                    diff += "];";
                }
                if (skip && !diff.empty()) {
                    ss.st.tolerated++;
                    break;
                } // ambiguous reference (MAX/MIN ties ...)
                if (diff.empty()) {
                    if (fpTolerated || vuTol)
                        ss.st.tolerated++;
                } else {
                    ss.st.mismatches++;
                    ++bad;
                    if (static_cast<int>(ss.samples.size()) < show) {
                        char hd[200];
                        std::snprintf(hd, sizeof hd, "  %s [", c.name);
                        std::string s = hd;
                        for (int wi = 0; wi < c.nwords; ++wi) {
                            std::snprintf(hd, sizeof hd, "%s%08x", wi ? " " : "", c.words[wi]);
                            s += hd;
                        }
                        s += "]";
                        // register state that matters: the registers named in the first word
                        const uint32_t rs = (w0 >> 21) & 31, rt = (w0 >> 16) & 31;
                        std::snprintf(hd, sizeof hd, " in: rs(r%u)=%016llx rt(r%u)=%016llx sa=%u |", rs,
                                      (unsigned long long)init.r[rs].d[0], rt, (unsigned long long)init.r[rt].d[0],
                                      init.sa);
                        s += hd;
                        if (fpuTest) {
                            std::snprintf(hd, sizeof hd, " fs(f%u)=%08x ft(f%u)=%08x acc=%08x fcr31=%08x |",
                                          (w0 >> 11) & 31, init.f[(w0 >> 11) & 31], rt, init.f[rt], init.acc,
                                          init.fcr31);
                            s += hd;
                        }
                        s += diff;
                        ss.samples.push_back(s);
                    }
                }
            } while (0);
            (void)allowDenormInputs;
        }
    }

    std::printf("difftest: %llu executed state/case pairs, %llu mismatching\n", (unsigned long long)total,
                (unsigned long long)bad);
    int nbadShapes = 0;
    for (auto& kv : byShape) {
        const Stats& s = kv.second.st;
        if (s.mismatches == 0) {
            if (verbose)
                std::printf("ok    %-22s runs=%llu skipped=%llu tolerated=%llu\n", kv.first.c_str(),
                            (unsigned long long)s.runs, (unsigned long long)s.skipped, (unsigned long long)s.tolerated);
            continue;
        }
        ++nbadShapes;
        std::printf("MISMATCH %-22s runs=%llu mismatches=%llu\n", kv.first.c_str(), (unsigned long long)s.runs,
                    (unsigned long long)s.mismatches);
        for (auto& line : kv.second.samples)
            std::printf("%s\n", line.c_str());
    }
    uint64_t skippedShapes = 0;
    for (auto& kv : byShape)
        if (kv.second.st.runs == 0) {
            ++skippedShapes;
            if (verbose)
                std::printf("skip  %s (reference does not model it)\n", kv.first.c_str());
        }
    std::printf("shapes with mismatches: %d; shapes not modelled by the reference: %llu\n", nbadShapes,
                (unsigned long long)skippedShapes);
    if (total == 0)
        std::fprintf(stderr, "No modelled cases executed; check --filter and --states.\n");
    return bad ? 1 : (total == 0 ? 2 : 0);
}
