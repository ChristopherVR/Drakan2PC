#define main existingSyntheticMain
#include "test_synthetic.cpp"
#undef main
int main() {
 Config cfg;cfg.scale=1;cfg.vsync=false;
 std::vector<std::vector<uint32_t>> decoded;
 cfg.onTexture=[&](const TextureEvent& e){decoded.clear();for(unsigned l=0;l<e.mipLevels;++l)decoded.emplace_back(e.level[l],e.level[l]+e.levelW[l]*e.levelH[l]);};
 auto gs=Renderer::create(cfg);if(!gs)return 1;
 std::vector<uint8_t> base(32*32*4),mip(16*16*4);
 for(size_t i=0;i<base.size();++i)base[i]=uint8_t(i*31+(i>>8)*17);
 for(size_t i=0;i<mip.size();++i)mip[i]=uint8_t(i*13+(i>>8)*23);
 Gif setup;setup.ad({{FRAME_1,1ull<<16},{ZBUF_1,140ull|(1ull<<32)},{XYOFFSET_1,kOfs},{SCISSOR_1,63ull<<16|63ull<<48},{TEST_1,mkTest(0,1,0,0,0,0,0,1)},{COLCLAMP,1},{PRMODECONT,1}});
 gs->submitGifPacket(GifPath::Path3,setup.d.data(),uint32_t(setup.d.size()));
 unsigned checks=0,drawSize=32;uint64_t clamp=0;
 auto run=[&](){Gif g;uploadImage(g,0x1000,1,0,0,0,32,32,base);uploadImage(g,0x1400,1,0,0,0,16,16,mip);
  g.ad({{TEX1_1,4},{CLAMP_1,clamp},{0x34,0x1400|(1ull<<14)},{TEX0_1,mkTex0(0x1000,1,0,5,5,1,1,0,0,0,0,0)}});
  sprite(g,0,0,float(drawSize),float(drawSize),128,128,128,128,0,int(mkPrim(6,0,1,0,0,1)),0,0,32,32);
  gs->submitGifPacket(GifPath::Path3,g.d.data(),uint32_t(g.d.size()));Image img;
  if(!gs->captureFramebuffer(0,1,0,32,32,img))return false;
  if(decoded.size()!=2 || decoded[0].size()*4!=base.size() || decoded[1].size()*4!=mip.size() || std::memcmp(decoded[0].data(),base.data(),base.size()) || std::memcmp(decoded[1].data(),mip.data(),mip.size()))return false;
  checks+=1280;return true;
 };
 for(unsigned i=0;i<12;++i)if(!run())return 2;
 auto decodes=gs->stats().textureDecodes;
 if(decodes>=12){std::printf("source reuse inactive: %llu decodes\n",decodes);return 3;}
 mip[17]^=0x59;if(!run() || gs->stats().textureDecodes!=decodes+1)return 4;
 decodes=gs->stats().textureDecodes;
 if(!run() || gs->stats().textureDecodes!=decodes)return 5;
 base[1025]^=0x37;if(!run() || gs->stats().textureDecodes!=decodes+1)return 6;
 decodes=gs->stats().textureDecodes;clamp=5;if(!run() || gs->stats().textureDecodes!=decodes+1)return 9;
 decodes=gs->stats().textureDecodes;drawSize=16;if(!run() || gs->stats().textureDecodes!=decodes+1)return 10;
 for(unsigned i=0;i<95;++i)gs->vsync(false);
 decodes=gs->stats().textureDecodes;if(!run() || gs->stats().textureDecodes!=decodes+1)return 7;
 cfg.onTexture={};texFormatTests(cfg);if(g_fail)return 8;
 std::printf("PASS %u exact mip pixels; unchanged upload reuse, changed base/mip invalidation, volatility-window reset; %d texture-format assertions\n",checks,g_checks);
}
