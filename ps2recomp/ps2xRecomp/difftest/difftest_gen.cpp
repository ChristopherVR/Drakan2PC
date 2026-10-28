// Differential tester, part 1: generate the test cases.
//
//   ps2_difftest_gen <outdir> [--variants K] [--seed S] [--files N] [--elf SCES_500.06]
//
// Enumerates every R5900 instruction "shape" (primary opcode / SPECIAL funct / REGIMM rt / COP1 fmt+funct / MMI
// funct+sa / COP2 ...), builds K random encodings per shape (random register / immediate fields), wraps each in a tiny
// function (branches get a delay slot, markers and an in-function target) and runs the REAL recompiler emitter
// (CodeGenerator::generateFunction) on it. Output:
//   <outdir>/difftest_cases_N.cpp   the emitted C++ of every case + a table {words, name, fn}
//   <outdir>/difftest_shapes.txt    shape list, how many encodings were generated, and (with --elf) how often the
//   Drakan .text uses the shape <outdir>/difftest_unhandled.txt shapes the emitter turns into `throw` (unimplemented)
//   with the game usage count
// The runner (difftest_run.cpp) executes every case against a reference R5900 interpreter. See
// docs/reference/stability.md.
#include "ps2recomp/code_generator.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include "ps2recomp/types.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ps2recomp;

static constexpr uint32_t BASE = 0x00200000u;

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 11);
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

struct Shape {
    uint32_t fixed;  // discriminating bits
    uint32_t mask;   // bits that are fixed
    std::string key; // human readable key, also used for the usage histogram
};

static std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

static std::string shapeKeyOf(uint32_t w) {
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, f = w & 63, sa = (w >> 6) & 31;
    char b[64];
    if (op == 0) {
        std::snprintf(b, sizeof b, "SPECIAL.%02x", f);
        return b;
    }
    if (op == 1) {
        std::snprintf(b, sizeof b, "REGIMM.%02x", rt);
        return b;
    }
    if (op == 0x10) {
        if (rs == 16)
            std::snprintf(b, sizeof b, "COP0.CO.%02x", f);
        else
            std::snprintf(b, sizeof b, "COP0.%02x", rs);
        return b;
    }
    if (op == 0x11) {
        if (rs == 16 || rs == 20)
            std::snprintf(b, sizeof b, "COP1.%02x.%02x", rs, f);
        else if (rs == 8)
            std::snprintf(b, sizeof b, "COP1.BC.%02x", rt);
        else
            std::snprintf(b, sizeof b, "COP1.%02x", rs);
        return b;
    }
    if (op == 0x12) {
        if (rs >= 16) {
            if (f >= 0x3c)
                std::snprintf(b, sizeof b, "COP2.CO.%02x.%02x", f, sa);
            else
                std::snprintf(b, sizeof b, "COP2.CO.%02x", f);
        } else if (rs == 8)
            std::snprintf(b, sizeof b, "COP2.BC.%02x", rt);
        else
            std::snprintf(b, sizeof b, "COP2.%02x", rs);
        return b;
    }
    if (op == 0x1c) {
        if (f == 8 || f == 9 || f == 0x28 || f == 0x29 || f == 0x30)
            std::snprintf(b, sizeof b, "MMI.%02x.%02x", f, sa);
        else
            std::snprintf(b, sizeof b, "MMI.%02x", f);
        return b;
    }
    std::snprintf(b, sizeof b, "OP.%02x", op);
    return b;
}

static std::vector<Shape> enumerateShapes() {
    std::vector<Shape> v;
    auto add = [&](uint32_t fixed, uint32_t mask) { v.push_back({fixed, mask, shapeKeyOf(fixed)}); };
    for (uint32_t op = 0; op < 64; ++op) {
        if (op == 0)
            for (uint32_t f = 0; f < 64; ++f)
                add(f, 0xFC00003Fu);
        else if (op == 1)
            for (uint32_t rt = 0; rt < 32; ++rt)
                add((1u << 26) | (rt << 16), 0xFC1F0000u);
        else if (op == 0x10) {
            for (uint32_t rs = 0; rs < 32; ++rs) {
                if (rs == 16) {
                    for (uint32_t f = 0; f < 64; ++f)
                        add((op << 26) | (rs << 21) | f, 0xFFE0003Fu);
                } else
                    add((op << 26) | (rs << 21), 0xFFE00000u);
            }
        } else if (op == 0x11) {
            for (uint32_t rs = 0; rs < 32; ++rs) {
                if (rs == 16 || rs == 20) {
                    for (uint32_t f = 0; f < 64; ++f)
                        add((op << 26) | (rs << 21) | f, 0xFFE0003Fu);
                } else if (rs == 8) {
                    for (uint32_t rt = 0; rt < 4; ++rt)
                        add((op << 26) | (rs << 21) | (rt << 16), 0xFFFF0000u);
                } else
                    add((op << 26) | (rs << 21), 0xFFE00000u);
            }
        } else if (op == 0x12) {
            for (uint32_t rs = 0; rs < 32; ++rs) {
                if (rs >= 16) {
                    for (uint32_t f = 0; f < 64; ++f) {
                        if (f >= 0x3c) {
                            for (uint32_t sa = 0; sa < 32; ++sa)
                                add((op << 26) | (1u << 25) | (sa << 6) | f, 0xFE00003Fu | (31u << 6));
                        } else
                            add((op << 26) | (1u << 25) | f, 0xFE00003Fu);
                    }
                    break; // one CO block (bit 25 set): rs 16..31 are all "CO"
                } else if (rs == 8) {
                    for (uint32_t rt = 0; rt < 4; ++rt)
                        add((op << 26) | (rs << 21) | (rt << 16), 0xFFFF0000u);
                } else
                    add((op << 26) | (rs << 21), 0xFFE00000u);
            }
        } else if (op == 0x1c) {
            for (uint32_t f = 0; f < 64; ++f) {
                if (f == 8 || f == 9 || f == 0x28 || f == 0x29 || f == 0x30) {
                    for (uint32_t sa = 0; sa < 32; ++sa)
                        add((op << 26) | (sa << 6) | f, 0xFC0007FFu);
                } else
                    add((op << 26) | f, 0xFC00003Fu);
            }
        } else
            add(op << 26, 0xFC000000u);
    }
    return v;
}

// Usage histogram of the game's .text (first segment of the ELF, up to `limit`).
static std::map<std::string, uint32_t> scanElf(const std::string& path, uint32_t limit) {
    std::map<std::string, uint32_t> h;
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return h;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 64)
        return h;
    auto rd32 = [&](size_t o) {
        uint32_t x;
        std::memcpy(&x, &d[o], 4);
        return x;
    };
    auto rd16 = [&](size_t o) {
        uint16_t x;
        std::memcpy(&x, &d[o], 2);
        return x;
    };
    const uint32_t phoff = rd32(28);
    const uint16_t phentsize = rd16(42), phnum = rd16(44);
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t p = phoff + size_t(i) * phentsize;
        if (rd32(p) != 1)
            continue;
        const uint32_t off = rd32(p + 4), va = rd32(p + 8), fs = rd32(p + 16);
        for (uint32_t a = 0; a + 4 <= fs && va + a < limit; a += 4)
            h[shapeKeyOf(rd32(off + a))]++;
        break;
    }
    return h;
}

// ---- program construction
// -------------------------------------------------------------------------------------------------------------------
enum class Kind { Plain, CondBranch, J, JAL, JR, JALR, BC1, BC2 };

static Kind classify(uint32_t w) {
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, f = w & 63;
    switch (op) {
        case 2:
            return Kind::J;
        case 3:
            return Kind::JAL;
        case 4:
        case 5:
        case 6:
        case 7:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            return Kind::CondBranch;
        case 1:
            if (rt <= 3 || (rt >= 0x10 && rt <= 0x13))
                return Kind::CondBranch;
            return Kind::Plain;
        case 0:
            if (f == 8)
                return Kind::JR;
            if (f == 9)
                return Kind::JALR;
            return Kind::Plain;
        case 0x11:
            if (rs == 8)
                return Kind::BC1;
            return Kind::Plain;
        case 0x12:
            if (rs == 8)
                return Kind::BC2;
            return Kind::Plain;
        default:
            return Kind::Plain;
    }
}

static constexpr uint32_t ADDIU_K0 =
    (0x09u << 26) | (26u << 21) | (26u << 16) | 1u; // addiu k0,k0,1  (fall-through marker)
static constexpr uint32_t ADDIU_K1_1 = (0x09u << 26) | (27u << 21) | (27u << 16) | 1u; // addiu k1,k1,1
static constexpr uint32_t ADDIU_K1_2 = (0x09u << 26) | (27u << 21) | (27u << 16) | 2u; // addiu k1,k1,2  (target marker)
static constexpr uint32_t ADDIU_S6 =
    (0x09u << 26) | (22u << 21) | (22u << 16) | 1u; // addiu s6,s6,1  (delay-slot marker)
static constexpr uint32_t NOP = 0;

struct Case {
    std::string name;
    std::vector<uint32_t> words;
    std::string shape;
};

static void buildCases(const Shape& sh, Rng& rng, int variants, std::vector<Case>& out) {
    for (int k = 0; k < variants; ++k) {
        uint32_t w = sh.fixed | (rng.next() & ~sh.mask);
        if ((w >> 26) == 0x11 && (((w >> 21) & 31) == 2 || ((w >> 21) & 31) == 6))
            w = (w & ~(31u << 11)) | (((k % 2) ? 31u : 0u) << 11); // CFC1 / CTC1: only FCR0 and FCR31 exist
        if ((w >> 26) == 0x12 && (w & (1u << 25))) {
            // VU0 integer registers are 4 bits wide (VI0..VI15): clear the 5th bit of the fields that name a VI
            // register
            const uint32_t f6 = w & 63, fd = (w >> 6) & 31;
            if (f6 >= 0x30 && f6 <= 0x35 && f6 != 0x33)
                w &= ~((1u << 20) | (1u << 15) | (f6 == 0x32 ? 0u : (1u << 10)));
            else if (f6 >= 0x3C && fd == 15)
                w &= ~((1u << 20) | (1u << 15)); // MTIR / MFIR / ILWR / ISWR
        }
        const Kind kind = classify(w);
        Case c;
        c.shape = sh.key;
        switch (kind) {
            case Kind::Plain:
                c.words = {w};
                break;
            case Kind::CondBranch:
            case Kind::BC1:
            case Kind::BC2: {
                w = (w & 0xFFFF0000u) | 3u; // target = BASE + 4 + 3*4 = BASE + 16
                // delay slot variants: nop, marker, ALU op that overwrites a compared register
                const uint32_t rs = (w >> 21) & 31, rt = (w >> 16) & 31;
                uint32_t delay;
                switch (k % 4) {
                    case 0:
                        delay = NOP;
                        break;
                    case 1:
                        delay = ADDIU_S6;
                        break;
                    case 2:
                        delay = (0x0Du << 26) | (0u << 21) | ((rs ? rs : 5) << 16) | (rng.next() & 0xFFFF);
                        break; // ori rs,zero,imm  (changes the compared reg)
                    default:
                        delay = (0x09u << 26) | (0u << 21) | ((rt ? rt : 6) << 16) | (rng.next() & 0xFFFF);
                        break; // addiu rt,zero,imm
                }
                if (kind != Kind::CondBranch)
                    delay = (k % 2) ? ADDIU_S6 : NOP;
                c.words = {w, delay, ADDIU_K0, ADDIU_K1_1, ADDIU_K1_2};
                break;
            }
            case Kind::J:
                c.words = {(2u << 26) | (((BASE + 16u) >> 2) & 0x03FFFFFFu), (k % 2) ? ADDIU_S6 : NOP, ADDIU_K0,
                           ADDIU_K1_1, ADDIU_K1_2};
                break;
            case Kind::JAL:
                // external target: the emitter takes the dispatch path; the test runtime treats the call as "returns
                // immediately"
                c.words = {
                    (3u << 26) | (((BASE + 0x1000u) >> 2) & 0x03FFFFFFu),
                    (k % 3 == 0)
                        ? NOP
                        : (k % 3 == 1 ? ADDIU_S6 : ((0x09u << 26) | (31u << 21) | (8u << 16) | 4u)) /* addiu t0,ra,4 */,
                    ADDIU_K0, ADDIU_K1_1};
                break;
            case Kind::JR:
            case Kind::JALR: {
                // register holds a target; the harness loads BASE+16 or BASE+0x1000 into rs by decoding the word
                const uint32_t rs = (w >> 21) & 31;
                w &= ~(31u << 16); // rt = 0 (reserved)
                uint32_t delay = (k % 3 == 0)
                                     ? NOP
                                     : (k % 3 == 1 ? ADDIU_S6
                                                   : ((0x09u << 26) | ((rs ? rs : 7) << 21) | ((rs ? rs : 7) << 16) |
                                                      8u)); // addiu rs,rs,8  (target already read)
                c.words = {w, delay, ADDIU_K0, ADDIU_K1_1, ADDIU_K1_2};
                break;
            }
        }
        char nm[96];
        std::snprintf(nm, sizeof nm, "%s_%d", sh.key.c_str(), k);
        c.name = nm;
        out.push_back(std::move(c));
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <outdir> [--variants K] [--seed S] [--files N] [--elf path]\n", argv[0]);
        return 2;
    }
    const std::string outdir = argv[1];
    int variants = 24, files = 8;
    uint64_t seed = 1;
    std::string elf;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--variants")
            variants = std::atoi(argv[i + 1]);
        else if (a == "--seed")
            seed = std::strtoull(argv[i + 1], nullptr, 0);
        else if (a == "--files")
            files = std::atoi(argv[i + 1]);
        else if (a == "--elf")
            elf = argv[i + 1];
    }
    Rng rng(seed);
    if (variants <= 0 || files <= 0) {
        std::fprintf(stderr, "--variants and --files must be positive\n");
        return 2;
    }
    const std::vector<Shape> shapes = enumerateShapes();
    const std::map<std::string, uint32_t> usage =
        elf.empty() ? std::map<std::string, uint32_t>() : scanElf(elf, 0x4c8000);

    std::vector<Case> cases;
    for (const Shape& sh : shapes)
        buildCases(sh, rng, variants, cases);

    std::vector<Symbol> syms;
    std::vector<Section> secs;
    R5900Decoder decoder;
    std::vector<std::ofstream> outs;
    for (int i = 0; i < files; ++i) {
        outs.emplace_back(outdir + "/difftest_cases_" + std::to_string(i) + ".cpp");
        if (!outs.back()) {
            std::fprintf(stderr, "Cannot create generated case files in %s\n", outdir.c_str());
            return 2;
        }
        outs.back() << "#include \"difftest_common.h\"\n\n";
    }
    std::ofstream tbl(outdir + "/difftest_table.cpp");
    if (!tbl)
        return 2;
    tbl << "#include \"difftest_common.h\"\n";
    std::set<std::string> unhandled;
    std::map<std::string, int> perShape;
    std::vector<std::string> decls, rows;
    size_t emitted = 0;
    for (size_t ci = 0; ci < cases.size(); ++ci) {
        const Case& c = cases[ci];
        CodeGenerator cg(syms, secs);
        cg.setEmitInstructionComments(false);
        cg.setAlwaysWritePc(false);
        const std::string fname = "tcase_" + std::to_string(ci);
        cg.setRenamedFunctions({{BASE, fname}});
        Function fn;
        fn.name = fname;
        fn.start = BASE;
        fn.end = BASE + 4u * static_cast<uint32_t>(c.words.size());
        std::vector<Instruction> insts;
        for (size_t i = 0; i < c.words.size(); ++i)
            insts.push_back(decoder.decodeInstruction(BASE + 4u * static_cast<uint32_t>(i), c.words[i], false));
        std::string code;
        try {
            code = cg.generateFunction(fn, insts, false);
        } catch (const std::exception& e) {
            // Keep the case failing if its reference is modelled; a no-op can accidentally pass.
            code = "void " + fname +
                   "(uint8_t*, R5900Context*, PS2Runtime*) { throw std::runtime_error(\"emitter failed\"); }\n";
            unhandled.insert(c.shape + " (emitter exception)");
        }
        if (code.find("throw std::runtime_error") != std::string::npos)
            unhandled.insert(c.shape);
        // the emitter may name the function through the symbol table; make sure the name we use exists
        outs[ci % files] << code << "\n";
        decls.push_back("void " + fname + "(uint8_t* __restrict, R5900Context* __restrict, PS2Runtime*);");
        std::string r = "    {\"" + c.name + "\", \"" + c.shape + "\", " + std::to_string(c.words.size()) + ", {";
        for (size_t i = 0; i < c.words.size(); ++i)
            r += (i ? ",0x" : "0x") + hex8(c.words[i]);
        r += "}, " + fname + "},";
        rows.push_back(r);
        perShape[c.shape]++;
        ++emitted;
    }
    for (const auto& d : decls)
        tbl << d << "\n";
    tbl << "\nconst DtCase g_dtCases[] = {\n";
    for (const auto& r : rows)
        tbl << r << "\n";
    tbl << "};\nconst size_t g_dtCaseCount = " << rows.size() << ";\n";

    std::ofstream sh(outdir + "/difftest_shapes.txt");
    for (const Shape& s : shapes) {
        auto it = usage.find(s.key);
        sh << s.key << " cases=" << perShape[s.key] << " game_uses=" << (it == usage.end() ? 0 : it->second) << "\n";
    }
    std::ofstream un(outdir + "/difftest_unhandled.txt");
    for (const auto& k : unhandled) {
        auto it = usage.find(k.substr(0, k.find(' ')));
        un << k << " game_uses=" << (it == usage.end() ? 0 : it->second) << "\n";
    }
    std::printf("difftest_gen: %zu shapes, %zu cases, %zu unhandled shapes\n", shapes.size(), emitted,
                unhandled.size());
    return 0;
}
