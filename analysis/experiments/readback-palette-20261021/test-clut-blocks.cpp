#include "clut_blocks.h"
#include <set>
#include <cstdio>
int main(){uint32_t rng=77;auto next=[&](){rng=rng*1664525u+1013904223u;return rng;};uint64_t checks=0;
for(unsigned trial=0;trial<20000;++trial){const uint32_t psms[]={0,1,2,10};auto psm=psms[trial%4],bp=next()%16384,bw=next()%64,cou=next()%64,cov=next()%1024;bool is8=(trial/4)%2,csm=(trial/8)%2;auto got=gsr::clutBlocks(psm,bp,is8,csm,bw,cou,cov);std::set<uint32_t> ref;
for(uint32_t j=0;j<(is8?256u:16u);++j){uint32_t x,y,pitch=1;if(csm){x=(cou*16+j)&2047;y=cov;pitch=bw?bw:1;}else if(is8){auto r=(j&~0x18u)|((j&8)<<1)|((j&16)>>1);x=r&15;y=r>>4;}else if(psm&2){x=j&15;y=0;}else{x=j&7;y=j>>3;}ref.insert(gsr::blockOf(psm,bp,pitch,x,y));}
if(std::set<uint32_t>(got.blocks.begin(),got.blocks.begin()+got.count)!=ref)return 1;++checks;}
std::printf("PASS %llu exact palette footprint cases across both CLUT modes and all color formats\n",checks);}
