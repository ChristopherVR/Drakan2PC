#pragma once
// Instruction-pair decoding for the VU1 interpreter as constant expressions (friend of VU1Interpreter), so that the
// same code decodes at run time (VU1Interpreter::decodeInstructionPair) and at compile time (the ahead-of-time compiled
// microcode in aot/).
#include "runtime/ps2_vu1.h"

#include <algorithm>
#include <bit>
#include <cstdint>

#include "ps2_vu1_detail.h"

struct Vu1Decode {
    using InstructionUsage = VU1Interpreter::InstructionUsage;
    using VfAccess = VU1Interpreter::VfAccess;
    using DecodedInstructionPair = VU1Interpreter::DecodedInstructionPair;
    using Pipeline = VU1Interpreter::Pipeline;
    static constexpr Pipeline PipelineNone = VU1Interpreter::PipelineNone;
    static constexpr Pipeline PipelineFmac = VU1Interpreter::PipelineFmac;
    static constexpr Pipeline PipelineLsu = VU1Interpreter::PipelineLsu;
    static constexpr Pipeline PipelineFdiv = VU1Interpreter::PipelineFdiv;
    static constexpr Pipeline PipelineEfu = VU1Interpreter::PipelineEfu;
    static constexpr Pipeline PipelineIalu = VU1Interpreter::PipelineIalu;
    static constexpr Pipeline PipelineBranch = VU1Interpreter::PipelineBranch;
    static constexpr Pipeline PipelineXgkick = VU1Interpreter::PipelineXgkick;
    static constexpr uint32_t kFmacLatency = VU1Interpreter::kFmacLatency;
    static constexpr uint32_t kAccForwardLatency = VU1Interpreter::kAccForwardLatency;

    static constexpr uint8_t laneForComponent(uint32_t component) {
        return static_cast<uint8_t>(1u << (3u - component));
    }

    static constexpr void addVfRead(InstructionUsage& usage, uint8_t reg, uint8_t lanes) {
        if (lanes == 0u)
            return;
        for (uint32_t index = 0; index < usage.vfReadCount; ++index) {
            if (usage.vfRead[index].reg == reg) {
                usage.vfRead[index].lanes |= lanes;
                return;
            }
        }
        if (usage.vfReadCount < usage.vfRead.size())
            usage.vfRead[usage.vfReadCount++] = {reg, lanes};
    }

    static constexpr void addVfWrite(InstructionUsage& usage, uint8_t reg, uint8_t lanes) {
        if (reg == 0u || lanes == 0u)
            return;
        if (usage.vfWrite.reg == 0u)
            usage.vfWrite = {reg, lanes};
        else if (usage.vfWrite.reg == reg)
            usage.vfWrite.lanes |= lanes;
    }

    static constexpr uint8_t vfReadLanes(const InstructionUsage& usage, uint8_t reg) {
        for (uint32_t index = 0; index < usage.vfReadCount; ++index) {
            if (usage.vfRead[index].reg == reg)
                return usage.vfRead[index].lanes;
        }
        return 0u;
    }

    static constexpr InstructionUsage decodeUpperUsage(uint32_t upper) {
        InstructionUsage usage;
        usage.pipeline = PipelineFmac;
        usage.latency = kFmacLatency;

        const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
        const uint8_t dest = DEST(upper);
        const uint8_t fs = FS(upper);
        const uint8_t ft = FT(upper);
        const uint8_t fd = FD(upper);

        if (op <= 0x2Fu) {
            addVfRead(usage, fs, dest);
            addVfWrite(usage, fd, dest);
            if (op <= 0x1Bu)
                addVfRead(usage, ft, laneForComponent(op & 3u));
            else if (op >= 0x28u)
                addVfRead(usage, ft, op == 0x2Eu ? 0xEu : dest);
            if (op == 0x08u || op == 0x09u || op == 0x0Au || op == 0x0Bu || op == 0x0Cu || op == 0x0Du || op == 0x0Eu ||
                op == 0x0Fu || op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u || op == 0x29u || op == 0x2Du ||
                op == 0x2Eu) {
                usage.accRead = dest;
            }
            return usage;
        }

        if (op >= 0x3Cu) {
            const uint8_t special = static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu));
            const bool writesAcc = special <= 0x0Fu || (special >= 0x18u && special <= 0x1Cu) || special == 0x1Eu ||
                                   (special >= 0x20u && special <= 0x2Au) || (special >= 0x2Cu && special <= 0x2Eu);
            if (writesAcc) {
                addVfRead(usage, fs, dest);
                if (special <= 0x1Bu)
                    addVfRead(usage, ft, laneForComponent(special & 3u));
                else if ((special >= 0x28u && special <= 0x2Eu))
                    addVfRead(usage, ft, special == 0x2Eu ? 0xEu : dest);
                usage.accWrite = dest;
                if ((special >= 0x08u && special <= 0x0Fu) || special == 0x21u || special == 0x23u ||
                    special == 0x25u || special == 0x27u || special == 0x29u || special == 0x2Du) {
                    usage.accRead = dest;
                }
            } else if (special >= 0x10u && special <= 0x17u) {
                addVfRead(usage, fs, dest);
                addVfWrite(usage, ft, dest);
            } else if (special == 0x1Du) {
                addVfRead(usage, fs, dest);
                addVfWrite(usage, ft, dest);
            } else if (special == 0x1Fu) {
                addVfRead(usage, fs, 0xEu);
                addVfRead(usage, ft, 0x1u);
                usage.writesClip = true;
            } else if (special != 0x2Fu && special != 0x30u) {
                usage.reserved = true;
            }
            return usage;
        }

        usage.reserved = true;
        return usage;
    }

    static constexpr InstructionUsage decodeLowerUsage(uint32_t lower, bool vu1) {
        InstructionUsage usage;
        if (lower == 0u || lower == 0x8000033Cu)
            return usage;

        const uint8_t opHi = static_cast<uint8_t>((lower >> 25) & 0x7Fu);
        const uint8_t vfT = FT(lower);
        const uint8_t vfS = FS(lower);
        const uint8_t viT = VIT(lower);
        const uint8_t viS = VIS(lower);
        const uint8_t viD = VID(lower);
        const uint8_t dest = DEST(lower);
        auto readVi = [&](uint8_t reg) {
            if (reg != 0u)
                usage.viRead |= static_cast<uint16_t>(1u << reg);
        };
        auto writeVi = [&](uint8_t reg) {
            if (reg != 0u)
                usage.viWrite |= static_cast<uint16_t>(1u << reg);
        };

        switch (opHi) {
            case 0x00:
                usage.pipeline = PipelineLsu;
                usage.latency = 4u;
                readVi(viS);
                addVfWrite(usage, vfT, dest);
                return usage;
            case 0x01:
                usage.pipeline = PipelineLsu;
                usage.latency = 1u;
                readVi(viT);
                addVfRead(usage, vfS, dest);
                return usage;
            case 0x04:
                usage.pipeline = PipelineLsu;
                usage.latency = 4u;
                readVi(viS);
                writeVi(viT);
                return usage;
            case 0x05:
                usage.pipeline = PipelineLsu;
                usage.latency = 1u;
                readVi(viS);
                readVi(viT);
                return usage;
            case 0x08:
            case 0x09:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                usage.delaysNextBranchRead = true;
                readVi(viS);
                writeVi(viT);
                return usage;
            case 0x10:
            case 0x12:
            case 0x13:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                usage.readsClip = true;
                writeVi(1u);
                return usage;
            case 0x11:
                usage.pipeline = PipelineFmac;
                usage.latency = kFmacLatency;
                usage.writesClip = true;
                return usage;
            case 0x14:
            case 0x16:
            case 0x17:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                writeVi(viT);
                return usage;
            case 0x15:
                usage.pipeline = PipelineFmac;
                usage.latency = kFmacLatency;
                return usage;
            case 0x18:
            case 0x1A:
            case 0x1B:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                readVi(viS);
                writeVi(viT);
                return usage;
            case 0x1C:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                usage.readsClip = true;
                writeVi(viT);
                return usage;
            case 0x20:
                usage.pipeline = PipelineBranch;
                return usage;
            case 0x21:
                usage.pipeline = PipelineBranch;
                usage.latency = 1u;
                writeVi(viT);
                return usage;
            case 0x24:
                usage.pipeline = PipelineBranch;
                readVi(viS);
                return usage;
            case 0x25:
                usage.pipeline = PipelineBranch;
                usage.latency = 1u;
                readVi(viS);
                writeVi(viT);
                return usage;
            case 0x28:
            case 0x29:
                usage.pipeline = PipelineBranch;
                readVi(viS);
                readVi(viT);
                return usage;
            case 0x2C:
            case 0x2D:
            case 0x2E:
            case 0x2F:
                usage.pipeline = PipelineBranch;
                readVi(viS);
                return usage;
            case 0x40:
                break;
            default:
                usage.reserved = true;
                return usage;
        }

        const uint8_t direct = static_cast<uint8_t>(lower & 0x3Fu);
        if (direct == 0x30u || direct == 0x31u || direct == 0x34u || direct == 0x35u) {
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viS);
            readVi(viT);
            writeVi(viD);
            return usage;
        }
        if (direct == 0x32u) {
            usage.pipeline = PipelineIalu;
            usage.latency = 1u;
            usage.delaysNextBranchRead = true;
            readVi(viS);
            writeVi(viT);
            return usage;
        }
        if (direct < 0x3Cu) {
            usage.reserved = true;
            return usage;
        }

        const uint8_t special = static_cast<uint8_t>((lower & 3u) | ((lower >> 4) & 0x7Cu));
        switch (special) {
            case 0x30:
            case 0x31:
                usage.pipeline = PipelineFmac;
                usage.latency = 4u;
                addVfRead(usage, vfS, special == 0x31u ? 0xFu : dest);
                addVfWrite(usage, vfT, dest);
                break;
            case 0x34:
            case 0x36:
                usage.pipeline = PipelineLsu;
                usage.latency = 4u;
                usage.viLatency = 1u;
                usage.delaysNextBranchRead = true;
                readVi(viS);
                writeVi(viS);
                addVfWrite(usage, vfT, dest);
                break;
            case 0x35:
            case 0x37:
                usage.pipeline = PipelineLsu;
                usage.latency = 1u;
                usage.delaysNextBranchRead = true;
                readVi(viT);
                writeVi(viT);
                addVfRead(usage, vfS, dest);
                break;
            case 0x38:
                usage.pipeline = PipelineFdiv;
                usage.latency = 7u;
                addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
                addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
                break;
            case 0x39:
                usage.pipeline = PipelineFdiv;
                usage.latency = 7u;
                addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
                break;
            case 0x3A:
                usage.pipeline = PipelineFdiv;
                usage.latency = 13u;
                addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
                addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
                break;
            case 0x3B:
                usage.pipeline = PipelineFdiv;
                usage.waitQ = true;
                break;
            case 0x3C:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                usage.delaysNextBranchRead = true;
                addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
                writeVi(viT);
                break;
            case 0x3D:
                usage.pipeline = PipelineFmac;
                usage.latency = 4u;
                readVi(viS);
                addVfWrite(usage, vfT, dest);
                break;
            case 0x3E:
                usage.pipeline = PipelineLsu;
                usage.latency = 4u;
                readVi(viS);
                writeVi(viT);
                break;
            case 0x3F:
                usage.pipeline = PipelineLsu;
                usage.latency = 1u;
                readVi(viS);
                readVi(viT);
                break;
            case 0x40:
            case 0x41:
                usage.pipeline = PipelineFmac;
                usage.latency = 4u;
                addVfWrite(usage, vfT, dest);
                break;
            case 0x42:
            case 0x43:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
                break;
            case 0x64:
                if (!vu1) {
                    usage.reserved = true;
                    break;
                }
                usage.pipeline = PipelineFmac;
                usage.latency = 4u;
                addVfWrite(usage, vfT, dest);
                break;
            case 0x68:
            case 0x69:
                usage.pipeline = PipelineIalu;
                usage.latency = 1u;
                writeVi(viT);
                break;
            case 0x6C:
                if (!vu1) {
                    usage.reserved = true;
                    break;
                }
                usage.pipeline = PipelineXgkick;
                usage.latency = 2u;
                readVi(viS);
                break;
            case 0x70:
            case 0x71:
            case 0x72:
            case 0x73:
            case 0x74:
            case 0x75:
            case 0x76:
            case 0x77:
            case 0x78:
            case 0x79:
            case 0x7A:
            case 0x7C:
            case 0x7D:
                if (!vu1) {
                    usage.reserved = true;
                    break;
                }
                usage.pipeline = PipelineEfu;
                switch (special) {
                    case 0x70:
                        usage.latency = 11u;
                        break;
                    case 0x71:
                    case 0x72:
                    case 0x77:
                        usage.latency = 18u;
                        break;
                    case 0x73:
                        usage.latency = 24u;
                        break;
                    case 0x74:
                    case 0x75:
                    case 0x7C:
                        usage.latency = 54u;
                        break;
                    case 0x76:
                    case 0x78:
                    case 0x7A:
                        usage.latency = 12u;
                        break;
                    case 0x79:
                        usage.latency = 29u;
                        break;
                    case 0x7D:
                        usage.latency = 44u;
                        break;
                    default:
                        break;
                }
                if (special >= 0x70u && special <= 0x73u)
                    addVfRead(usage, vfS, 0xEu);
                else if (special == 0x74u)
                    addVfRead(usage, vfS, 0xCu);
                else if (special == 0x75u)
                    addVfRead(usage, vfS, 0xAu);
                else if (special == 0x76u)
                    addVfRead(usage, vfS, 0xFu);
                else
                    addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
                break;
            case 0x7B:
                if (!vu1) {
                    usage.reserved = true;
                    break;
                }
                usage.pipeline = PipelineEfu;
                usage.waitP = true;
                break;
            default:
                usage.reserved = true;
                break;
        }
        return usage;
    }

    static constexpr DecodedInstructionPair decodePair(uint32_t lowerWord, uint32_t upperWord, bool vu1) {
        DecodedInstructionPair decoded{};
        decoded.lower = lowerWord;
        decoded.upper = upperWord;
        decoded.iBit = (decoded.upper & 0x80000000u) != 0u;
        decoded.eBit = (decoded.upper & 0x40000000u) != 0u;
        decoded.mBit = (decoded.upper & 0x20000000u) != 0u;
        decoded.dBit = (decoded.upper & 0x10000000u) != 0u;
        decoded.tBit = (decoded.upper & 0x08000000u) != 0u;
        decoded.upperUsage = decodeUpperUsage(decoded.upper);
        if (!decoded.iBit)
            decoded.lowerUsage = decodeLowerUsage(decoded.lower, vu1);

        for (const InstructionUsage* usage : {&decoded.upperUsage, &decoded.lowerUsage}) {
            for (uint32_t index = 0; index < usage->vfReadCount && decoded.rdCount < decoded.rdReg.size(); ++index) {
                decoded.rdReg[decoded.rdCount] = usage->vfRead[index].reg;
                decoded.rdLanes[decoded.rdCount] = usage->vfRead[index].lanes;
                ++decoded.rdCount;
            }
            decoded.viReadMask |= static_cast<uint16_t>(usage->viRead & 0xFFFEu);
            decoded.accReadMask |= usage->accRead;
        }
        {
            // Arithmetic FMAC ops that go through applyFmacDest/applyFmacDestAcc share one numbering (0x00..0x2D) in
            // the main and in the "A" (accumulator) special groups.
            const uint32_t upper = decoded.upper;
            const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
            uint8_t code = 0xFFu;
            if (op < 0x30u)
                code = op;
            else if (op >= 0x3Cu) {
                code = static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu));
                decoded.fmToAcc = true;
            }
            uint8_t kind = 0, b = 0;
            if (code <= 0x0Fu) {
                kind = code < 4u ? 1 : code < 8u ? 2 : code < 12u ? 4 : 5;
                b = code & 3u;
            } else if (code >= 0x18u && code <= 0x1Bu) {
                kind = 3;
                b = code & 3u;
            } else {
                switch (code) {
                    case 0x1C:
                        kind = 3;
                        b = 4;
                        break;
                    case 0x1E:
                        kind = 3;
                        b = 5;
                        break;
                    case 0x20:
                        kind = 1;
                        b = 4;
                        break;
                    case 0x21:
                        kind = 4;
                        b = 4;
                        break;
                    case 0x22:
                        kind = 1;
                        b = 5;
                        break;
                    case 0x23:
                        kind = 4;
                        b = 5;
                        break;
                    case 0x24:
                        kind = 2;
                        b = 4;
                        break;
                    case 0x25:
                        kind = 5;
                        b = 4;
                        break;
                    case 0x26:
                        kind = 2;
                        b = 5;
                        break;
                    case 0x27:
                        kind = 5;
                        b = 5;
                        break;
                    case 0x28:
                        kind = 1;
                        b = 6;
                        break;
                    case 0x29:
                        kind = 4;
                        b = 6;
                        break;
                    case 0x2A:
                        kind = 3;
                        b = 6;
                        break;
                    case 0x2C:
                        kind = 2;
                        b = 6;
                        break;
                    case 0x2D:
                        kind = 5;
                        b = 6;
                        break;
                    default:
                        break;
                }
            }
            if (kind == 0)
                decoded.fmToAcc = false;
            decoded.fmKind = kind;
            decoded.fmB = b;
            if (op >= 0x3Cu && (code == 0x2Fu || code == 0x30u))
                decoded.fmKind = 0xFFu; // upper NOP: nothing to execute
        }

        const uint8_t upperWriteReg = decoded.upperUsage.vfWrite.reg;
        if (upperWriteReg != 0u &&
            (vfReadLanes(decoded.lowerUsage, upperWriteReg) != 0u || decoded.lowerUsage.vfWrite.reg == upperWriteReg)) {
            decoded.upperVfShadowReg = upperWriteReg;
            if (decoded.lowerUsage.vfWrite.reg == upperWriteReg)
                decoded.suppressedLowerVf = upperWriteReg;
        }
        {
            const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
            if (lowerWrite.reg != 0u && decoded.suppressedLowerVf != lowerWrite.reg) {
                decoded.wrReg[decoded.wrCount] = lowerWrite.reg;
                decoded.wrLanes[decoded.wrCount] = lowerWrite.lanes;
                decoded.wrLat[decoded.wrCount] =
                    decoded.lowerUsage.vfLatency != 0u ? decoded.lowerUsage.vfLatency : decoded.lowerUsage.latency;
                ++decoded.wrCount;
            }
            const VfAccess upperWrite = decoded.upperUsage.vfWrite;
            if (upperWrite.reg != 0u) {
                decoded.wrReg[decoded.wrCount] = upperWrite.reg;
                decoded.wrLanes[decoded.wrCount] = upperWrite.lanes;
                decoded.wrLat[decoded.wrCount] =
                    decoded.upperUsage.vfLatency != 0u ? decoded.upperUsage.vfLatency : decoded.upperUsage.latency;
                ++decoded.wrCount;
            }
            decoded.viWriteMask = static_cast<uint16_t>(decoded.lowerUsage.viWrite & 0xFFFEu);
            decoded.viWriteLat =
                decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency : decoded.lowerUsage.latency;
            decoded.accWriteMask = decoded.upperUsage.accWrite;
        }
        {
            const auto vfBits = [](uint8_t reg, uint8_t lanes, uint64_t& lo, uint64_t& hi) {
                for (uint32_t l = lanes; l != 0u; l &= l - 1u) {
                    const uint32_t comp = 3u - static_cast<uint32_t>(std::countr_zero(l));
                    const uint64_t bit = 1ull << ((reg & 15u) * 4u + comp);
                    (reg < 16u ? lo : hi) |= bit;
                }
            };
            for (uint32_t index = 0; index < decoded.rdCount; ++index)
                vfBits(decoded.rdReg[index], decoded.rdLanes[index], decoded.rdLo, decoded.rdHi);
            decoded.rdVa = static_cast<uint32_t>(decoded.viReadMask);
            for (uint32_t l = decoded.accReadMask; l != 0u; l &= l - 1u)
                decoded.rdVa |= 1u << (16u + 3u - static_cast<uint32_t>(std::countr_zero(l)));

            const auto addGroup = [&decoded](uint64_t lo, uint64_t hi, uint32_t va, uint32_t lat) {
                if ((lo | hi | va) == 0u || lat == 0u)
                    return;
                decoded.grpLo[decoded.grpCount] = lo;
                decoded.grpHi[decoded.grpCount] = hi;
                decoded.grpVa[decoded.grpCount] = va;
                decoded.grpLat[decoded.grpCount] = static_cast<uint8_t>(std::min<uint32_t>(lat, 15u));
                ++decoded.grpCount;
            };
            for (uint32_t index = 0; index < decoded.wrCount; ++index) {
                uint64_t lo = 0, hi = 0;
                vfBits(decoded.wrReg[index], decoded.wrLanes[index], lo, hi);
                addGroup(lo, hi, 0u, decoded.wrLat[index]);
            }
            addGroup(0, 0, decoded.viWriteMask, decoded.viWriteLat);
            {
                uint32_t va = 0;
                for (uint32_t l = decoded.accWriteMask; l != 0u; l &= l - 1u)
                    va |= 1u << (16u + 3u - static_cast<uint32_t>(std::countr_zero(l)));
                addGroup(0, 0, va, kAccForwardLatency);
            }
            const InstructionUsage& lu = decoded.lowerUsage;
            decoded.needsExtra = lu.pipeline == PipelineFdiv || lu.pipeline == PipelineEfu ||
                                 lu.pipeline == PipelineXgkick || lu.waitQ || lu.waitP;
        }
        return decoded;
    }
};
