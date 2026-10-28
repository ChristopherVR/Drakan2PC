#include "gs_complete_upload.h"
#include <cstdio>
#include <vector>
#include <random>
int main() {
 std::mt19937 rng(20261021);unsigned accepted=0;
 for(unsigned i=0;i<20000;++i) {
  unsigned psm=i%2?gsr::PSM_CT32:gsr::PSM_T8;
  auto g=gsr::geomOf(psm);
  unsigned bp=i%5==0?16383:unsigned(rng()%16000),bw=1+rng()%4;
  unsigned x=(rng()%10)*g.blockW,y=(rng()%8)*g.blockH;
  unsigned w=(1+rng()%8)*g.blockW,h=(1+rng()%4)*g.blockH;
  if(i%7==0)++x;if(i%11==0)--w;
  unsigned bytes=w*h*(psm==gsr::PSM_CT32?4:1),available=i%13==0?bytes-1:bytes;
  unsigned first=0,last=0;
  if(!gsr::fullUploadBlocks(psm,bp,bw,x,y,w,h,available,first,last))continue;
  ++accepted;std::vector<unsigned> addresses;
  for(unsigned yy=y;yy<y+h;++yy)for(unsigned xx=x;xx<x+w;++xx) {
   unsigned a=gsr::pixelAddr(psm,bp,bw,xx,yy);
   for(unsigned k=0;k<(psm==gsr::PSM_CT32?4:1);++k)addresses.push_back(a+k);
  }
  std::sort(addresses.begin(),addresses.end());
  if(addresses.size()>available || addresses.size()!=size_t(last-first+1)*256)return 1;
  for(size_t j=0;j<addresses.size();++j)if(addresses[j]!=first*256+j || addresses[j]>=gsr::kVramSize)return 2;
 }
 unsigned first,last;
 if(!gsr::fullUploadBlocks(0,12544,1,0,0,16,16,1024,first,last) || first!=12544 || last!=12547)return 3;
 if(gsr::fullUploadBlocks(0,12544,1,0,0,8,16,512,first,last))return 4;
 if(accepted<100)return 5;
 std::printf("PASS 20000 small-rectangle cases; %u accepted cases exhaustively cover every byte exactly once; combat palette and noncontiguous rejection verified\n",accepted);
}
