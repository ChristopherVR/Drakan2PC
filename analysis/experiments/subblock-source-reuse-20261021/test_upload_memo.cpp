#define main existingSyntheticMain
#include "test_synthetic.cpp"
#undef main
#include "gs_mem.h"
int main(){uint64_t pixels=0;unsigned callbacks=0;
for(unsigned scale:{1u,4u})for(unsigned psm:{0u,19u}) {
 constexpr unsigned bp=0x2000;unsigned w=psm==0?128:256,bw=psm==0?2:4,bpp=psm==0?4:1;
 std::vector<uint8_t> payload(65536);for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i*29+(i>>8)*17+37);
 Config cfg;cfg.scale=scale;cfg.vsync=false;cfg.onUpload=[&](const UploadEvent& e){if(e.bytes!=payload.size() || std::memcmp(e.data,payload.data(),payload.size()))std::abort();++callbacks;};
 auto gs=Renderer::create(cfg);if(!gs)return 1;
 LocalMemory ref;for(unsigned i=0;i<w*w;++i){uint32_t v=0;for(unsigned k=0;k<bpp;++k)v|=uint32_t(payload[i*bpp+k])<<(8*k);ref.write(psm,bp,bw,i%w,i/w,v);}
 auto setup=[&](){Gif g;g.ad({{FRAME_1,bp/32|(2ull<<16)},{ZBUF_1,140ull|(1ull<<32)},{XYOFFSET_1,kOfs},{SCISSOR_1,127ull<<16|127ull<<48},{TEST_1,mkTest(0,1,0,0,0,0,0,1)},{COLCLAMP,1},{PRMODECONT,1}});gs->submitGifPacket(GifPath::Path3,g.d.data(),uint32_t(g.d.size()));};
 auto upload=[&](bool split=false){Gif g;uploadImage(g,bp,bw,psm,0,0,w,w,payload);if(split){size_t cut=g.d.size()-32768;gs->submitGifPacket(GifPath::Path3,g.d.data(),uint32_t(cut));gs->submitGifPacket(GifPath::Path3,g.d.data()+cut,uint32_t(g.d.size()-cut));}else gs->submitGifPacket(GifPath::Path3,g.d.data(),uint32_t(g.d.size()));};
 auto verify=[&](){Image im;if(!gs->captureFramebuffer(bp/32,2,0,128,128,im))return false;for(unsigned y=0;y<128*scale;++y)for(unsigned x=0;x<128*scale;++x){uint32_t v;std::memcpy(&v,&im.rgba[(size_t(y)*im.width+x)*4],4);if(v!=(ref.read(0,bp,2,x/scale,y/scale)|0xff000000u))return false;++pixels;}return true;};
 setup();for(unsigned i=0;i<12;++i)upload(i%2);if(!verify())return 2;
 Gif draw;sprite(draw,0,0,128,128,11,23,37,128,0,int(mkPrim(6,0,0,0,0,0)));gs->submitGifPacket(GifPath::Path3,draw.d.data(),uint32_t(draw.d.size()));
 upload();if(!verify()){std::printf("GPU overwrite reuse failed\n");return 3;}
 gs->writeVram(0,bp,2,3,7,0x12345678);upload();if(!verify()){std::printf("CPU stamp invalidation failed\n");return 4;}
 gs->reset();setup();upload();if(!verify()){std::printf("reset invalidation failed\n");return 5;}
 gs->flushToMemory();if(std::memcmp(gs->vram()+bp*256,ref.data()+bp*256,65536))return 6;
}
std::printf("PASS %llu GPU pixel comparisons, 4 exact 64KiB ranges, %u upload callbacks; fragmented reuse, GPU overwrite, CPU write, and reset invalidation\n",pixels,callbacks);
}
