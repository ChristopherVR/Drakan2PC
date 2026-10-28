// Only slice 0 includes these four checked, bounded self loops.
#include "../ps2_vu1_image_fingerprint.h"
#include "../ps2_vu1_normalized_lanes.h"
namespace
{
    bool disjointNativeRanges(const void* a, size_t an, const void* b, size_t bn)
    {
        if (!an || !bn) return true;
        const uintptr_t x=reinterpret_cast<uintptr_t>(a), y=reinterpret_cast<uintptr_t>(b);
        return x<=y ? y-x>=an : x-y>=bn;
    }
    bool nativeLoopDataSafe(const VU1Interpreter& v,const Vu1Step::Ctx& c)
    {
        // A guest store must not alter the interpreter or its host context.
        return disjointNativeRanges(&v,sizeof v,&c,sizeof c) &&
               disjointNativeRanges(c.vuData,c.dataSize,&v,sizeof v) &&
               disjointNativeRanges(c.vuData,c.dataSize,&c,sizeof c);
    }
    template<uint32_t B>
    inline constexpr auto kNativeLaneInputs=[] {
        std::array<uint8_t,Vu1Blocks::kMaxPairs> result{};
        Vu1SelectedNormalized::State known;
        for(uint32_t k=0;k<kTable.b[B].count;++k) {
            const uint64_t word=kVu1Image[kTable.b[B].first+k];
            const auto d=Vu1Decode::decodePair(uint32_t(word),uint32_t(word>>32),true);
            result[k]=known.inputs(d);known.apply(d);
        }
        return result;
    }();
    template<uint32_t B,size_t... K>
    PS2_VU_FORCEINLINE void nativeLanePairs(VU1Interpreter& v,Vu1Step::Ctx& c,uint64_t base,std::index_sequence<K...>,Vu1FlagBatch* batch=nullptr)
    {
        constexpr auto& bi=kTable.b[B];
        (Vu1Step::blockPair<kBatchFlags[B] && bi.T[K]+4u<=bi.cycles,false,kNativeLaneInputs<B>[K]>(v,kDec<bi.first+K>,c,base+bi.T[K],(bi.first+K)*8u,batch),...);
    }
    template<uint32_t B>
    bool nativeSteadyLoop(VU1Interpreter& v,Vu1Step::Ctx& c)
    {
        constexpr auto& bi=kTable.b[B];
        // All entry writes have retired at each backedge. These blocks cannot
        // introduce scalar pipelines, stop bits or unresolved delayed branches.
        static_assert(bi.cycles>=3 && bi.evCount==0 && bi.qSafe && bi.branchEnd);
        if(!nativeLoopDataSafe(v,c))return blockFn<B,true>(v,c);
        uint32_t remaining=kNativeLoopLimit;
        bool first=true;
        do {
            if(first) {
                if(!Vu1Step::blockCanEnter(v,c,bi.cycles,bi.qSafe,bi.eLo,bi.eHi,bi.eVa,bi.rLo,bi.rHi,bi.rVa))
                    return Vu1Aot::g_table[bi.first](v,c);
                first=false;
            } else if(c.budgetEnd-Vu1Step::blockBase(v)<=bi.cycles) {
                return Vu1Aot::g_table[bi.first](v,c);
            }
            if(Vu1Aot::g_blockStats)Vu1Step::blockCount(v,true,bi.count);
            const uint64_t base=Vu1Step::blockBase(v);
            if constexpr(kBatchFlags[B]) {
                Vu1FlagBatch batch;
                nativeLanePairs<B>(v,c,base,std::make_index_sequence<bi.count>{},&batch);
                Vu1Step::blockExit(v,c,base,bi.cycles,bi.count,(bi.first+bi.count)*8u,bi.branchEnd,bi.ev,bi.evCount);
                Vu1Step::finishFlagBatch(v,batch);
            } else {
                nativeLanePairs<B>(v,c,base,std::make_index_sequence<bi.count>{});
                Vu1Step::blockExit(v,c,base,bi.cycles,bi.count,(bi.first+bi.count)*8u,bi.branchEnd,bi.ev,bi.evCount);
            }
            if(--remaining && v.state().pc==bi.first*8u && Vu1Step::nativeContinue(v,c))continue;
            return true;
        } while(true);
    }
    bool nativeFillLoop(VU1Interpreter& v,Vu1Step::Ctx& c)
    {
        static_assert(Vu1Fingerprint::ofRange(kVu1Image,55,5)==0xe4fc677a2cbed32eull);
        uint32_t pairs=0;
        if(nativeLoopDataSafe(v,c) && Vu1Fast::nativeFill(v,c.vuData,c.dataSize,c.budgetEnd,kNativeLoopLimit,pairs)) {
            c.pairs+=pairs;return true;
        }
        return nativeSteadyLoop<4>(v,c);
    }
}
namespace Vu1Aot
{
    void registerBlocks0()
    {
        registerBlocks0Base();
        const char* mode=std::getenv("DRAKAN_VU_NATIVE_LOOPS");
        if(!mode || (mode[0]!='2' && mode[0]!='3'))return;
        static_assert(kTable.b[4].first==55 && kTable.b[19].first==155 &&
                      kTable.b[25].first==188 && kTable.b[40].first==334);
        g_dispatch[55]=&nativeSteadyLoop<4>;
        g_dispatch[155]=&nativeSteadyLoop<19>;
        g_dispatch[188]=&nativeSteadyLoop<25>;
        g_dispatch[334]=&nativeSteadyLoop<40>;
        if(mode[0]=='3')g_dispatch[55]=&nativeFillLoop;
    }
}
