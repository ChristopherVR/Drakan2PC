// Included by ps2_vu1_aot_N.cpp with AOT_FIRST / AOT_COUNT / AOT_REGISTER defined: the compiled pairs [AOT_FIRST, AOT_FIRST + AOT_COUNT).
#include "ps2_vu1_aot.h"
#include "ps2_vu1_blocks.h"
#include "../ps2_vu1_decode.h"
#include "../ps2_vu1_normalized.h"

#include <array>
#include <utility>
#include <cstdlib>

#include "ps2_vu1_image.inc"

namespace
{
    using Dec = Vu1Step::Dec;

    template <size_t I>
    inline constexpr Dec kDec = Vu1Decode::decodePair(static_cast<uint32_t>(kVu1Image[I]), static_cast<uint32_t>(kVu1Image[I] >> 32), true);

    template <size_t I>
    bool pairFn(VU1Interpreter &v, Vu1Step::Ctx &c)
    {
        return Vu1Step::step(v, kDec<I>, c);
    }

    template <size_t... Is>
    void registerPairs(std::index_sequence<Is...>)
    {
        ((Vu1Aot::g_table[AOT_FIRST + Is] = &pairFn<AOT_FIRST + Is>), ...);
    }

    // ---- blocks (ps2_vu1_blocks.h)
    constexpr uint32_t kBlockEntries[] = {0u, 0x26e0u / 8u}; // microprogram entry points used by MSCAL
    // Replay-profiled re-entry points after load-use hazards / an in-flight Q
    // result. Retain the original whole blocks for an already-ready pipeline.
    constexpr uint32_t kRecoveryEntries[] = {127u, 425u, 502u, 609u};
    inline constexpr Vu1Blocks::Table kTable = Vu1Blocks::build(kVu1Image, kBlockEntries, 2,
                                                               kRecoveryEntries, 4);
    static_assert(kTable.maxLat <= 4, "the block entry check assumes busy registers become ready within 3 cycles");

    constexpr bool batchFlagsSafe(uint32_t B)
    {
        const auto& bi = kTable.b[B];
        // Reading Q does not observe MAC/status flags. Existing block entry
        // checks already reject a pending division for Q-reading blocks.
        uint32_t early = 0;
        for (uint32_t k = 0; k < bi.count; ++k)
        {
            const Dec d = Vu1Decode::decodePair(static_cast<uint32_t>(kVu1Image[bi.first+k]),
                                               static_cast<uint32_t>(kVu1Image[bi.first+k] >> 32), true);
            const uint32_t lowerOp = (d.lower >> 25) & 127u;
            if (!d.iBit && lowerOp >= 0x10u && lowerOp <= 0x1cu) return false;
            if (d.fmKind == 0)
            {
                const uint32_t op = d.upper & 63u;
                const uint32_t special = (d.upper & 3u) | ((d.upper >> 4) & 124u);
                const bool maxMin = (op >= 0x10u && op <= 0x17u) || op == 0x1du || op == 0x1fu || op == 0x2bu || op == 0x2fu;
                const bool conversionOrAbs = op >= 0x3cu && ((special >= 0x10u && special <= 0x17u) || special == 0x1du);
                if (!maxMin && !conversionOrAbs) return false;
            }
            if (d.fmKind >= 1 && d.fmKind <= 5 && ((d.upper >> 21) & 15u) != 0 && bi.T[k] + 4u <= bi.cycles) ++early;
        }
        return early >= 2;
    }

    inline constexpr auto kBatchFlags = [] {
        std::array<bool, Vu1Blocks::kMaxBlocks> result{};
        for (uint32_t b = 0; b < kTable.count; ++b) result[b] = batchFlagsSafe(b);
        return result;
    }();

    inline constexpr auto kNormalInputs = [] {
        std::array<std::array<uint8_t, Vu1Blocks::kMaxPairs>, Vu1Blocks::kMaxBlocks> result{};
        for (uint32_t b=0; b<kTable.count; ++b) {
            Vu1Normalized::State known;
            for (uint32_t k=0; k<kTable.b[b].count; ++k) {
                const uint64_t word=kVu1Image[kTable.b[b].first+k];
                const Dec d=Vu1Decode::decodePair(uint32_t(word),uint32_t(word>>32),true);
                result[b][k]=known.inputs(d);
                known.apply(d);
            }
        }
        return result;
    }();

    template <uint32_t B, bool TimedQ = false, size_t... K>
    PS2_VU_FORCEINLINE void blockPairs(VU1Interpreter &v, Vu1Step::Ctx &c, uint64_t base, std::index_sequence<K...>, Vu1FlagBatch* batch = nullptr)
    {
        constexpr const Vu1Blocks::Info &bi = kTable.b[B];
        (Vu1Step::blockPair<kBatchFlags[B] && bi.T[K] + 4u <= bi.cycles, TimedQ, kNormalInputs[B][K]>(v, kDec<bi.first + K>, c, base + bi.T[K], static_cast<uint32_t>((bi.first + K) * 8u), batch), ...);
    }

    template<uint32_t B, size_t... K>
    PS2_VU_FORCEINLINE void chooseBlockPairs(VU1Interpreter& v,Vu1Step::Ctx& c,uint64_t base,std::index_sequence<K...> indices,Vu1FlagBatch* batch=nullptr)
    {
        if constexpr (!kTable.b[B].qSafe)
        {
            if (Vu1Step::blockQDue(v,base+kTable.b[B].cycles))
            {
                blockPairs<B,true>(v,c,base,indices,batch);
                return;
            }
        }
        blockPairs<B>(v,c,base,indices,batch);
    }

    const uint32_t kNativeLoopLimit = [] {
        const char* setting = std::getenv("DRAKAN_VU_NATIVE_LOOP_LIMIT");
        const long limit = setting ? std::strtol(setting, nullptr, 10) : 32;
        return static_cast<uint32_t>(limit < 1 ? 1 : limit > 256 ? 256 : limit);
    }();

    template <uint32_t B, bool NativeLoop = false>
    bool blockFn(VU1Interpreter &v, Vu1Step::Ctx &c)
    {
        constexpr const Vu1Blocks::Info &bi = kTable.b[B];
        uint32_t remaining = NativeLoop ? kNativeLoopLimit : 1u;
        do
        {
        if (!Vu1Step::blockCanEnter(v, c, bi.cycles, bi.qSafe, bi.eLo, bi.eHi, bi.eVa, bi.rLo, bi.rHi, bi.rVa))
        {
            if (Vu1Aot::g_blockStats)
            {
                Vu1Step::blockCount(v, false, 0);
                Vu1Step::blockWhy(v, c, bi.cycles, bi.qSafe, bi.rLo, bi.rHi, bi.rVa, bi.wLo, bi.wHi, bi.wVa);
            }
            return Vu1Aot::g_table[bi.first](v, c);
        }
        if (Vu1Aot::g_blockStats) Vu1Step::blockCount(v, true, bi.count);
        const uint64_t base = Vu1Step::blockBase(v);
        if constexpr (kBatchFlags[B])
        {
            Vu1FlagBatch batch;
            chooseBlockPairs<B>(v, c, base, std::make_index_sequence<bi.count>{}, &batch);
            Vu1Step::blockExit(v, c, base, bi.cycles, bi.count, static_cast<uint32_t>((bi.first + bi.count) * 8u), bi.branchEnd, bi.ev, bi.evCount);
            Vu1Step::finishFlagBatch(v, batch);
        }
        else
        {
            chooseBlockPairs<B>(v, c, base, std::make_index_sequence<bi.count>{});
            Vu1Step::blockExit(v, c, base, bi.cycles, bi.count, static_cast<uint32_t>((bi.first + bi.count) * 8u), bi.branchEnd, bi.ev, bi.evCount);
        }
        if constexpr (NativeLoop)
        {
            if (--remaining && v.state().pc == bi.first * 8u && Vu1Step::nativeContinue(v,c))
                continue;
        }
        return true;
        } while (true);
    }

#if AOT_FIRST <= 489 && (AOT_FIRST + AOT_COUNT) > 489
    // Predicted packet-emission path, scheduled once across internal branches.
    // Each guard commits a complete prefix if the real branch chooses another
    // path. The original checked block is retained as the entry fallback.
    constexpr uint32_t kPacketPcs[] = {489,490,491,492,493,494,495,496,498,499,501,502,503,504,505,506,507};
    struct PacketImage { uint64_t words[17]{}; };
    inline constexpr auto kPacketImage = [] {
        PacketImage image;
        for (uint32_t i=0;i<17;++i) image.words[i]=kVu1Image[kPacketPcs[i]];
        return image;
    }();
    template<uint32_t Count>
    inline constexpr auto kPacketSchedule = Vu1Blocks::schedule(kPacketImage.words, 0, Count, true, kTable.maxLat);
    constexpr bool packetEligible()
    {
        for (uint32_t i=0;i<17;++i)
        {
            const uint32_t pc=kPacketPcs[i];
            const auto d=Vu1Decode::decodePair(uint32_t(kVu1Image[pc]),uint32_t(kVu1Image[pc]>>32),true);
            if (!Vu1Blocks::blockEligible(d)) return false;
            if ((d.lowerUsage.pipeline==Vu1Decode::PipelineBranch) != (i==4 || i==6 || i==8 || i==15)) return false;
        }
        return true;
    }
    template<uint32_t Count>
    constexpr bool packetPrefixAgrees()
    {
        for(uint32_t i=0;i<Count;++i) if(kPacketSchedule<Count>.T[i]!=kPacketSchedule<17>.T[i])return false;
        return true;
    }
    static_assert(packetEligible() && kPacketSchedule<17>.qSafe);
    static_assert(packetPrefixAgrees<6>() && packetPrefixAgrees<8>() && packetPrefixAgrees<10>());
    static_assert(kTable.b[55].first==489 && kTable.b[55].count==6);
    static_assert(kDec<493>.lowerUsage.pipeline==Vu1Decode::PipelineBranch &&
                  kDec<495>.lowerUsage.pipeline==Vu1Decode::PipelineBranch &&
                  kDec<498>.lowerUsage.pipeline==Vu1Decode::PipelineBranch &&
                  kDec<506>.lowerUsage.pipeline==Vu1Decode::PipelineBranch);

    template<uint32_t First,size_t... K>
    PS2_VU_FORCEINLINE void packetPairs(VU1Interpreter& v, Vu1Step::Ctx& c,uint64_t base,std::index_sequence<K...>)
    {
        (Vu1Step::blockPair(v,kDec<kPacketPcs[First+K]>,c,base+kPacketSchedule<17>.T[First+K],kPacketPcs[First+K]*8u),...);
    }
    template<uint32_t Count>
    PS2_VU_FORCEINLINE bool packetGuard(VU1Interpreter& v,Vu1Step::Ctx& c,uint64_t base,uint32_t fallthrough,uint32_t expected)
    {
        const uint32_t next = Vu1Step::resolveTraceBranch(v,fallthrough*8u);
        if(next==expected*8u)return true;
        constexpr const auto& prefix=kPacketSchedule<Count>;
        Vu1Step::blockExit(v,c,base,prefix.cycles,Count,next,false,prefix.ev,prefix.evCount);
        if (Vu1Aot::g_blockStats) Vu1Step::blockCount(v,true,Count);
        return false;
    }
    bool packetSuperTrace(VU1Interpreter& v,Vu1Step::Ctx& c)
    {
        constexpr const auto& bi=kPacketSchedule<17>;
        // Retain the exact path for every pending scalar pipeline, even though
        // the ordinary Q-safe blocks can defer FDIV retirement individually.
        if(!Vu1Step::traceScalarsIdle(v) || !Vu1Step::blockCanEnter(v,c,bi.cycles,false,bi.eLo,bi.eHi,bi.eVa,bi.rLo,bi.rHi,bi.rVa))
            return blockFn<55>(v,c);
        const uint64_t base=Vu1Step::blockBase(v);
        packetPairs<0>(v,c,base,std::make_index_sequence<6>{});
        if(!packetGuard<6>(v,c,base,495,495))return true;
        packetPairs<6>(v,c,base,std::make_index_sequence<2>{});
        if(!packetGuard<8>(v,c,base,497,498))return true;
        packetPairs<8>(v,c,base,std::make_index_sequence<2>{});
        if(!packetGuard<10>(v,c,base,500,501))return true;
        packetPairs<10>(v,c,base,std::make_index_sequence<7>{});
        Vu1Step::blockExit(v,c,base,bi.cycles,17,508u*8u,true,bi.ev,bi.evCount);
        if (Vu1Aot::g_blockStats) Vu1Step::blockCount(v,true,17);
        return true;
    }
#endif

    struct BlockRange
    {
        uint32_t count = 0;
        uint32_t idx[Vu1Blocks::kMaxBlocks]{};
    };
    constexpr BlockRange blocksIn(uint32_t lo, uint32_t hi)
    {
        BlockRange r;
        for (uint32_t i = 0; i < kTable.count; ++i)
            if (kTable.b[i].first >= lo && kTable.b[i].first < hi)
                r.idx[r.count++] = i;
        return r;
    }
    inline constexpr BlockRange kSliceBlocks = blocksIn(AOT_FIRST, AOT_FIRST + AOT_COUNT);

    template <uint32_t B>
    void registerBlock()
    {
        if constexpr (kTable.b[B].recovery)
            if (!Vu1Aot::recoveryEnabled()) return;
        Vu1Aot::g_dispatch[kTable.b[B].first] = &blockFn<B>;
        if constexpr (kTable.b[B].first == 55 || kTable.b[B].first == 155 ||
                      kTable.b[B].first == 188 || kTable.b[B].first == 334)
        {
            const char* setting=std::getenv("DRAKAN_VU_NATIVE_LOOPS");
            if (setting && setting[0] && setting[0] != '0')
                Vu1Aot::g_dispatch[kTable.b[B].first] = &blockFn<B,true>;
        }
    }

    template <size_t... Is>
    void registerBlocks(std::index_sequence<Is...>)
    {
        (registerBlock<kSliceBlocks.idx[Is]>(), ...);
    }
}

namespace Vu1Aot
{
    void AOT_REGISTER()
    {
        registerPairs(std::make_index_sequence<AOT_COUNT>{});
    }

    void AOT_REGISTER_BLOCKS()
    {
        registerBlocks(std::make_index_sequence<kSliceBlocks.count>{});
#if AOT_FIRST <= 489 && (AOT_FIRST + AOT_COUNT) > 489
        const char* trace = std::getenv("DRAKAN_VU_TRACE");
        if (recoveryEnabled() && !(trace && trace[0] == '0'))
            g_dispatch[489] = &packetSuperTrace;
#endif
    }
}
