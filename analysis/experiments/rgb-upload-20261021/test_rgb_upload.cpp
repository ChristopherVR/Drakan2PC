#define main existingSyntheticMain
#include "test_synthetic.cpp"
#undef main
#include "gs_mem.h"
int main() {
 uint64_t pixels=0;unsigned ranges=0,textureChecks=0;
 for(unsigned scale:{1u,4u})for(unsigned mode=0;mode<13;++mode) {
  if(const char* min=std::getenv("GSR_TEST_SUBBLOCK_MIN"))if(mode<unsigned(std::atoi(min)))continue;
  constexpr unsigned base=0x2000;const unsigned bp=base+(mode%2?193:146);
  std::vector<uint32_t> decoded;
  Config cfg;cfg.scale=scale;cfg.vsync=false;
  cfg.onTexture=[&](const TextureEvent& e){if(e.width==64 && e.height==64 && e.level[0])decoded.assign(e.level[0],e.level[0]+4096);};
  auto gs=Renderer::create(cfg);if(!gs)return 1;
  LocalMemory ref;
  auto submit=[&](Gif& g){gs->submitGifPacket(GifPath::Path3,g.d.data(),uint32_t(g.d.size()));};
  Gif draw;draw.ad({{FRAME_1,base/32|(2ull<<16)},{ZBUF_1,140ull|(1ull<<32)},{XYOFFSET_1,kOfs},{SCISSOR_1,127ull<<16|127ull<<48},{TEST_1,mkTest(0,1,0,0,0,0,0,1)},{COLCLAMP,1},{PRMODECONT,1}});
  for(unsigned q=0;q<4;++q){unsigned x=(q&1)*64,y=(q>>1)*64;uint32_t c=0x20402010+q*0x20070503;
   sprite(draw,float(x),float(y),float(x+64),float(y+64),c&255,(c>>8)&255,(c>>16)&255,c>>24,0,int(mkPrim(6,0,0,0,0,0)));
   for(unsigned yy=y;yy<y+64;++yy)for(unsigned xx=x;xx<x+64;++xx)ref.write(0,base,2,xx,yy,c);
  }submit(draw);
  std::vector<uint8_t> data(64*64*3);for(unsigned i=0;i<data.size();++i)data[i]=uint8_t(i*37+19);
  auto value=[&](unsigned i){return uint32_t(data[i*3])|(uint32_t(data[i*3+1])<<8)|(uint32_t(data[i*3+2])<<16);};
  Gif upload;uploadImage(upload,bp,1,1,0,0,64,64,data);
  if(mode==1 || mode==2){const size_t cut=upload.d.size()-48;gs->submitGifPacket(GifPath::Path3,upload.d.data(),uint32_t(cut));
   if(mode==2){for(unsigned i=0;i<4080;++i)ref.write(1,bp,1,i%64,i/64,value(i));if(gs->readVram(0,base,2,0,0)!=ref.read(0,base,2,0,0))return 2;}
   gs->submitGifPacket(GifPath::Path3,upload.d.data()+cut,uint32_t(upload.d.size()-cut));
  }else submit(upload);
  for(unsigned i=0;i<4096;++i)ref.write(1,bp,1,i%64,i/64,value(i));
  if(mode!=2 && gs->stats().rtReadbacks){std::printf("premature readback mode=%u\n",mode);return 3;}
  if(mode>=10){
   unsigned other=base+(bp==base+146?247:146);std::vector<uint8_t> second(64);for(unsigned i=0;i<64;++i)second[i]=uint8_t(i*19+71);
   Gif more;uploadImage(more,other,1,19,0,0,8,8,second);submit(more);for(unsigned i=0;i<64;++i)ref.write(19,other,1,i%8,i/8,second[i]);
   if(mode==10){std::vector<uint8_t> whole(256);for(unsigned i=0;i<256;++i)whole[i]=uint8_t(i*13+91);Gif full;uploadImage(full,base+1,2,0,0,0,8,8,whole);submit(full);for(unsigned i=0;i<64;++i){uint32_t v;std::memcpy(&v,&whole[i*4],4);ref.write(0,base+1,2,i%8,i/8,v);}}
  }
  if(mode==3 || mode==4 || mode==12){std::vector<uint8_t> replacement(mode==3?256:65536);for(size_t i=0;i<replacement.size();++i)replacement[i]=uint8_t(i*29+(i>>8)*11);
   Gif full;unsigned w=mode==3?8:128,h=w,dbp=mode==3?bp:base;uploadImage(full,dbp,2,0,0,0,w,h,replacement);submit(full);
   for(unsigned i=0;i<w*h;++i){uint32_t v;std::memcpy(&v,&replacement[i*4],4);ref.write(0,dbp,2,i%w,i/w,v);}
  }
  if(mode==5){auto* raw=gs->vram();if(std::memcmp(raw+base*256,ref.data()+base*256,65536))return 4;raw[bp*256+2]^=0x5a;ref.data()[bp*256+2]^=0x5a;gs->vramTouched(bp,1);}
  if(mode==6 && (gs->readVram(1,bp,1,63,63)&0xffffff)!=value(4095))return 5;
  if(mode==7){Gif next;sprite(next,32,32,96,96,85,102,119,128,0,int(mkPrim(6,0,0,0,0,0)));submit(next);
   for(unsigned y=32;y<96;++y)for(unsigned x=32;x<96;++x)ref.write(0,base,2,x,y,0x80776655);
  }
  if(mode==8){Gif tex;
   tex.ad({{FRAME_1,0x3000/32|(2ull<<16)},{TEX1_1,0},{TEXA,0x80},{CLAMP_1,0},{TEX0_1,mkTex0(bp,1,1,6,6,1,1,0,0,0,0,0)}});
   for(unsigned pass=0;pass<2;++pass){sprite(tex,0,0,64,64,128,128,128,128,0,int(mkPrim(6,0,1,0,0,1)),0,0,64,64);submit(tex);tex.d.clear();Image screen;if(!gs->captureFramebuffer(0x3000/32,2,0,64,64,screen))return 6;
    if(decoded.size()!=4096)return 7;for(unsigned i=0;i<4096;++i){if(decoded[i]!=(0x80000000u|value(i))){std::printf("decode mismatch %u %08x\n",i,decoded[i]);return 8;}++textureChecks;}
    if(gs->stats().rtReadbacks){std::printf("texture forced readback\n");return 9;}
   }
  }
  if(mode==9){for(auto& b:data)b^=0x6d;Gif repeat;uploadImage(repeat,bp,1,1,0,0,64,64,data);submit(repeat);for(unsigned i=0;i<4096;++i)ref.write(1,bp,1,i%64,i/64,value(i));}
  Image image;if(!gs->captureFramebuffer(base/32,2,0,128,128,image))return 10;
  if((mode==4 || mode==12) && gs->stats().rtReadbacks){std::printf("superseded upload forced readback\n");return 11;}
  for(unsigned y=0;y<128*scale;++y)for(unsigned x=0;x<128*scale;++x){uint32_t got;std::memcpy(&got,&image.rgba[(size_t(y)*image.width+x)*4],4);uint32_t expected=ref.read(0,base,2,x/scale,y/scale)|0xff000000u;if(got!=expected){std::printf("pixel mismatch mode=%u scale=%u x=%u y=%u actual=%08x expected=%08x\n",mode,scale,x,y,got,expected);return 12;}++pixels;}
  gs->flushToMemory();if(std::memcmp(gs->vram()+base*256,ref.data()+base*256,65536)){std::printf("VRAM mismatch mode=%u scale=%u\n",mode,scale);return 13;}++ranges;
  std::printf("PASS case mode=%u scale=%u\n",mode,scale);std::fflush(stdout);
 }
 std::printf("PASS %llu GPU pixel comparisons, %u exact 64KiB ranges, %u decoded texture pixels\n",pixels,ranges,textureChecks);
}

