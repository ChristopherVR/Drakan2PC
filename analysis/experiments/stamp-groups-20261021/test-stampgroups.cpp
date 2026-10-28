#include "gs_mem.h"
#include <cstdio>
#include <chrono>
uint64_t reference(const gsr::LocalMemory& m,uint32_t base,uint32_t count){uint64_t v=0;for(uint32_t i=0;i<count;++i){auto s=m.blockStamp(base+i);if(s>v)v=s;}return v;}
int main(){
 gsr::LocalMemory m;uint32_t rng=471;auto next=[&](){rng=rng*1664525u+1013904223u;return rng;};uint64_t checks=0;
 const uint32_t formats[]={0,1,2,10,19,20,27,36,44,48,49,50,58};
 for(unsigned step=0;step<4000;++step){
  if(step%2)m.touchBlocks(next()%32768,next()%513);
  else {auto psm=formats[next()%13],bp=next()%16384,bw=next()%8,x=next()%129,y=next()%129,w=next()%129,h=next()%129;m.touchRect(psm,bp,bw,x,y,w,h);}
  for(unsigned q=0;q<24;++q){auto base=next();auto count=next()%20000;if(m.maxStamp(base,count)!=reference(m,base,count))return 1;++checks;}
 }
 for(unsigned b=0;b<16384;++b)for(unsigned n:{0u,1u,63u,64u,65u,127u,128u}){if(m.maxStamp(b,n)!=reference(m,b,n))return 2;++checks;}
 uint64_t sum=0;
 for(unsigned mode=0;mode<4;++mode){auto start=std::chrono::steady_clock::now();for(unsigned q=0;q<100000;++q){auto base=q%16384;sum+=mode%2?m.maxStamp(base,2048):reference(m,base,2048);}std::printf("stamp-only %s %.3fms checksum=%llu\n",mode%2?"grouped":"scalar",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),sum);}
 std::printf("PASS %llu exact range queries after interleaved block/rectangle writes\n",checks);
}
