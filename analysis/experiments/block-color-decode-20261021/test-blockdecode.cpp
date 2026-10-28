#include "rt_color_decode.h"
#include <cstdio>
#include <vector>
#include <chrono>
int main() {
 gsr::LocalMemory mem;uint32_t seed=741;
 for(uint32_t i=0;i<gsr::kVramSize;++i){seed=seed*1664525u+1013904223u;mem.data()[i]=uint8_t(seed>>24);}
 uint64_t checked=0;
 for(auto psm:{gsr::PSM_CT32,gsr::PSM_CT24})
 for(uint32_t bp:{0u,1u,31u,32u,16360u,16383u})
 for(uint32_t bw:{0u,1u,3u,8u})
 for(uint32_t w:{0u,1u,7u,8u,9u,63u,64u,65u,512u})
 for(uint32_t y0:{0u,1u,7u,8u,31u,32u,511u}) {
  const uint32_t rows=35;std::vector<uint32_t> out(size_t(w)*rows+2,0xdeadbeef);
  if(!gsr::decodeColorRows(mem,psm,bp,bw,w,y0,rows,out.data()+1))return 1;
  for(uint32_t y=0;y<rows;++y)for(uint32_t x=0;x<w;++x){
   uint32_t ref=mem.read(psm,bp,bw,x,y0+y);if(psm==gsr::PSM_CT24)ref|=0x80000000u;
   if(out[1+size_t(y)*w+x]!=ref){std::printf("mismatch %u %u %u %u %u\n",psm,bp,bw,x,y0+y);return 2;}++checked;
  }
  if(out.front()!=0xdeadbeef || out.back()!=0xdeadbeef)return 3;
 }
 uint32_t sentinel=77;
 if(gsr::decodeColorRows(mem,gsr::PSM_CT16,0,1,1,0,1,&sentinel)||sentinel!=77)return 4;
 std::vector<uint32_t> timing(512*256);uint64_t checksum=0;
 for(int round=0;round<4;++round)for(int trial=0;trial<2;++trial) {
  const bool fast=((round+trial)&1)!=0;
  auto start=std::chrono::steady_clock::now();
  for(int repeat=0;repeat<200;++repeat) {
   if(fast)gsr::decodeColorRows(mem,gsr::PSM_CT32,31,8,512,0,256,timing.data());
   else for(uint32_t y=0;y<256;++y)for(uint32_t x=0;x<512;++x)timing[y*512+x]=mem.read(gsr::PSM_CT32,31,8,x,y);
   checksum+=timing[(repeat*997u)%timing.size()];
  }
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::printf("decode-only %s %.3f ms checksum %llu\n",fast?"block":"reference",ms,checksum);
 }
 std::printf("PASS: %llu exact pixel comparisons, boundaries and unsupported format fallback\n",checked);
}
