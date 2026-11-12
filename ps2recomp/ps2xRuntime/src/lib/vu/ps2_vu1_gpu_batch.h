#pragma once
// Vertex-program draws from the batch path of the game's VU1 microprogram (gsr/vertex_batch.h). Header only, and free
// of the interpreter: it works on the VU data memory, the seven registers the batch set-up loads and the pc.
//
// A batch is one MSCAL (set-up) followed by MSCNTs that each run one stage, chosen by the header quadword 9 and the
// flag word in quadword 7, which the EE rewrites between the MSCNTs: vertex passes that work in place on three arrays
// of 225 vertex slots, and index-list runs that copy processed vertices into strip packets. The Tracker follows the
// runs of a VU1 worker:
//
//   * a vertex pass it can express is not executed: its constants are kept as a pending pass on the slots it covers,
//     and the lanes it would have written are marked virtual (VU memory still holds what was there before);
//   * an index-list run it can express is not executed either: it becomes one record with the raw rows of the
//     vertices it draws, the pending passes those rows need and the strips as indices;
//   * anything else is executed by the microprogram after the pending passes of every slot were applied to VU memory
//     with the CPU evaluation (materialise), which writes the bits the microprogram would have written;
//   * a write of the EE into a slot (VIF unpack, reported through noteWrite) makes the written lanes real again, and
//     pending passes that read a lane the EE has replaced are dropped with their results;
//   * at the next set-up, lanes that are still virtual are marked stale: VU memory does not hold what the microprogram
//     would have left there. Stale lanes are cleared by the next write to them. A run that would draw from a stale
//     lane cannot be reproduced; it is counted (staleDraws) and has not been observed. The last slot is the exception:
//     it is brought up to date instead, because a clip-code index list reads its alpha lane whatever the batch drew.
//
// Every decision is taken per run from the `families` mask of that call, every decline has a Reason and a counter.
#include "gsr/vertex_batch.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace vu1gpu::batch {
using gsr::vp::BatchVertex;
using gsr::vp::Pass;
using gsr::vp::Row;

// Data layout of the batch path (quadword addresses in VU data memory).
inline constexpr uint32_t kPos = 66, kCol = 291, kSt = 516, kSlots = 225; // the three arrays
inline constexpr uint32_t kBuffer = 773, kMemory = 1024;                  // packet buffer up to the end of memory
inline constexpr uint32_t kQwFlags = 7, kQwColour = 8, kQwHeader = 9, kQwFade = 10, kQwTag = 11;
inline constexpr uint32_t kQwLightDir = 13, kQwLightColour = 17, kQwTexMatrix = 23, kQwGrid = 43;
inline constexpr uint32_t kPcSetup = 0x0000, kPcAfterSetup = 0x01F0, kPcAfterVertex = 0x0E70;
inline constexpr uint32_t kPcAfterList = 0x1018, kPcAfterClipList = 0x1580;
inline constexpr uint32_t kMaxChain = 24; // pending passes of one slot

// Lanes of a slot: bits 0..3 position x y z w, 4..7 colour, 8..11 texture row.
inline constexpr uint16_t kLanePos = 0x00F, kLaneCol = 0x0F0, kLaneAlpha = 0x080, kLaneRgb = 0x070;
inline constexpr uint16_t kLaneStXy = 0x300, kLaneStZ = 0x400, kLaneStW = 0x800, kLaneSt = 0xF00, kLaneY = 0x002;

// Stage families, each with its own enable bit (the mask is read on every run).
enum Family : uint32_t {
    FamStrips = 1u << 0,         // the strip stage (ps2_vu1_gpu_program.h)
    FamFade = 1u << 1,           // depth fade of alpha
    FamTexMatrix = 1u << 2,      // texture matrix
    FamGrid = 1u << 3,           // grid generator
    FamPointLight = 1u << 4,     // point light
    FamTexGen = 1u << 5,         // texture coordinates from positions or integers
    FamLightTransform = 1u << 6, // directional lighting and the transform
    FamPrimitive = 1u << 7,      // index-list runs without clip codes
    FamClipInside = 1u << 8,     // index-list runs of a batch with clip codes whose vertices are all inside
    FamAll = 0x1FFu,
};

enum class Reason : uint32_t {
    None,
    Disabled,       // a stage family of the run is switched off
    EngineState,    // the engine cannot be entered at a run boundary (pending pipelines, other microcode)
    ClipCodes,      // a vertex pass that writes clip codes (flag 1)
    Clipper,        // an index-list run with a vertex outside the view volume
    HeaderShape,    // base or count of the run outside the vertex window
    GridShape,      // grid generator not on the run's own slots, or a grid that leaves the vertex window
    LightExponent,  // point light with more squarings than a record expresses
    TagTemplate,    // the strip tag is not the packed four-register layout
    IndexList,      // index list outside VU memory, or an index beyond the vertex window
    CornerSource,   // per-corner texture coordinate or colour taken from lanes a pending pass produces
    BufferOverflow, // the strips do not fit the packet buffer
    ChainLength,    // more passes than a record or a slot holds
    StaleInput,     // the run reads lanes whose real content is not known
    Count
};
inline const char* reasonName(Reason r) {
    static const char* const names[] = {"none",         "family disabled", "engine state",  "clip codes",
                                        "clipper",      "header shape",    "grid shape",    "light exponent",
                                        "tag template", "index list",      "corner source", "packet buffer",
                                        "chain length", "stale input"};
    return static_cast<uint32_t>(r) < static_cast<uint32_t>(Reason::Count) ? names[static_cast<uint32_t>(r)] : "?";
}

enum class Stage : uint32_t { Setup, Vertex, List, ClipList, Other, Count };

struct Counters {
    uint64_t runs[static_cast<uint32_t>(Stage::Count)] = {};
    uint64_t taken[static_cast<uint32_t>(Stage::Count)] = {};
    uint64_t declined[static_cast<uint32_t>(Reason::Count)] = {};
    uint64_t passesDeferred = 0, slotPassesDeferred = 0; // stages, and stage applications to a slot, not executed
    uint64_t slotsMaterialised = 0, slotPassesMaterialised = 0;
    uint64_t passesDropped = 0; // pending slot passes whose input or result the EE replaced
    uint64_t records = 0, recordVertices = 0, recordIndices = 0, recordPasses = 0;
    uint64_t staleLanes = 0, staleDraws = 0; // lanes left virtual at a set-up; executed runs drawing a stale lane
};

inline int16_t word16(const uint8_t* data, uint32_t qw, uint32_t lane) {
    int16_t v;
    std::memcpy(&v, data + size_t(qw) * 16u + size_t(lane) * 4u, 2);
    return v;
}
inline void loadRow(const uint8_t* data, uint32_t slot, Row& r) {
    std::memcpy(r.pos, data + size_t(kPos + slot) * 16u, 16);
    std::memcpy(r.col, data + size_t(kCol + slot) * 16u, 16);
    std::memcpy(r.st, data + size_t(kSt + slot) * 16u, 16);
}

// Lanes a stage reads and writes on its own slot.
inline uint16_t readLanes(uint32_t kind) {
    switch (kind) {
        case gsr::vp::PassGrid:
            return kLaneY;
        case gsr::vp::PassGridFlat:
            return 0;
        case gsr::vp::PassPointLight:
            return kLanePos | kLaneRgb;
        case gsr::vp::PassTexGenInt:
        case gsr::vp::PassLight:
            return kLaneCol;
        case gsr::vp::PassTexGenPos:
        case gsr::vp::PassTransform:
        case gsr::vp::PassProject:
            return kLanePos;
        case gsr::vp::PassFade:
            return kLaneAlpha | kLaneStZ;
        case gsr::vp::PassTexMatrix:
            return kLaneStXy;
        default:
            return 0;
    }
}
inline uint16_t writeLanes(uint32_t kind) {
    switch (kind) {
        case gsr::vp::PassGrid:
        case gsr::vp::PassGridFlat:
            return kLanePos;
        case gsr::vp::PassPointLight:
        case gsr::vp::PassLight:
            return kLaneCol;
        case gsr::vp::PassTexGenInt:
        case gsr::vp::PassTexGenPos:
            return kLaneSt;
        case gsr::vp::PassTransform:
        case gsr::vp::PassProject:
            return kLanePos | kLaneStZ;
        case gsr::vp::PassFade:
            return kLaneAlpha;
        case gsr::vp::PassTexMatrix:
            return kLaneStXy;
        default:
            return 0;
    }
}
inline uint32_t familyOf(uint32_t kind) {
    switch (kind) {
        case gsr::vp::PassGrid:
        case gsr::vp::PassGridFlat:
            return FamGrid;
        case gsr::vp::PassPointLight:
            return FamPointLight;
        case gsr::vp::PassTexGenInt:
        case gsr::vp::PassTexGenPos:
            return FamTexGen;
        case gsr::vp::PassLight:
        case gsr::vp::PassTransform:
            return FamLightTransform;
        case gsr::vp::PassFade:
            return FamFade;
        case gsr::vp::PassTexMatrix:
            return FamTexMatrix;
        default:
            return 0;
    }
}

// The stages one vertex run (header quadword 9 with z <= 0) executes, in order, as the dispatcher at 0x01F0 selects
// them from the flag word.
struct VertexRun {
    Reason why = Reason::None;
    uint32_t count = 0;            // stages
    uint32_t kinds[5] = {};        // PassKind
    uint32_t first = 0, slots = 0; // slot range of the stages (the grid has its own)
    uint32_t gridFirst = 0, gridCols = 0, gridRows = 0;
    bool gridFlat = false;
    uint32_t exponent = 0;
    uint32_t families = 0;
    bool clipCodes = false;
};
inline VertexRun classifyVertexRun(const uint8_t* data) {
    using namespace gsr::vp;
    VertexRun v;
    const uint32_t flags = static_cast<uint16_t>(word16(data, kQwFlags, 0));
    const int32_t base = word16(data, kQwHeader, 0), n = word16(data, kQwHeader, 1);
    v.clipCodes = (flags & 1u) != 0u;
    const bool texMatrix = (flags & 0x13C0u) != 0u && (flags & 0x80u) == 0u && (flags & 0x200u) != 0u;
    const int32_t origin = texMatrix ? static_cast<int32_t>(kSt) : static_cast<int32_t>(kPos);
    if (n < 1 || base < origin || base - origin + n > static_cast<int32_t>(kSlots)) {
        v.why = Reason::HeaderShape;
        return v;
    }
    v.first = static_cast<uint32_t>(base - origin);
    v.slots = static_cast<uint32_t>(n);
    const auto push = [&v](uint32_t kind) {
        v.kinds[v.count++] = kind;
        v.families |= familyOf(kind);
    };
    if ((flags & 0x13C0u) != 0u) {
        if (flags & 0x80u) {
            push(PassFade);
            if (v.clipCodes)
                v.why = Reason::ClipCodes;
            return v;
        }
        if (flags & 0x200u) {
            push(PassTexMatrix);
            if (v.clipCodes) // batches with clip codes are left to the microprogram as a whole
                v.why = Reason::ClipCodes;
            return v;
        }
        bool pointLight = false;
        if (flags & 0x1000u) {
            const int32_t cols = word16(data, kQwGrid, 0), rows = word16(data, kQwGrid, 1);
            const int32_t dest = word16(data, kQwGrid, 2);
            if (dest != base || cols < 1 || rows < 1 || cols > static_cast<int32_t>(kSlots) ||
                rows > static_cast<int32_t>(kSlots) || base - origin + cols * rows > static_cast<int32_t>(kSlots)) {
                v.why = Reason::GridShape;
                return v;
            }
            v.gridFirst = v.first;
            v.gridCols = static_cast<uint32_t>(cols);
            v.gridRows = static_cast<uint32_t>(rows);
            v.gridFlat = (flags & 0x2000u) != 0u;
            push(v.gridFlat ? PassGridFlat : PassGrid);
            // The test is on a signed 16-bit value: bit 15 switches the point light off.
            pointLight = static_cast<int16_t>(flags & 0xC000u) > 0;
            if (pointLight) {
                const int32_t e = word16(data, 20, 0);
                if (e > static_cast<int32_t>(kBatchMaxExponent)) {
                    v.why = Reason::LightExponent;
                    return v;
                }
                v.exponent = e > 0 ? static_cast<uint32_t>(e) : 0u;
                push(PassPointLight);
            }
        }
        const bool tryIntegers = (flags & 0x1000u) == 0u || pointLight;
        if (tryIntegers && (flags & 0x100u))
            push(PassTexGenInt);
        else if (flags & 0x40u)
            push(PassTexGenPos);
    }
    if (flags & 0x10u)
        push(PassLight);
    push(PassTransform);
    if (v.clipCodes)
        v.why = Reason::ClipCodes;
    return v;
}

// The constants of a stage, from VU memory and the registers the batch set-up loaded (vf: VF00.., four floats each).
inline Pass makePass(uint32_t kind, const VertexRun& run, const uint8_t* data, const float (*vf)[4]) {
    using namespace gsr::vp;
    Pass p{};
    p.kind = kind;
    p.first = run.first;
    p.count = run.slots;
    const auto row = [data, &p](uint32_t to, uint32_t qw, uint32_t rows = 1) {
        std::memcpy(p.k[to], data + size_t(qw) * 16u, size_t(rows) * 16u);
    };
    switch (kind) {
        case PassGrid:
        case PassGridFlat: {
            row(0, kQwGrid + 1);
            row(1, kQwGrid + 2);
            p.param = run.gridCols;
            p.first = run.gridFirst;
            p.count = run.gridCols * run.gridRows;
            break;
        }
        case PassPointLight:
            row(0, 13);
            row(1, 14);
            row(2, 17);
            row(3, 18);
            row(4, 19);
            p.param = run.exponent;
            break;
        case PassTexGenInt:
        case PassTexGenPos:
            row(0, kQwTexMatrix, 4);
            break;
        case PassLight:
            row(0, kQwColour);
            row(1, kQwLightDir, 4);
            row(5, kQwLightColour, 4);
            break;
        case PassTransform:
            std::memcpy(p.k[0], vf[1], 64);
            std::memcpy(p.k[4], vf[5], 48);
            break;
        case PassProject:
            std::memcpy(p.k[4], vf[5], 48);
            break;
        case PassFade:
            row(0, kQwFade);
            break;
        case PassTexMatrix:
            row(0, kQwTexMatrix, 2);
            row(2, kQwTexMatrix + 3);
            break;
        default:
            break;
    }
    return p;
}

// The accumulated x of every column and z of every row of a grid pass (gsr::vp::gridAxis).
struct GridAxes {
    uint32_t x[kSlots], z[kSlots];
    void set(const Pass& p) {
        const uint32_t cols = p.param, rows = (p.count + cols - 1u) / cols;
        gsr::vp::gridAxis(p.k[0][0], p.k[1][0], cols, x);
        gsr::vp::gridAxis(p.k[0][2], p.k[1][2], rows, z);
    }
};
inline bool isGrid(uint32_t kind) {
    return kind == gsr::vp::PassGrid || kind == gsr::vp::PassGridFlat;
}

// One index of an index-list run, resolved to the slots its three rows come from.
struct Corner {
    uint16_t slot, stSlot, colSlot;
};
struct IndexRun {
    Reason why = Reason::None;
    uint32_t prim = 0;
    bool clip = false;     // the batch carries clip codes
    bool emptyTag = false; // the run ends with the empty tag
    uint32_t stripCount = 0, cornerCount = 0;
    uint32_t strips[gsr::vp::kBatchMaxIndices] = {};
    Corner corners[gsr::vp::kBatchMaxIndices] = {};
    // What the clipper variant's first walk stores: per strip entry the OR and AND of the clip codes, their OR.
    uint32_t entryCount = 0, allOr = 0;
    uint16_t entries[17][2] = {};
    uint16_t seedQw[17] = {}; // quadword whose lane w an end index contributed as a code
};
// Walks the index list like the microprogram: four 16-bit indices per quadword; an index above zero is a vertex, any
// other ends the strip (and the run, when the strip is empty). Vertices after the last end are never sent.
inline IndexRun walkIndexList(const uint8_t* data) {
    IndexRun r;
    const uint32_t flags = static_cast<uint16_t>(word16(data, kQwFlags, 0));
    const int32_t list = word16(data, kQwHeader, 0), quads = word16(data, kQwHeader, 1);
    const int32_t stream = word16(data, kQwHeader, 2), face = word16(data, kQwHeader, 3);
    r.clip = (flags & 1u) != 0u;
    uint64_t tagLo, tagHi;
    std::memcpy(&tagLo, data + size_t(kQwTag) * 16u, 8);
    std::memcpy(&tagHi, data + size_t(kQwTag) * 16u + 8, 8);
    if ((tagLo & 0xFFFFFFFFull) != 0x8000ull || ((tagLo >> 58) & 3u) != 0u || ((tagLo >> 60) & 15u) != 4u ||
        ((tagLo >> 46) & 1u) != 1u || (tagHi & 0xFFFFu) != 0x5A12u) {
        r.why = Reason::TagTemplate;
        return r;
    }
    r.prim = static_cast<uint32_t>((tagLo >> 47) & 0x7FFu);
    if (quads < 1 || list < 0 || list + quads > static_cast<int32_t>(kMemory)) {
        r.why = Reason::IndexList;
        return r;
    }
    uint32_t pending = 0, bufferQuads = 0;
    bool ended = false, anyOutside = false;
    uint32_t allOutside = 0u;
    if (r.clip) {
        // The clipper variant first walks the whole list. It keeps the OR and the AND of the clip codes per strip
        // entry in quadword 27 and up, and the first index after an end starts the next entry with the "code" of the
        // end index itself (lane w of quadword 515 + index, for index 0 the alpha of the last colour slot). The exit
        // test for sixteen entries reads the entry counter one instruction late, so seventeen are stored and
        // the last lands in the grid header (quadword 43). The run sends nothing when the AND over the entries
        // (the last stored one left out) is above zero.
        uint32_t entryOr = 0, entryAnd = 0xFFFu, allAnd = 0xFFFu;
        for (int32_t j = 0; j < quads * 4; ++j) {
            const int32_t index = word16(data, static_cast<uint32_t>(list + j / 4), static_cast<uint32_t>(j & 3));
            if (index > static_cast<int32_t>(kSlots)) {
                r.why = Reason::IndexList;
                return r;
            }
            const uint32_t codeQw = static_cast<uint32_t>(515 + index) & (kMemory - 1u);
            const uint32_t code = static_cast<uint16_t>(word16(data, codeQw, 3));
            if (index <= 0) {
                if (r.entryCount < 17u) {
                    r.entries[r.entryCount][0] = static_cast<uint16_t>(entryOr);
                    r.entries[r.entryCount][1] = static_cast<uint16_t>(entryAnd);
                    r.seedQw[r.entryCount] = static_cast<uint16_t>(codeQw);
                    ++r.entryCount;
                    r.allOr |= entryOr;
                    if (r.entryCount < 17u) {
                        allAnd &= entryAnd;
                        entryOr = code;
                        entryAnd = 0xFFFu & code;
                    }
                }
                continue;
            }
            // Any vertex of the list outside the view volume sends the run through the clipper.
            anyOutside = anyOutside || code != 0u;
            if (r.entryCount < 17u) {
                entryOr |= code;
                entryAnd &= code;
            }
        }
        allOutside = static_cast<int16_t>(allAnd) > 0 ? 1u : 0u;
    }
    for (int32_t j = 0; j < quads * 4 && !ended; ++j) {
        const int32_t index = word16(data, static_cast<uint32_t>(list + j / 4), static_cast<uint32_t>(j & 3));
        if (index <= 0) {
            if (pending == 0u) {
                ended = true;
                break;
            }
            r.strips[r.stripCount++] = pending;
            bufferQuads += 1u + 4u * pending;
            pending = 0;
            continue;
        }
        const int32_t stSlot = (flags & 0x20u) ? index - 1 : stream + j - 1;
        const int32_t colSlot = (flags & 4u) ? index - 1 : face + j / 4;
        if (index > static_cast<int32_t>(kSlots) || stSlot < 0 || stSlot >= static_cast<int32_t>(kSlots) ||
            colSlot < 0 || colSlot >= static_cast<int32_t>(kSlots) || r.cornerCount >= gsr::vp::kBatchMaxIndices) {
            r.why = Reason::IndexList;
            return r;
        }
        r.corners[r.cornerCount++] =
            Corner{static_cast<uint16_t>(index - 1), static_cast<uint16_t>(stSlot), static_cast<uint16_t>(colSlot)};
        ++pending;
    }
    r.cornerCount -= pending; // the tail without an end is written to the buffer but not sent
    if (kBuffer + bufferQuads + (pending != 0u ? 1u + 4u * pending : 0u) > kMemory) {
        r.why = Reason::BufferOverflow;
        return r;
    }
    if (r.clip) {
        if (anyOutside || r.stripCount > 17u) {
            r.why = Reason::Clipper;
            return r;
        }
        // All vertices inside: the strips of the unclipped variant, unless the run is rejected as a whole.
        if (allOutside != 0u) {
            r.stripCount = 0;
            r.cornerCount = 0;
        } else {
            r.emptyTag = true;
        }
    }
    return r;
}

enum class Action : uint32_t {
    Run,  // execute the run with the microprogram (VU memory was brought up to date)
    Skip, // do not execute; pc was set to where the run would have stopped; `packet` may hold a record to send
};

class Tracker {
public:
    Counters counters;

    // An EE write into VU memory: `lanes` bit 0..3 = x y z w of quadword `qw`.
    void noteWrite(uint32_t qw, uint32_t lanes) {
        if (qw < kPos || qw >= kSt + kSlots)
            return;
        const uint32_t array = (qw - kPos) / kSlots, slot = (qw - kPos) % kSlots;
        writeSlot(slot, static_cast<uint16_t>((lanes & 15u) << (array * 4u)));
    }

    // A run of whole quadwords [first, first + count) written with the same lanes (the VIF's fast unpack path).
    void noteWrites(uint32_t first, uint32_t count, uint32_t lanes) {
        const uint32_t end = first + count;
        for (uint32_t array = 0; array < 3u; ++array) { // the part of the range in each of the three arrays
            const uint32_t base = kPos + array * kSlots;
            const uint32_t from = first > base ? first : base, to = end < base + kSlots ? end : base + kSlots;
            const uint16_t mask = static_cast<uint16_t>((lanes & 15u) << (array * 4u));
            for (uint32_t qw = from; qw < to; ++qw) {
                const uint32_t slot = qw - base;
                if (m_chainLength[slot] == 0u)
                    m_stale[slot] &= static_cast<uint16_t>(~mask);
                else
                    writeSlot(slot, mask);
            }
        }
    }

    // A write whose value is not the EE's own data (an unpack mode that adds to what the lane held): the lanes'
    // content is not known any more.
    void noteUnknown(uint32_t qw, uint32_t lanes) {
        if (qw < kPos || qw >= kSt + kSlots)
            return;
        const uint32_t array = (qw - kPos) / kSlots, slot = (qw - kPos) % kSlots;
        const uint16_t mask = static_cast<uint16_t>((lanes & 15u) << (array * 4u));
        writeSlot(slot, mask);
        m_stale[slot] |= mask;
    }

    // True when VU memory is the microprogram's in every lane: nothing pending, nothing stale. Only then may the
    // tracker stop following the runs and the EE's writes (and start again later from this same state).
    bool clean() const {
        if (m_anyPending)
            return false;
        uint32_t any = 0;
        for (uint32_t s = 0; s < kSlots; ++s)
            any |= m_stale[s] | m_virtual[s];
        return any == 0u;
    }

    // Stage families of the run the last step() decided on (a vertex run: one bit per stage it executes).
    uint32_t lastFamilies() const { return m_lastFamilies; }

    // Called before a run starts. resume: MSCNT; pc: the start address or the address the run resumes at; vf: the
    // float registers; data: VU data memory (written when pending passes are materialised); engineOk: the engine can
    // be entered at this boundary. Sets `pc` and returns Skip when the run must not be executed.
    Action step(bool resume, uint32_t& pc, const float (*vf)[4], uint8_t* data, uint32_t families, bool engineOk,
                std::vector<uint8_t>& packet, Reason* why = nullptr) {
        m_recordBuilt = false; // the packet keeps its storage; it is only emptied when no record was written
        Reason reason = Reason::None;
        const Action a = decide(resume, pc, vf, data, families, engineOk, packet, reason);
        if (!m_recordBuilt)
            packet.clear();
        if (why)
            *why = reason;
        if (reason != Reason::None)
            ++counters.declined[static_cast<uint32_t>(reason)];
        return a;
    }

    // Applies every pending pass to VU memory.
    void materialiseAll(uint8_t* data) {
        if (!m_anyPending)
            return;
        for (uint32_t s = 0; s < kSlots; ++s)
            if (m_chainLength[s] != 0u)
                materialiseSlot(s, data);
        m_anyPending = false;
    }

    // True when a quadword of [first, first + count) has a lane whose real content is not in VU memory (pending or
    // stale): a run that reads them from memory (the strip stage, anything this tracker does not follow) must not
    // be turned into a record from memory, and executing it needs materialiseAll first.
    bool unknownLanes(uint32_t first, uint32_t count) const {
        for (uint32_t qw = first < kPos ? kPos : first; qw < first + count && qw < kSt + kSlots; ++qw)
            if ((((m_virtual[(qw - kPos) % kSlots] | m_stale[(qw - kPos) % kSlots]) >> (((qw - kPos) / kSlots) * 4u)) &
                 15u) != 0u)
                return true;
        return false;
    }

    // The pending or stale lanes (bit 0..3) of quadword `qw` among `lanes`.
    uint32_t lanesOf(uint32_t qw, uint32_t lanes) const {
        if (qw < kPos || qw >= kSt + kSlots)
            return 0;
        return ((m_virtual[(qw - kPos) % kSlots] | m_stale[(qw - kPos) % kSlots]) >> (((qw - kPos) / kSlots) * 4u)) &
               lanes;
    }

    uint16_t virtualLanes(uint32_t slot) const { return m_virtual[slot]; }
    uint16_t staleLanes(uint32_t slot) const { return m_stale[slot]; }
    uint32_t pendingPasses(uint32_t slot) const { return m_chainLength[slot]; }
    Stage lastStage() const { return m_lastStage; }

private:
    uint16_t m_virtual[kSlots] = {}; // lanes whose content is the result of the slot's pending passes
    uint16_t m_stale[kSlots] = {};   // lanes whose real content is unknown
    uint8_t m_chainLength[kSlots] = {};
    uint16_t m_chainWrites[kSlots] = {}; // lanes the slot's pending passes write
    uint8_t m_chain[kSlots][kMaxChain] = {};
    std::vector<Pass> m_passes;      // pending passes of the batch, in the order the runs arrived
    std::vector<int16_t> m_passAxes; // per pending pass: index into m_axes for a grid pass, else -1
    std::vector<GridAxes> m_axes;
    bool m_anyPending = false;

    void gridOf(uint32_t id, uint32_t slot, uint32_t& x, uint32_t& z) const {
        x = z = 0;
        if (m_passAxes[id] < 0)
            return;
        const Pass& p = m_passes[id];
        const uint32_t cell = slot - p.first;
        x = m_axes[static_cast<size_t>(m_passAxes[id])].x[cell % p.param];
        z = m_axes[static_cast<size_t>(m_passAxes[id])].z[cell / p.param];
    }
    Stage m_lastStage = Stage::Other;
    uint32_t m_lastFamilies = 0;

    void materialiseSlot(uint32_t s, uint8_t* data) {
        Row r;
        loadRow(data, s, r);
        for (uint32_t i = 0; i < m_chainLength[s]; ++i) {
            uint32_t gx, gz;
            gridOf(m_chain[s][i], s, gx, gz);
            gsr::vp::applyPass(m_passes[m_chain[s][i]], r, gx, gz);
        }
        const uint32_t* words[3] = {r.pos, r.col, r.st};
        const uint32_t base[3] = {kPos, kCol, kSt};
        for (uint32_t l = 0; l < 12u; ++l)
            if ((m_virtual[s] >> l) & 1u)
                std::memcpy(data + size_t(base[l / 4u] + s) * 16u + size_t(l & 3u) * 4u, &words[l / 4u][l & 3u], 4);
        ++counters.slotsMaterialised;
        counters.slotPassesMaterialised += m_chainLength[s];
        m_virtual[s] = 0;
        m_chainLength[s] = 0;
        m_chainWrites[s] = 0;
    }

    // The EE (or an executed run) replaced `lanes` of a slot.
    void writeSlot(uint32_t s, uint16_t lanes) {
        m_stale[s] &= static_cast<uint16_t>(~lanes);
        if (m_chainLength[s] == 0u)
            return;
        // A pending pass that read a replaced lane from memory can no longer be evaluated; its results, and what
        // later passes made of them, are lost. They only matter when they are not replaced themselves.
        uint16_t written = 0, bad = 0;
        uint32_t keep = 0;
        for (uint32_t i = 0; i < m_chainLength[s]; ++i) {
            const uint32_t kind = m_passes[m_chain[s][i]].kind;
            const uint16_t rd = readLanes(kind), wr = writeLanes(kind);
            if ((rd & lanes & ~written) != 0u || (rd & bad) != 0u) {
                bad |= wr;
                ++counters.passesDropped;
            } else {
                written |= wr;
                bad &= static_cast<uint16_t>(~wr);
                m_chain[s][keep++] = m_chain[s][i];
            }
        }
        m_chainLength[s] = static_cast<uint8_t>(keep);
        m_chainWrites[s] = written;
        const uint16_t still = m_virtual[s] & static_cast<uint16_t>(~lanes);
        m_stale[s] |= still & bad;
        m_virtual[s] = still & static_cast<uint16_t>(~bad);
        if (m_virtual[s] == 0u) {
            m_chainLength[s] = 0;
            m_chainWrites[s] = 0;
        }
    }

    // A stage the microprogram executes on real memory: only staleness moves.
    void executedStage(uint32_t kind, uint32_t first, uint32_t count) {
        const uint16_t rd = readLanes(kind), wr = writeLanes(kind);
        for (uint32_t s = first; s < first + count && s < kSlots; ++s) {
            if (m_stale[s] & rd)
                m_stale[s] |= wr;
            else
                m_stale[s] &= static_cast<uint16_t>(~wr);
        }
    }

    void deferStage(const Pass& pass, uint8_t* data) {
        const uint16_t rd = readLanes(pass.kind), wr = writeLanes(pass.kind);
        const uint8_t id = static_cast<uint8_t>(m_passes.size());
        m_passes.push_back(pass);
        m_passAxes.push_back(-1);
        if (isGrid(pass.kind)) {
            m_passAxes.back() = static_cast<int16_t>(m_axes.size());
            m_axes.emplace_back();
            m_axes.back().set(pass);
        }
        m_anyPending = true;
        ++counters.passesDeferred;
        for (uint32_t s = pass.first; s < pass.first + pass.count; ++s) {
            if (m_stale[s] & rd) { // unknown in, unknown out
                m_stale[s] |= wr;
                m_virtual[s] &= static_cast<uint16_t>(~wr);
                continue;
            }
            // A lane the pass reads from memory must not be one an earlier pending pass of the slot also writes
            // (the EE replaced it in between): evaluating the chain from memory would feed the pass the wrong value.
            const uint16_t fromMemory = rd & static_cast<uint16_t>(~m_virtual[s]);
            if ((fromMemory & m_chainWrites[s]) != 0u || m_chainLength[s] == kMaxChain)
                materialiseSlot(s, data);
            m_chain[s][m_chainLength[s]++] = id;
            m_chainWrites[s] |= wr;
            m_virtual[s] |= wr;
            m_stale[s] &= static_cast<uint16_t>(~wr);
            ++counters.slotPassesDeferred;
        }
    }

    void endBatch() {
        if (!m_anyPending && m_passes.empty())
            return;
        for (uint32_t s = 0; s < kSlots; ++s) {
            if (m_virtual[s] != 0u) {
                uint32_t bits = m_virtual[s];
                bits = (bits & 0x555u) + ((bits >> 1) & 0x555u);
                bits = (bits & 0x333u) + ((bits >> 2) & 0x333u);
                counters.staleLanes += (bits & 0xFu) + ((bits >> 4) & 0xFu) + ((bits >> 8) & 0xFu);
                m_stale[s] |= m_virtual[s];
                m_virtual[s] = 0;
            }
            m_chainLength[s] = 0;
            m_chainWrites[s] = 0;
        }
        m_passes.clear();
        m_passAxes.clear();
        m_axes.clear();
        m_anyPending = false;
    }

    // Pending passes of slot `s` that the lanes `need` depend on, as a mask over m_passes; `memory` receives the
    // lanes read from VU memory.
    void collect(uint32_t s, uint16_t need, uint64_t marks[4], uint16_t& memory) const {
        need &= m_virtual[s];
        for (uint32_t i = m_chainLength[s]; i-- > 0u;) {
            const uint8_t id = m_chain[s][i];
            const uint32_t kind = m_passes[id].kind;
            const uint16_t wr = writeLanes(kind);
            if ((wr & need) == 0u)
                continue;
            marks[id >> 6] |= 1ull << (id & 63u);
            need = static_cast<uint16_t>((need & ~wr) | readLanes(kind));
        }
        memory = need;
    }

    Action runIt(Stage stage, uint8_t* data) {
        (void)stage;
        materialiseAll(data);
        return Action::Run;
    }

    Action decide(bool resume, uint32_t& pc, const float (*vf)[4], uint8_t* data, uint32_t families, bool engineOk,
                  std::vector<uint8_t>& packet, Reason& reason) {
        m_lastFamilies = 0;
        const auto count = [this](Stage s, bool taken) {
            m_lastStage = s;
            ++counters.runs[static_cast<uint32_t>(s)];
            if (taken)
                ++counters.taken[static_cast<uint32_t>(s)];
        };
        if (!resume) {
            // A new microprogram call ends the batch. What its pending passes would have left is not in memory,
            // with one exception: the last slot is brought up to date first. The index-list run of a batch with clip
            // codes reads the alpha lane of that slot's colour row as the "code" of an end index (index 0), also in
            // batches that never filled the slot, so that lane must always be what the microprogram left there.
            if (m_chainLength[kSlots - 1u] != 0u)
                materialiseSlot(kSlots - 1u, data);
            endBatch();
            if (pc == kPcSetup) {
                count(Stage::Setup, false);
                // With flag 8 the set-up fills the colour rows of all slots.
                if (static_cast<uint16_t>(word16(data, kQwFlags, 0)) & 8u)
                    for (uint32_t s = 0; s < kSlots; ++s)
                        m_stale[s] &= static_cast<uint16_t>(~kLaneCol);
            } else {
                count(Stage::Other, false);
            }
            return Action::Run;
        }
        if (pc != kPcAfterSetup && pc != kPcAfterVertex && pc != kPcAfterList && pc != kPcAfterClipList) {
            count(Stage::Other, false);
            return runIt(Stage::Other, data);
        }
        if (word16(data, kQwHeader, 2) > 0) {
            const IndexRun list = walkIndexList(data);
            const Stage stage = list.clip ? Stage::ClipList : Stage::List;
            m_lastFamilies = list.clip ? FamClipInside : FamPrimitive;
            Reason r = list.why;
            if (r == Reason::None && (families & (list.clip ? FamClipInside : FamPrimitive)) == 0u)
                r = Reason::Disabled;
            if (r == Reason::None && !engineOk)
                r = Reason::EngineState;
            // The codes the end indices contribute are lanes of the arrays too: they have to be real.
            for (uint32_t e = 0; r == Reason::None && e < list.entryCount; ++e)
                if (lanesOf(list.seedQw[e], 8u) != 0u)
                    r = Reason::StaleInput;
            if (r == Reason::None)
                r = buildRecord(list, vf, data, packet);
            if (r != Reason::None) {
                reason = r;
                count(stage, false);
                // The run is executed: every vertex it copies must be real, and so must the lanes the end indices
                // of a clip-code list contribute as codes (they decide which strips are dropped or clipped).
                bool staleSeed = false;
                for (uint32_t e = 0; e < list.entryCount; ++e) {
                    const uint32_t qw = list.seedQw[e];
                    if (qw >= kPos && qw < kSt + kSlots &&
                        ((m_stale[(qw - kPos) % kSlots] >> (((qw - kPos) / kSlots) * 4u + 3u)) & 1u) != 0u)
                        staleSeed = true;
                }
                if (staleSeed)
                    ++counters.staleDraws;
                else if (list.why == Reason::None || list.why == Reason::Clipper)
                    for (uint32_t i = 0; i < list.cornerCount; ++i)
                        if ((m_stale[list.corners[i].slot] & (kLanePos | kLaneStZ)) != 0u ||
                            (m_stale[list.corners[i].stSlot] & kLaneStXy) != 0u ||
                            (m_stale[list.corners[i].colSlot] & kLaneCol) != 0u) {
                            ++counters.staleDraws;
                            break;
                        }
                return runIt(stage, data);
            }
            count(stage, true);
            pc = list.clip ? kPcAfterClipList : kPcAfterList;
            if (list.clip) {
                // what the first walk leaves in memory (the seventeenth entry lies in the grid header)
                for (uint32_t e = 0; e < list.entryCount; ++e) {
                    const uint32_t words[2] = {list.entries[e][0], list.entries[e][1]};
                    std::memcpy(data + size_t(27u + e) * 16u, words, 8);
                }
                std::memcpy(data + size_t(65u) * 16u, &list.allOr, 4);
            }
            return Action::Skip;
        }
        const VertexRun run = classifyVertexRun(data);
        m_lastFamilies = run.families;
        Reason r = run.why;
        if (r == Reason::None && (run.families & ~families) != 0u)
            r = Reason::Disabled;
        if (r == Reason::None && !engineOk)
            r = Reason::EngineState;
        if (r == Reason::None && m_passes.size() + run.count > 255u)
            r = Reason::ChainLength;
        if (r != Reason::None) {
            reason = r;
            count(Stage::Vertex, false);
            const Action a = runIt(Stage::Vertex, data);
            if (run.why == Reason::None || run.why == Reason::ClipCodes) {
                for (uint32_t i = 0; i < run.count; ++i) {
                    const bool grid = isGrid(run.kinds[i]);
                    executedStage(run.kinds[i], grid ? run.gridFirst : run.first,
                                  grid ? run.gridCols * run.gridRows : run.slots);
                }
                if (run.clipCodes && run.kinds[run.count - 1u] == gsr::vp::PassTransform)
                    for (uint32_t s = run.first; s < run.first + run.slots; ++s)
                        m_stale[s] &= static_cast<uint16_t>(~kLaneStW);
            } else if (run.why == Reason::HeaderShape || run.why == Reason::GridShape ||
                       run.why == Reason::LightExponent) {
                // A run of a shape that is not followed: if anything is stale, anything it writes may be.
                bool any = false;
                for (uint32_t s = 0; s < kSlots; ++s)
                    any = any || m_stale[s] != 0u;
                if (any)
                    for (uint32_t s = 0; s < kSlots; ++s)
                        m_stale[s] = 0xFFFu;
            }
            return a;
        }
        for (uint32_t i = 0; i < run.count; ++i) {
            deferStage(makePass(run.kinds[i], run, data, vf), data);
            if (run.kinds[i] == gsr::vp::PassGridFlat) {
                // the flat grid stores the zero quadword it reads its heights from
                const uint32_t zero[4] = {0u, 0u, 0u, gsr::vp::vu::kOne};
                std::memcpy(data + size_t(47u) * 16u, zero, 16);
            }
        }
        count(Stage::Vertex, true);
        pc = kPcAfterVertex;
        return Action::Skip;
    }

    // Vertices of a record that need the same pending passes.
    struct PassSet {
        uint64_t marks[4];
        uint32_t passMask;
        int32_t lastPass, grid;
    };

    Reason buildRecord(const IndexRun& list, const float (*vf)[4], const uint8_t* data, std::vector<uint8_t>& packet) {
        using namespace gsr::vp;
        if (list.clip && list.stripCount == 0u && !list.emptyTag) {
            ++counters.records; // rejected as a whole: nothing is sent
            return Reason::None;
        }
        // The size is known once the passes are; the vertices are written in place behind the largest pass block.
        const uint32_t fixedBytes = static_cast<uint32_t>(sizeof(BatchHeader)) + batchStripBytes(list.stripCount) +
                                    batchIndexBytes(list.cornerCount);
        m_vertices.resize(kBatchMaxIndices);
        BatchVertex* const vertices = m_vertices.data();
        Corner from[kBatchMaxIndices];
        uint8_t setOf[kBatchMaxIndices];
        uint16_t indices[kBatchMaxIndices];
        PassSet sets[kBatchMaxIndices];
        uint32_t vertexCount = 0, setCount = 0;
        uint64_t all[4] = {};
        // Runs whose corners take all three rows from their own slot share vertices by slot.
        uint8_t vertexOfSlot[kSlots];
        bool mapped = false;
        for (uint32_t c = 0; c < list.cornerCount; ++c) {
            const Corner& k = list.corners[c];
            const bool own = k.stSlot == k.slot && k.colSlot == k.slot;
            if (own) {
                if (!mapped) {
                    std::memset(vertexOfSlot, 0xFF, sizeof(vertexOfSlot));
                    mapped = true;
                }
                if (vertexOfSlot[k.slot] != 0xFFu) {
                    indices[c] = vertexOfSlot[k.slot];
                    continue;
                }
                vertexOfSlot[k.slot] = static_cast<uint8_t>(vertexCount);
            } else if (k.stSlot == k.slot) {
                // a colour of another slot with the slot's own texture row: the same pair may come again
                uint32_t at = 0;
                while (at < vertexCount && (from[at].slot != k.slot || from[at].colSlot != k.colSlot))
                    ++at;
                if (at < vertexCount) {
                    indices[c] = static_cast<uint16_t>(at);
                    continue;
                }
            } // else: a texture coordinate of its own per corner, never the same twice
            indices[c] = static_cast<uint16_t>(vertexCount);
            from[vertexCount] = k;
            uint16_t need = kLanePos | kLaneStZ;
            if (k.stSlot == k.slot)
                need |= kLaneStXy;
            if (k.colSlot == k.slot)
                need |= kLaneCol;
            if (m_stale[k.slot] & need)
                return Reason::StaleInput;
            uint64_t marks[4] = {};
            uint16_t memory = 0;
            if (m_chainLength[k.slot] != 0u)
                collect(k.slot, need, marks, memory);
            BatchVertex& v = vertices[vertexCount];
            loadRow(data, k.slot, v.row);
            v.passMask = 0;
            v.gridX = v.gridZ = 0;
            v.slot = k.slot;
            if (k.stSlot != k.slot) {
                // A texture coordinate of its own per corner: it must be real, and no pass of the vertex may touch it.
                if ((m_stale[k.stSlot] | m_virtual[k.stSlot]) & kLaneStXy)
                    return (m_stale[k.stSlot] & kLaneStXy) ? Reason::StaleInput : Reason::CornerSource;
                std::memcpy(v.row.st, data + size_t(kSt + k.stSlot) * 16u, 8);
            }
            if (k.colSlot != k.slot) {
                // A colour of another slot (one per index quadword): real, or the result of lighting alone, which
                // reads nothing but its own row.
                if (m_stale[k.colSlot] & kLaneCol)
                    return Reason::StaleInput;
                uint64_t colourMarks[4] = {};
                uint16_t colourMemory = 0;
                collect(k.colSlot, kLaneCol, colourMarks, colourMemory);
                for (uint32_t id = 0; id < m_passes.size(); ++id) {
                    const bool mine = (marks[id >> 6] >> (id & 63u)) & 1u;
                    const bool colour = (colourMarks[id >> 6] >> (id & 63u)) & 1u;
                    if (colour && m_passes[id].kind != PassLight)
                        return Reason::CornerSource;
                    if (mine && ((readLanes(m_passes[id].kind) | writeLanes(m_passes[id].kind)) & kLaneCol))
                        return Reason::CornerSource;
                }
                for (uint32_t w = 0; w < 4u; ++w)
                    marks[w] |= colourMarks[w];
                std::memcpy(v.row.col, data + size_t(kCol + k.colSlot) * 16u, 16);
            }
            // most vertices of a run need the same passes
            uint32_t set = setCount;
            for (uint32_t i = setCount; i-- > 0u;)
                if (sets[i].marks[0] == marks[0] && sets[i].marks[1] == marks[1] && sets[i].marks[2] == marks[2] &&
                    sets[i].marks[3] == marks[3]) {
                    set = i;
                    break;
                }
            if (set == setCount) {
                std::memcpy(sets[setCount].marks, marks, sizeof(marks));
                sets[setCount].passMask = 0;
                sets[setCount].lastPass = -1;
                sets[setCount].grid = -1;
                ++setCount;
                for (uint32_t w = 0; w < 4u; ++w)
                    all[w] |= marks[w];
            }
            setOf[vertexCount] = static_cast<uint8_t>(set);
            ++vertexCount;
        }
        // The passes of the record, in the order the runs arrived. A batch of many vertex chunks repeats the same
        // stage with the same constants on other slots: such passes share one pass of the record as long as every
        // vertex still meets its passes in the order they ran.
        Pass passes[kBatchMaxPasses];
        uint32_t passCount = 0;
        for (uint32_t word = 0; word < 4u; ++word) {
            for (uint64_t bits = all[word]; bits != 0u; bits &= bits - 1u) {
                uint32_t bit = 0;
                while (((bits >> bit) & 1u) == 0u)
                    ++bit;
                const uint32_t id = word * 64u + bit;
                const Pass& p = m_passes[id];
                int32_t after = -1; // the pass has to come after every pass its vertices already have
                for (uint32_t i = 0; i < setCount; ++i)
                    if (((sets[i].marks[word] >> bit) & 1u) != 0u && sets[i].lastPass > after)
                        after = sets[i].lastPass;
                uint32_t at = static_cast<uint32_t>(after + 1);
                while (at < passCount && (passes[at].kind != p.kind || passes[at].param != p.param ||
                                          std::memcmp(passes[at].k, p.k, sizeof(p.k)) != 0))
                    ++at;
                if (at == passCount) {
                    if (passCount == kBatchMaxPasses - 1u)
                        return Reason::ChainLength;
                    passes[passCount++] = p;
                }
                for (uint32_t i = 0; i < setCount; ++i) {
                    if (((sets[i].marks[word] >> bit) & 1u) == 0u)
                        continue;
                    sets[i].passMask |= 1u << at;
                    sets[i].lastPass = static_cast<int32_t>(at);
                    if (isGrid(p.kind)) {
                        if (sets[i].grid >= 0)
                            return Reason::ChainLength; // two grid passes on one vertex: one pair of axis values
                        sets[i].grid = static_cast<int32_t>(id);
                    }
                }
            }
        }
        uint32_t projectBit = 0;
        if (list.clip) {
            // The batch kept clip-space positions; the index-list run does the projection itself.
            VertexRun none;
            passes[passCount] = makePass(PassProject, none, data, vf);
            projectBit = 1u << passCount;
            ++passCount;
        }
        for (uint32_t i = 0; i < vertexCount; ++i) {
            BatchVertex& v = vertices[i];
            const PassSet& set = sets[setOf[i]];
            v.passMask = set.passMask | projectBit;
            if (set.grid >= 0)
                gridOf(static_cast<uint32_t>(set.grid), v.slot, v.gridX, v.gridZ);
            if (list.clip && (m_virtual[v.slot] & (kLanePos | kLaneStZ)))
                return Reason::CornerSource;
        }
        BatchHeader h{};
        h.program = Batch;
        h.prim = list.prim;
        h.stripCount = list.stripCount;
        h.indexCount = list.cornerCount;
        h.vertexCount = vertexCount;
        h.passCount = passCount;
        h.flags = list.emptyTag ? kBatchEmptyTag : 0u;
        const uint32_t payload = fixedBytes + passCount * static_cast<uint32_t>(sizeof(Pass)) +
                                 vertexCount * static_cast<uint32_t>(sizeof(BatchVertex));
        packet.resize(16u + payload);
        const uint64_t wrapLo = uint64_t(payload / 16u) | (1ull << 15) | (3ull << 58);
        std::memcpy(packet.data(), &wrapLo, 8);
        std::memcpy(packet.data() + 8, &kTagMagic, 8);
        uint8_t* out = packet.data() + 16;
        std::memcpy(out, &h, sizeof(h));
        out += sizeof(h);
        std::memcpy(out, passes, size_t(passCount) * sizeof(Pass));
        out += size_t(passCount) * sizeof(Pass);
        std::memset(out, 0, batchStripBytes(h.stripCount) + batchIndexBytes(h.indexCount));
        std::memcpy(out, list.strips, size_t(h.stripCount) * 4u);
        out += batchStripBytes(h.stripCount);
        std::memcpy(out, indices, size_t(h.indexCount) * 2u);
        out += batchIndexBytes(h.indexCount);
        std::memcpy(out, vertices, size_t(vertexCount) * sizeof(BatchVertex));
        m_recordBuilt = true;
        ++counters.records;
        counters.recordVertices += vertexCount;
        counters.recordIndices += h.indexCount;
        counters.recordPasses += passCount;
        return Reason::None;
    }
    std::vector<BatchVertex> m_vertices; // scratch of buildRecord
    bool m_recordBuilt = false;
};

// True when every lane a strip record (gsr/vertex_program.h; `record` is its payload as vu1gpu::buildStrips wrote it)
// takes from VU memory holds what the microprogram would find there. The strip stage of the object path shares VU
// memory with the batch path: header slots from quadword 54, each followed by its vertices, with the strip length or
// the integer normal 240 quadwords higher and the texture coordinate 480 higher, so its rows overlap the three batch
// arrays. A batch whose runs were not executed leaves lanes behind that the tracker marks; the EE's upload of the
// strip table clears the marks of what it writes. Lanes the evaluation of the record does not read (the fourth lane
// of a texture coordinate, the normals of a constant-colour run) do not count.
inline bool stripInputsKnown(const Tracker& tracker, const uint8_t* record) {
    constexpr uint32_t kFirstSlot = 54, kAux = 240, kStripSt = 480;
    gsr::vp::Header header;
    std::memcpy(&header, record, sizeof(header));
    const uint8_t* table = record + sizeof(header) + sizeof(gsr::vp::Constants);
    const bool lit = header.program != gsr::vp::StripsFlat;
    uint32_t slot = kFirstSlot, unknown = 0;
    for (uint32_t s = 0; s < header.stripCount; ++s) {
        uint32_t n;
        std::memcpy(&n, table + size_t(s) * 4u, 4);
        unknown |= tracker.lanesOf(slot + kAux, 1u); // the strip's length
        for (uint32_t i = 1; i <= n; ++i) {
            unknown |= tracker.lanesOf(slot + i, 15u) | tracker.lanesOf(slot + i + kStripSt, 7u);
            if (lit)
                unknown |= tracker.lanesOf(slot + i + kAux, 15u);
        }
        slot += n + 1u;
    }
    return (unknown | tracker.lanesOf(slot + kAux, 1u)) == 0u; // and the length that ends the table
}
} // namespace vu1gpu::batch
