#define main existingSyntheticMain
#include "test_synthetic.cpp"
#undef main
#include "gs_mem.h"
int main(){uint64_t comparisons=0;
for(unsigned scale:{1u,4u})for(unsigned mode=0;mode<13;++mode){
 Config cfg;cfg.scale=scale;cfg.vsync=false;
 auto gs=Renderer::create(cfg);if(!gs)return 1;
 gs->reset();auto priorReadbacks=gs->stats().rtReadbacks;auto* raw=mode==10?gs->vram():nullptr;
 constexpr unsigned bp=0x2000,fbp=bp/32;
 Gif g;g.ad({{FRAME_1,fbp|(2ull<<16)},{ZBUF_1,140ull|(1ull<<32)},{XYOFFSET_1,kOfs},{SCISSOR_1,127ull<<16|127ull<<48},{TEST_1,mkTest(0,1,0,0,0,0,0,1)},{COLCLAMP,1},{PRMODECONT,1}});
 sprite(g,0,0,128,128,16,32,64,128,0,int(mkPrim(6,0,0,0,0,0)));
 gs->submitGifPacket(GifPath::Path3,g.d.data(),uint32_t(g.d.size()));
 unsigned psm=mode==1?19:mode==6?1:0,bw=mode==1?4:2,w=mode==1?256:mode==2?16:128,h=mode==1?256:mode==2?16:128,bpp=psm==19?1:psm==1?3:4;
 std::vector<uint8_t> bytes(size_t(w)*h*bpp);for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(i*17+29);
 Gif upload;upload.ad({{BITBLTBUF,uint64_t(bp)<<32|uint64_t(bw)<<48|uint64_t(psm)<<56},{TRXPOS,0},{TRXREG,w|(uint64_t(h)<<32)},{TRXDIR,0}});
 if(mode==5)upload.ad({{TEXFLUSH,0}});
 const size_t header=upload.d.size();upload.image(bytes);
 if(mode==3)upload.d.resize(upload.d.size()-bytes.size()/2);
 if(mode>=9){size_t cut=header+16+bytes.size()/2;gs->submitGifPacket(GifPath::Path3,upload.d.data(),uint32_t(cut));
  if(mode==9){if(gs->readVram(0,bp,2,0,0)!=0x503f2e1d || gs->readVram(0,bp,2,0,127)!=0x80402010)return 10;}
  if(mode==10){uint32_t direct=0x12345678;std::memcpy(raw+bp*256,&direct,4);gs->vramTouched(bp,1);}
  if(mode==11)gs->writeRegister(TEXFLUSH,0);
  if(mode==12)gs->writeRegister(TRXDIR,3);
  else gs->submitGifPacket(GifPath::Path3,upload.d.data()+cut,uint32_t(upload.d.size()-cut));
 }
 else if(mode==7){size_t cut=header+16+bytes.size()/2;gs->submitGifPacket(GifPath::Path3,upload.d.data(),uint32_t(cut));gs->submitGifPacket(GifPath::Path3,upload.d.data()+cut,uint32_t(upload.d.size()-cut));}
 else if(mode==8){gs->submitGifPacket(GifPath::Path3,upload.d.data(),uint32_t(header));if(gs->readVram(0,bp,2,0,0)!=0x80402010)return 9;gs->submitGifPacket(GifPath::Path3,upload.d.data()+header,uint32_t(upload.d.size()-header));}
 else if(mode==4){gs->submitGifPacket(GifPath::Path3,upload.d.data(),uint32_t(header));gs->submitGifPacket(GifPath::Path3,upload.d.data()+header,uint32_t(upload.d.size()-header));}
 else gs->submitGifPacket(GifPath::Path3,upload.d.data(),uint32_t(upload.d.size()));
 if(mode==3)gs->flushToMemory();
 auto readbacks=gs->stats().rtReadbacks-priorReadbacks;
 if(((mode<2 || mode==4 || mode==7) && readbacks!=0)||((mode>=2 && mode!=4 && mode!=7) && readbacks==0)){std::printf("bad readback policy mode=%u scale=%u count=%llu\n",mode,scale,(unsigned long long)readbacks);return 2;}
 LocalMemory ref;for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)ref.write(0,bp,2,x,y,0x80402010);
 const auto count=(mode==3 || mode==12)?size_t(w)*h/2:size_t(w)*h;
 for(size_t i=0;i<count;++i){uint32_t v=0;for(unsigned k=0;k<bpp;++k)v|=uint32_t(bytes[i*bpp+k])<<(8*k);ref.write(psm,bp,bw,unsigned(i%w),unsigned(i/w),v);}
 if(mode==10)ref.write(0,bp,2,0,0,0x12345678);
 gs->flushToMemory();
 if(std::memcmp(gs->vram()+bp*256,ref.data()+bp*256,65536)){std::printf("VRAM mismatch mode=%u scale=%u\n",mode,scale);return 3;}
 Image image;if(!gs->captureFramebuffer(fbp,2,0,128,128,image))return 4;
 for(unsigned y=0;y<128*scale;++y)for(unsigned x=0;x<128*scale;++x){uint32_t v;std::memcpy(&v,&image.rgba[(size_t(y)*image.width+x)*4],4);if(v!=(ref.read(0,bp,2,x/scale,y/scale)|0xff000000u)){std::printf("GPU mismatch mode=%u scale=%u x=%u y=%u actual=%08x expected=%08x\n",mode,scale,x,y,v,ref.read(0,bp,2,x/scale,y/scale));return 5;}++comparisons;}
}
std::printf("PASS %llu exact rendered RGBA comparisons; full, aliased, partial, truncated, fragmented, interposed, and CT24 uploads\n",comparisons);
}


