#include "vu1ref.h"
#include <cfenv>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace {
inline float f(uint32_t b) {
    float x;
    std::memcpy(&x, &b, 4);
    return x;
}
inline uint32_t u(float x) {
    uint32_t b;
    std::memcpy(&b, &x, 4);
    return b;
}
// operand flush: denormal -> +-0, exp 255 -> +-Fmax  (PCSX2 vuDouble)
inline uint32_t nrm(uint32_t b) {
    uint32_t e = (b >> 23) & 0xff;
    if (e == 0)
        return b & 0x80000000u;
    if (e == 255)
        return (b & 0x80000000u) | 0x7f7fffffu;
    return b;
}
inline float nf(uint32_t b) {
    return f(nrm(b));
}
// result clamp; flags: bit0 zero, bit1 sign, bit2 underflow, bit3 overflow
inline uint32_t res(float x, uint32_t& flags) {
    uint32_t b = u(x);
    uint32_t e = (b >> 23) & 0xff;
    flags = 0;
    if (e == 0) {
        if (b & 0x7fffff)
            flags |= 4;
        b &= 0x80000000u;
        flags |= 1;
    } else if (e == 255) {
        b = (b & 0x80000000u) | 0x7f7fffffu;
        flags |= 8;
    }
    if (b & 0x80000000u)
        flags |= 2;
    return b;
}
inline int64_t key(uint32_t b) {
    return (b & 0x80000000u) ? -(int64_t)(b & 0x7fffffffu) : (int64_t)(b & 0x7fffffffu);
}
inline int32_t ftoi(uint32_t b, int sh) {
    double d = (double)nf(b) * (double)(1 << sh);
    if (d >= 2147483647.0)
        return 0x7fffffff;
    if (d <= -2147483648.0)
        return (int32_t)0x80000000u;
    return (int32_t)d;
}
} // namespace

Vu1Ref::Vu1Ref() {
    reset();
}
void Vu1Ref::reset() {
    memset(vf, 0, sizeof vf);
    vf[0][3] = 0x3f800000u;
    memset(vi, 0, sizeof vi);
    memset(acc, 0, sizeof acc);
    q = 0x3f800000u;
    p = 0;
    i = 0;
    r = 0x3f800000u;
    mac = clip = status = clipRun = 0;
    top = itop = 0;
    pc = 0;
    cycle = 0;
    memset(code, 0, sizeof code);
    memset(mem, 0, sizeof mem);
    memset(vfReady, 0, sizeof vfReady);
    memset(iluReady, 0, sizeof iluReady);
    flagPipe.clear();
    qPend = pPend = false;
    backupReg = -1;
}

void Vu1Ref::settle() {
    size_t k = 0;
    for (; k < flagPipe.size(); ++k) {
        if (flagPipe[k].ready > cycle)
            break;
        if (flagPipe[k].hasMac)
            mac = flagPipe[k].mac;
        if (flagPipe[k].hasClip)
            clip = flagPipe[k].clip;
    }
    if (k)
        flagPipe.erase(flagPipe.begin(), flagPipe.begin() + k);
    if (qPend && cycle >= qReady) {
        q = qVal;
        qPend = false;
    }
    if (pPend && cycle >= pReady) {
        p = pVal;
        pPend = false;
    }
}
void Vu1Ref::flushAll() {
    uint64_t t = cycle;
    for (auto& e : flagPipe)
        t = std::max(t, e.ready);
    if (qPend)
        t = std::max(t, qReady);
    if (pPend)
        t = std::max(t, pReady);
    cycle = t;
    settle();
}
uint16_t Vu1Ref::readBranchVi(int reg) {
    if (opt.branchHazard && reg && reg == backupReg && cycle < backupUntil) {
        ++nHazard;
        return backupVal;
    }
    return vi[reg];
}
void Vu1Ref::writeVi(int reg, uint16_t v) {
    if (!reg)
        return;
    if (!(backupReg == reg && cycle < backupUntil)) {
        backupReg = reg;
        backupVal = vi[reg];
    }
    backupUntil = cycle + 2;
    vi[reg] = v;
}

bool Vu1Ref::run(uint32_t startPc, uint32_t maxPairs) {
    int oldRound = std::fegetround();
    std::fesetround(FE_TOWARDZERO);
    pc = startPc & 0x3ff8;
    bool ended = false, ok = true;
    int delay = 0;
    bool ePending = false;
    bool brPending = false;
    uint32_t brTarget = 0;
    packets.clear();
    kicks.clear();
    kickPcs.clear();
    error.clear();
    vf[0][0] = vf[0][1] = vf[0][2] = 0;
    vf[0][3] = 0x3f800000u;
    for (uint32_t n = 0; n < maxPairs && !ended; ++n) {
        uint32_t lo, up;
        memcpy(&lo, code + pc, 4);
        memcpy(&up, code + pc + 4, 4);
        const bool ibit = up >> 31, ebit = (up >> 30) & 1;
        const uint32_t uop = up & 0x3f;
        uint32_t udest = (up >> 21) & 0xf, uft = (up >> 16) & 0x1f, ufs = (up >> 11) & 0x1f, ufd = (up >> 6) & 0x1f;
        uint32_t usub = 0xff;
        bool special = (uop >= 0x3c);
        if (special)
            usub = (up & 3) | ((up >> 4) & 0x7c);
        auto need = [&](int reg, uint32_t lanes, uint64_t& t) {
            if (!reg)
                return;
            for (int c = 0; c < 4; ++c)
                if (lanes & (8 >> c))
                    t = std::max(t, vfReady[reg][c]);
        };
        uint64_t t = cycle;
        if (opt.stallModel) {
            bool isNop = (up & 0x7ff) == 0x2ff;
            if (!isNop) {
                bool bc = false;
                int bcl = 0;
                bool usesVt = true, usesVs = true;
                uint32_t vsl = udest, vtl = udest;
                if (!special) {
                    if (uop <= 0x1b) {
                        bc = true;
                        bcl = uop & 3;
                    } else if (uop <= 0x27 && uop != 0x2e)
                        usesVt = false;
                    if (uop == 0x2e) {
                        vsl = 0xe;
                        vtl = 0xe;
                    }
                } else {
                    if (usub <= 0x0f || (usub >= 0x18 && usub <= 0x1b)) {
                        bc = true;
                        bcl = usub & 3;
                    } else if (usub <= 0x17 || usub == 0x1d)
                        usesVt = false;
                    else if (usub >= 0x1c && usub <= 0x27)
                        usesVt = false;
                    if (usub == 0x2e) {
                        vsl = 0xe;
                        vtl = 0xe;
                    }
                    if (usub == 0x1f) {
                        vsl = 0xe;
                        vtl = 1;
                        bc = false;
                        usesVt = true;
                    }
                    if (usub == 0x2f)
                        usesVs = usesVt = false;
                }
                if (usesVs)
                    need(ufs, vsl, t);
                if (usesVt) {
                    if (bc)
                        need(uft, 8 >> bcl, t);
                    else
                        need(uft, vtl, t);
                }
            }
            if (!ibit) {
                uint32_t op7 = lo >> 25, dest = (lo >> 21) & 0xf;
                if (op7 == 0x01)
                    need((lo >> 11) & 0x1f, dest, t);
                else if (op7 == 0x40 && (lo & 0x3f) >= 0x3c) {
                    uint32_t f2 = (lo & 3) | ((lo >> 4) & 0x7c);
                    uint32_t fs = (lo >> 11) & 0x1f, ft = (lo >> 16) & 0x1f;
                    uint32_t fsf = (lo >> 21) & 3, ftf = (lo >> 23) & 3;
                    switch (f2) {
                        case 0x30:
                            need(fs, dest, t);
                            break;
                        case 0x31:
                            need(fs, ((dest << 1) | (dest >> 3)) & 0xf, t);
                            break;
                        case 0x35:
                        case 0x37:
                            need(fs, dest, t);
                            break;
                        case 0x38:
                            need(fs, 8 >> fsf, t);
                            need(ft, 8 >> ftf, t);
                            if (qPend)
                                t = std::max(t, qReady);
                            break;
                        case 0x3c:
                            need(fs, 8 >> fsf, t);
                            break;
                        case 0x73:
                            need(fs, 0xe, t);
                            if (pPend)
                                t = std::max(t, pReady);
                            break;
                        case 0x3b:
                            if (qPend)
                                t = std::max(t, qReady);
                            break;
                        case 0x7b:
                        case 0x64:
                            if (pPend)
                                t = std::max(t, pReady);
                            break;
                        default:
                            break;
                    }
                }
                if ((op7 >= 0x28 && op7 <= 0x2f) || op7 == 0x24 || op7 == 0x25) {
                    uint32_t is = (lo >> 11) & 0xf, it = (lo >> 16) & 0xf;
                    t = std::max(t, iluReady[is]);
                    if (op7 == 0x28 || op7 == 0x29)
                        t = std::max(t, iluReady[it]);
                }
            }
        }
        if (t > cycle) {
            cycle = t;
            ++nStalls;
        }
        settle();
        ++nPairs;

        uint32_t accN[4] = {};
        bool accW[4] = {};
        uint32_t macN = 0;
        bool macW = false;
        uint32_t clipN = 0;
        bool clipW = false;
        struct Pend {
            int reg;
            uint32_t lanes;
            uint32_t v[4];
            int lat;
        };
        std::vector<Pend> pend;

        uint32_t S_vs[4], S_vt[4], S_acc[4];
        memcpy(S_vs, vf[ufs], 16);
        memcpy(S_vt, vf[uft], 16);
        memcpy(S_acc, acc, 16);
        const uint32_t Q = nrm(q), I = nrm(i);

        if ((up & 0x7ff) != 0x2ff) {
            float vs[4], vt[4], ac[4];
            for (int c = 0; c < 4; ++c) {
                vs[c] = nf(S_vs[c]);
                vt[c] = nf(S_vt[c]);
                ac[c] = nf(S_acc[c]);
            }
            int kind = -1;
            bool toAcc = false;
            float b2[4] = {0, 0, 0, 0};
            bool opx = false;
            uint32_t rb[4] = {0, 0, 0, 0};
            bool intRes = false;
            int dreg = ufd;
            auto setB = [&](int mode, int b) {
                for (int c = 0; c < 4; ++c)
                    b2[c] = mode == 0 ? vt[b] : mode == 1 ? f(Q) : mode == 2 ? f(I) : vt[c];
            };
            if (!special) {
                if (uop <= 0x03) {
                    kind = 0;
                    setB(0, uop & 3);
                } else if (uop <= 0x07) {
                    kind = 1;
                    setB(0, uop & 3);
                } else if (uop <= 0x0b) {
                    kind = 3;
                    setB(0, uop & 3);
                } else if (uop <= 0x0f) {
                    kind = 4;
                    setB(0, uop & 3);
                } else if (uop <= 0x13) {
                    kind = 5;
                    setB(0, uop & 3);
                } else if (uop <= 0x17) {
                    kind = 6;
                    setB(0, uop & 3);
                } else if (uop <= 0x1b) {
                    kind = 2;
                    setB(0, uop & 3);
                } else
                    switch (uop) {
                        case 0x1c:
                            kind = 2;
                            setB(1, 0);
                            break;
                        case 0x1d:
                            kind = 5;
                            setB(2, 0);
                            break;
                        case 0x1e:
                            kind = 2;
                            setB(2, 0);
                            break;
                        case 0x1f:
                            kind = 6;
                            setB(2, 0);
                            break;
                        case 0x20:
                            kind = 0;
                            setB(1, 0);
                            break;
                        case 0x21:
                            kind = 3;
                            setB(1, 0);
                            break;
                        case 0x22:
                            kind = 0;
                            setB(2, 0);
                            break;
                        case 0x23:
                            kind = 3;
                            setB(2, 0);
                            break;
                        case 0x24:
                            kind = 1;
                            setB(1, 0);
                            break;
                        case 0x25:
                            kind = 4;
                            setB(1, 0);
                            break;
                        case 0x26:
                            kind = 1;
                            setB(2, 0);
                            break;
                        case 0x27:
                            kind = 4;
                            setB(2, 0);
                            break;
                        case 0x28:
                            kind = 0;
                            setB(3, 0);
                            break;
                        case 0x29:
                            kind = 3;
                            setB(3, 0);
                            break;
                        case 0x2a:
                            kind = 2;
                            setB(3, 0);
                            break;
                        case 0x2b:
                            kind = 5;
                            setB(3, 0);
                            break;
                        case 0x2c:
                            kind = 1;
                            setB(3, 0);
                            break;
                        case 0x2d:
                            kind = 4;
                            setB(3, 0);
                            break;
                        case 0x2f:
                            kind = 6;
                            setB(3, 0);
                            break;
                        case 0x2e:
                            opx = true;
                            break;
                        default:
                            error = "bad upper op";
                            ok = false;
                            break;
                    }
            } else {
                dreg = uft;
                uint32_t s = usub;
                if (s <= 0x03) {
                    kind = 0;
                    toAcc = true;
                    setB(0, s & 3);
                } else if (s <= 0x07) {
                    kind = 1;
                    toAcc = true;
                    setB(0, s & 3);
                } else if (s <= 0x0b) {
                    kind = 3;
                    toAcc = true;
                    setB(0, s & 3);
                } else if (s <= 0x0f) {
                    kind = 4;
                    toAcc = true;
                    setB(0, s & 3);
                } else if (s >= 0x10 && s <= 0x13) {
                    static const int sh[4] = {0, 4, 12, 15};
                    intRes = true;
                    for (int c = 0; c < 4; ++c) {
                        uint32_t fl;
                        rb[c] = res((float)((double)(int32_t)S_vs[c] / (double)(1 << sh[s - 0x10])), fl);
                    }
                } else if (s >= 0x14 && s <= 0x17) {
                    static const int sh[4] = {0, 4, 12, 15};
                    intRes = true;
                    for (int c = 0; c < 4; ++c)
                        rb[c] = (uint32_t)ftoi(S_vs[c], sh[s - 0x14]);
                } else if (s >= 0x18 && s <= 0x1b) {
                    kind = 2;
                    toAcc = true;
                    setB(0, s & 3);
                } else
                    switch (s) {
                        case 0x1c:
                            kind = 2;
                            toAcc = true;
                            setB(1, 0);
                            break;
                        case 0x1d:
                            intRes = true;
                            for (int c = 0; c < 4; ++c)
                                rb[c] = nrm(S_vs[c]) & 0x7fffffffu;
                            break;
                        case 0x1e:
                            kind = 2;
                            toAcc = true;
                            setB(2, 0);
                            break;
                        case 0x1f: {
                            float w = std::fabs(f(nrm(S_vt[3])));
                            uint32_t nc = 0;
                            auto cmp = [&](float x, uint32_t pos, uint32_t neg) {
                                if (x > w)
                                    nc |= pos;
                                if (x < -w)
                                    nc |= neg;
                            };
                            cmp(vs[0], 1, 2);
                            cmp(vs[1], 4, 8);
                            cmp(vs[2], 0x10, 0x20);
                            clipRun = ((clipRun << 6) | nc) & 0xffffff;
                            clipN = clipRun;
                            clipW = true;
                            break;
                        }
                        case 0x20:
                            kind = 0;
                            toAcc = true;
                            setB(1, 0);
                            break;
                        case 0x21:
                            kind = 3;
                            toAcc = true;
                            setB(1, 0);
                            break;
                        case 0x22:
                            kind = 0;
                            toAcc = true;
                            setB(2, 0);
                            break;
                        case 0x23:
                            kind = 3;
                            toAcc = true;
                            setB(2, 0);
                            break;
                        case 0x24:
                            kind = 1;
                            toAcc = true;
                            setB(1, 0);
                            break;
                        case 0x25:
                            kind = 4;
                            toAcc = true;
                            setB(1, 0);
                            break;
                        case 0x26:
                            kind = 1;
                            toAcc = true;
                            setB(2, 0);
                            break;
                        case 0x27:
                            kind = 4;
                            toAcc = true;
                            setB(2, 0);
                            break;
                        case 0x28:
                            kind = 0;
                            toAcc = true;
                            setB(3, 0);
                            break;
                        case 0x29:
                            kind = 3;
                            toAcc = true;
                            setB(3, 0);
                            break;
                        case 0x2a:
                            kind = 2;
                            toAcc = true;
                            setB(3, 0);
                            break;
                        case 0x2c:
                            kind = 1;
                            toAcc = true;
                            setB(3, 0);
                            break;
                        case 0x2d:
                            kind = 4;
                            toAcc = true;
                            setB(3, 0);
                            break;
                        case 0x2e:
                            opx = true;
                            toAcc = true;
                            break;
                        case 0x2f:
                        case 0x30:
                            break;
                        default:
                            error = "bad upper special";
                            ok = false;
                            break;
                    }
            }
            if (!ok)
                break;
            const bool isCl = special && usub == 0x1f;
            if (!isCl) {
                uint32_t out[4] = {};
                uint32_t flg[4] = {0, 0, 0, 0};
                bool fmacFlags = false;
                bool anyOut = true;
                if (intRes) {
                    for (int c = 0; c < 4; ++c)
                        out[c] = rb[c];
                } else if (opx) {
                    float r3[3];
                    if (!toAcc) {
                        r3[0] = ac[0] - vs[1] * vt[2];
                        r3[1] = ac[1] - vs[2] * vt[0];
                        r3[2] = ac[2] - vs[0] * vt[1];
                    } else {
                        r3[0] = vs[1] * vt[2];
                        r3[1] = vs[2] * vt[0];
                        r3[2] = vs[0] * vt[1];
                    }
                    for (int c = 0; c < 3; ++c)
                        out[c] = res(r3[c], flg[c]);
                    out[3] = 0;
                    flg[3] = 1;
                    fmacFlags = true;
                } else if (kind >= 0) {
                    for (int c = 0; c < 4; ++c) {
                        float r = 0;
                        switch (kind) {
                            case 0:
                                r = vs[c] + b2[c];
                                break;
                            case 1:
                                r = vs[c] - b2[c];
                                break;
                            case 2:
                                r = vs[c] * b2[c];
                                break;
                            case 3:
                                r = ac[c] + vs[c] * b2[c];
                                break;
                            case 4:
                                r = ac[c] - vs[c] * b2[c];
                                break;
                            case 5: {
                                uint32_t a = nrm(S_vs[c]), bb = u(b2[c]);
                                r = f(key(a) > key(bb) ? a : bb);
                                break;
                            }
                            case 6: {
                                uint32_t a = nrm(S_vs[c]), bb = u(b2[c]);
                                r = f(key(a) < key(bb) ? a : bb);
                                break;
                            }
                        }
                        if (kind >= 5)
                            out[c] = u(r);
                        else
                            out[c] = res(r, flg[c]);
                    }
                    fmacFlags = kind < 5;
                    if (kind >= 5 && opt.macFromMaxMini)
                        fmacFlags = true;
                } else
                    anyOut = false;
                if (anyOut) {
                    if (toAcc) {
                        for (int c = 0; c < 4; ++c)
                            if (udest & (8 >> c)) {
                                accN[c] = out[c];
                                accW[c] = true;
                            }
                    } else if (dreg) {
                        Pend pd{dreg, udest, {out[0], out[1], out[2], out[3]}, opt.fmacLatency};
                        pend.push_back(pd);
                    }
                    if (fmacFlags) {
                        uint32_t m = 0;
                        for (int c = 0; c < 4; ++c)
                            if (udest & (8 >> c)) {
                                if (flg[c] & 1)
                                    m |= 1u << (3 - c);
                                if (flg[c] & 2)
                                    m |= 1u << (7 - c);
                                if (flg[c] & 4)
                                    m |= 1u << (11 - c);
                                if (flg[c] & 8)
                                    m |= 1u << (15 - c);
                            }
                        macN = m;
                        macW = true;
                    }
                }
            }
        }
        // ---- lower ----
        bool branchTaken = false;
        uint32_t newTarget = 0;
        std::vector<std::pair<int, uint16_t>> viW;
        struct Store {
            uint32_t addr;
            uint32_t w[4];
            uint32_t lanes;
        };
        std::vector<Store> stores;
        uint32_t newI = i;
        bool setI = false;
        bool newQ = false;
        uint32_t newQv = 0;
        bool newP = false;
        uint32_t newPv = 0, newPLat = 0;
        int kickAddr = -1;
        auto vfS = [&](int reg, uint32_t* o) { memcpy(o, vf[reg], 16); };
        if (ibit) {
            newI = lo;
            setI = true;
        } else if (lo != 0x8000033c && lo != 0) {
            uint32_t op7 = lo >> 25, dest = (lo >> 21) & 0xf;
            int fs = (lo >> 11) & 0x1f, ft = (lo >> 16) & 0x1f;
            int is = (lo >> 11) & 0xf, it = (lo >> 16) & 0xf, id = (lo >> 6) & 0xf;
            int imm11 = ((int32_t)(lo << 21)) >> 21;
            auto adr = [&](int base, int off) { return ((uint32_t)(vi[base] + off) & 0x3ff) * 16u; };
            auto br = [&](bool cond) {
                if (cond) {
                    branchTaken = true;
                    newTarget = (pc + 8 + imm11 * 8) & 0x3ff8;
                }
            };
            switch (op7) {
                case 0x00: {
                    uint32_t a = adr(is, imm11);
                    uint32_t v[4];
                    memcpy(v, mem + a, 16);
                    Pend pd{ft, dest, {v[0], v[1], v[2], v[3]}, 0};
                    pend.push_back(pd);
                    break;
                }
                case 0x01: {
                    Store s;
                    s.addr = adr(it, imm11);
                    vfS(fs, s.w);
                    s.lanes = dest;
                    stores.push_back(s);
                    break;
                }
                case 0x04: {
                    uint32_t a = adr(is, imm11);
                    int c = dest & 8 ? 0 : dest & 4 ? 1 : dest & 2 ? 2 : 3;
                    uint32_t v;
                    memcpy(&v, mem + a + c * 4, 4);
                    viW.push_back({it, (uint16_t)v});
                    iluReady[it] = cycle + 4;
                    break;
                }
                case 0x05: {
                    Store s;
                    s.addr = adr(is, imm11);
                    for (int c = 0; c < 4; ++c)
                        s.w[c] = vi[it];
                    s.lanes = dest;
                    stores.push_back(s);
                    break;
                }
                case 0x08: {
                    int imm = (lo & 0x7ff) | ((lo >> 10) & 0x7800);
                    viW.push_back({it, (uint16_t)(vi[is] + imm)});
                    break;
                }
                case 0x09: {
                    int imm = (lo & 0x7ff) | ((lo >> 10) & 0x7800);
                    viW.push_back({it, (uint16_t)(vi[is] - imm)});
                    break;
                }
                case 0x10:
                    viW.push_back({1, (uint16_t)((clip & 0xffffff) == (lo & 0xffffff))});
                    break;
                case 0x12:
                    viW.push_back({1, (uint16_t)((clip & lo & 0xffffff) != 0)});
                    break;
                case 0x13:
                    viW.push_back({1, (uint16_t)(((clip | lo) & 0xffffff) == 0xffffff)});
                    break;
                case 0x1a:
                    viW.push_back({it, (uint16_t)(mac & vi[is])});
                    break;
                case 0x1b:
                    viW.push_back({it, (uint16_t)(mac | vi[is])});
                    break;
                case 0x18:
                    viW.push_back({it, (uint16_t)((mac & 0xffff) == vi[is])});
                    break;
                case 0x1c:
                    viW.push_back({it, (uint16_t)(clip & 0xfff)});
                    break;
                case 0x20:
                    br(true);
                    break;
                case 0x28:
                    br(readBranchVi(is) == readBranchVi(it));
                    break;
                case 0x29:
                    br(readBranchVi(is) != readBranchVi(it));
                    break;
                case 0x2c:
                    br((int16_t)readBranchVi(is) < 0);
                    break;
                case 0x2d:
                    br((int16_t)readBranchVi(is) > 0);
                    break;
                case 0x2e:
                    br((int16_t)readBranchVi(is) <= 0);
                    break;
                case 0x2f:
                    br((int16_t)readBranchVi(is) >= 0);
                    break;
                case 0x40: {
                    uint32_t fn = lo & 0x3f;
                    if (fn == 0x30)
                        viW.push_back({id, (uint16_t)(vi[is] + vi[it])});
                    else if (fn == 0x31)
                        viW.push_back({id, (uint16_t)(vi[is] - vi[it])});
                    else if (fn == 0x32) {
                        int im = ((int32_t)(((lo >> 6) & 0x1f) << 27)) >> 27;
                        viW.push_back({it, (uint16_t)(vi[is] + im)});
                    } else if (fn == 0x34)
                        viW.push_back({id, (uint16_t)(vi[is] & vi[it])});
                    else if (fn == 0x35)
                        viW.push_back({id, (uint16_t)(vi[is] | vi[it])});
                    else if (fn >= 0x3c) {
                        uint32_t f2 = (lo & 3) | ((lo >> 4) & 0x7c);
                        uint32_t fsf = (lo >> 21) & 3, ftf = (lo >> 23) & 3;
                        switch (f2) {
                            case 0x30: {
                                uint32_t v[4];
                                vfS(fs, v);
                                Pend pd{ft, dest, {v[0], v[1], v[2], v[3]}, opt.fmacLatency};
                                pend.push_back(pd);
                                break;
                            }
                            case 0x31: {
                                uint32_t v[4];
                                vfS(fs, v);
                                Pend pd{ft, dest, {v[1], v[2], v[3], v[0]}, opt.fmacLatency};
                                pend.push_back(pd);
                                break;
                            }
                            case 0x34: {
                                uint32_t a = (vi[is] & 0x3ff) * 16u;
                                uint32_t v[4];
                                memcpy(v, mem + a, 16);
                                Pend pd{ft, dest, {v[0], v[1], v[2], v[3]}, 0};
                                pend.push_back(pd);
                                viW.push_back({is, (uint16_t)(vi[is] + 1)});
                                break;
                            }
                            case 0x35: {
                                Store s;
                                s.addr = (vi[it] & 0x3ff) * 16u;
                                vfS(fs, s.w);
                                s.lanes = dest;
                                stores.push_back(s);
                                viW.push_back({it, (uint16_t)(vi[it] + 1)});
                                break;
                            }
                            case 0x36: {
                                uint16_t nv = vi[is] - 1;
                                uint32_t a = (nv & 0x3ff) * 16u;
                                uint32_t v[4];
                                memcpy(v, mem + a, 16);
                                Pend pd{ft, dest, {v[0], v[1], v[2], v[3]}, 0};
                                pend.push_back(pd);
                                viW.push_back({is, nv});
                                break;
                            }
                            case 0x37: {
                                uint16_t nv = vi[it] - 1;
                                Store s;
                                s.addr = (nv & 0x3ff) * 16u;
                                vfS(fs, s.w);
                                s.lanes = dest;
                                stores.push_back(s);
                                viW.push_back({it, nv});
                                break;
                            }
                            case 0x38: {
                                uint32_t num = nrm(vf[fs][fsf]), den = nrm(vf[ft][ftf]);
                                uint32_t qv;
                                if ((den & 0x7fffffff) == 0)
                                    qv = ((num ^ den) & 0x80000000u) ? 0xff7fffffu : 0x7f7fffffu;
                                else {
                                    uint32_t fl;
                                    qv = res(f(num) / f(den), fl);
                                }
                                newQ = true;
                                newQv = qv;
                                break;
                            }
                            case 0x3b:
                                break;
                            case 0x3c:
                                viW.push_back({it, (uint16_t)vf[fs][fsf]});
                                break;
                            case 0x3d: {
                                uint32_t v = (uint32_t)(int32_t)(int16_t)vi[is];
                                Pend pd{ft, dest, {v, v, v, v}, opt.fmacLatency};
                                pend.push_back(pd);
                                break;
                            }
                            case 0x3e: {
                                uint32_t a = (vi[is] & 0x3ff) * 16u;
                                int c = dest & 8 ? 0 : dest & 4 ? 1 : dest & 2 ? 2 : 3;
                                uint32_t v;
                                memcpy(&v, mem + a + c * 4, 4);
                                viW.push_back({it, (uint16_t)v});
                                iluReady[it] = cycle + 4;
                                break;
                            }
                            case 0x3f: {
                                Store s;
                                s.addr = (vi[is] & 0x3ff) * 16u;
                                for (int c = 0; c < 4; ++c)
                                    s.w[c] = vi[it];
                                s.lanes = dest;
                                stores.push_back(s);
                                break;
                            }
                            case 0x64: {
                                Pend pd{ft, dest, {p, p, p, p}, opt.fmacLatency};
                                pend.push_back(pd);
                                break;
                            }
                            case 0x68:
                                viW.push_back({it, (uint16_t)(top & 0x3ff)});
                                break;
                            case 0x69:
                                viW.push_back({it, (uint16_t)(itop & 0x3ff)});
                                break;
                            case 0x6c:
                                kickAddr = vi[is] & 0x3ff;
                                break;
                            case 0x73: {
                                float x = nf(vf[fs][0]), y = nf(vf[fs][1]), z = nf(vf[fs][2]);
                                float len = std::sqrt(x * x + y * y + z * z);
                                float pv = len != 0 ? 1.0f / len : len;
                                uint32_t fl;
                                newP = true;
                                newPv = res(pv, fl);
                                newPLat = 24;
                                break;
                            }
                            default:
                                error = "unimplemented lower special " + std::to_string(f2);
                                ok = false;
                                break;
                        }
                        (void)ftf;
                    } else {
                        error = "unimplemented lower1 " + std::to_string(fn);
                        ok = false;
                    }
                    break;
                }
                default:
                    error = "unimplemented lower op " + std::to_string(op7);
                    ok = false;
                    break;
            }
            if (!ok)
                break;
        }
        // ---------- commit ----------
        for (auto& pd : pend) {
            if (!pd.reg)
                continue;
            for (int c = 0; c < 4; ++c)
                if (pd.lanes & (8 >> c)) {
                    vf[pd.reg][c] = pd.v[c];
                    vfReady[pd.reg][c] = cycle + pd.lat;
                }
        }
        for (int c = 0; c < 4; ++c)
            if (accW[c])
                acc[c] = accN[c];
        if (macW)
            flagPipe.push_back({cycle + (uint64_t)opt.flagLatency, macN, 0, true, false});
        if (clipW)
            flagPipe.push_back({cycle + (uint64_t)opt.flagLatency, 0, clipN, false, true});
        for (auto& s : stores)
            for (int c = 0; c < 4; ++c)
                if (s.lanes & (8 >> c))
                    memcpy(mem + s.addr + c * 4, &s.w[c], 4);
        for (auto& w : viW)
            writeVi(w.first, w.second);
        if (setI)
            i = newI;
        if (newQ) {
            qPend = true;
            qVal = newQv;
            qReady = cycle + opt.divLatency;
        }
        if (newP) {
            pPend = true;
            pVal = newPv;
            pReady = cycle + newPLat;
        }
        if (kickAddr >= 0) {
            kicks.push_back(kickAddr);
            kickPcs.push_back(pc);
            std::vector<uint8_t> pk;
            uint32_t a = kickAddr * 16;
            for (int g = 0; g < 4096; ++g) {
                uint64_t tag[2];
                memcpy(tag, mem + (a & 0x3fff), 16);
                uint32_t nloop = tag[0] & 0x7fff;
                bool eop = (tag[0] >> 15) & 1;
                uint32_t flg = (tag[0] >> 58) & 3, nreg = (tag[0] >> 60) & 15;
                if (!nreg)
                    nreg = 16;
                uint32_t qwc = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
                for (uint32_t k = 0; k < 1 + qwc; ++k) {
                    uint32_t o = (a + k * 16) & 0x3fff;
                    pk.insert(pk.end(), mem + o, mem + o + 16);
                }
                a += (1 + qwc) * 16;
                if (eop)
                    break;
            }
            packets.push_back(std::move(pk));
        }
        if (opt.trace)
            fprintf(stderr, "c%llu pc=%04x up=%08x lo=%08x vi1=%d\n", (unsigned long long)cycle, pc, up, lo, vi[1]);
        ++cycle;
        uint32_t cur = pc;
        if (delay) {
            if (branchTaken) {
                error = "branch in delay slot unsupported";
                ok = false;
                break;
            }
            pc = brPending ? brTarget : ((cur + 8) & 0x3ff8);
            if (ePending) {
                ended = true;
                break;
            }
            brPending = false;
            delay = 0;
            continue;
        }
        if (ebit) {
            ePending = true;
            delay = 1;
            pc = (cur + 8) & 0x3ff8;
            if (branchTaken) {
                brPending = true;
                brTarget = newTarget;
            }
            continue;
        }
        if (branchTaken) {
            delay = 1;
            brPending = true;
            brTarget = newTarget;
            pc = (cur + 8) & 0x3ff8;
            continue;
        }
        pc = (cur + 8) & 0x3ff8;
    }
    if (!ended && ok && error.empty()) {
        error = "pair budget exhausted";
        ok = false;
    }
    if (ended)
        flushAll();
    std::fesetround(oldRound);
    return ok && ended;
}
