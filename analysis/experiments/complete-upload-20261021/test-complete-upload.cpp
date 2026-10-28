#include "gs_complete_upload.h"
#include <cstdio>
#include <vector>
#include <array>
int main(){uint64_t tests=0;std::array<uint8_t,1024> packet{};
for(unsigned flag=0;flag<4;++flag)for(unsigned loops:{0u,1u,2u,31u,32767u})for(unsigned bytes=0;bytes<=1024;++bytes){uint64_t tag=uint64_t(flag)<<58|loops;std::memcpy(packet.data()+16,&tag,8);auto actual=gsr::followingImageBytes(packet.data(),bytes);auto expected=bytes<32||flag!=2?0:std::min(loops,(bytes-32)/16)*16;if(actual!=expected)return 1;++tests;}
for(uint32_t psm:{0u,1u,2u,19u,20u,27u})for(uint32_t bp:{0u,1u,31u,12288u,16383u})for(uint32_t bw:{0u,1u,2u,4u})for(uint32_t h:{0u,1u,32u,64u,128u})for(uint32_t x:{0u,1u})for(uint32_t available:{0u,65535u,65536u,1048576u}){
 uint32_t first=0,last=0,w=bw*64;bool ok=gsr::fullUploadBlocks(psm,bp,bw,x,0,w,h,available,first,last);
 if(ok){std::vector<uint8_t> written(gsr::kVramSize);uint64_t count=0;for(uint32_t y=0;y<h;++y)for(uint32_t col=0;col<w;++col){auto addr=gsr::pixelAddr(psm,bp,bw,col,y)&(gsr::kVramSize-1);for(unsigned k=0;k<(psm==0?4u:1u);++k){if(written[addr+k])return 2;written[addr+k]=1;++count;}}
 if(count!=uint64_t(last-first+1)*256 || count>available)return 3;
 for(uint32_t a=first*256;a<(last+1)*256;++a)if(!written[a])return 4;
 }++tests;
}
uint32_t first,last;if(gsr::fullUploadBlocks(0,0,2,0,1,128,128,65536,first,last))return 5;
if(gsr::fullUploadBlocks(0,0,2,0,0,127,128,65536,first,last))return 6;
std::printf("PASS %llu packet/coverage cases; accepted uploads exhaustively cover every byte exactly once\n",tests);}
