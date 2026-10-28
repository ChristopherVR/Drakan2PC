#include "gs_mem.h"
#include <algorithm>
#include <array>
#include <cstdio>
using namespace gsr;
int main() {
    unsigned cases=0;
    for(auto psm:{PSM_CT32,PSM_CT24,PSM_CT16,PSM_CT16S,PSM_T8,PSM_T4,PSM_T8H,PSM_T4HL,PSM_T4HH})
    for(unsigned bp:{0u,1u,17u,16370u,16383u})
    for(unsigned bw:{1u,2u,3u,16u})
    for(unsigned w:{0u,1u,7u,16u,33u,128u})
    for(unsigned h:{0u,1u,9u,32u,65u}) {
        std::array<bool,kVramBlocks> expected{}, actual{};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
            expected[blockOf(psm,bp,bw,x,y)]=true;
        std::vector<uint16_t> blocks;
        appendTextureBlocks(blocks,psm,bp,bw,w,h);
        for(auto block:blocks)actual[block]=true;
        if(actual!=expected){std::printf("FAIL psm=%u bp=%u bw=%u %ux%u\n",unsigned(psm),bp,bw,w,h);return 1;}
        ++cases;
    }
    std::printf("Exact footprints match exhaustive pixel enumeration: %u cases\n",cases);
}
