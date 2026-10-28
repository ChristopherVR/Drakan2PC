// Included inside drakan_input_glue.cpp's anonymous namespace after RecompGuest.
// Observe actual Map image submissions; preserve every chained native call.
struct MapImageScope {
    bool open=false;
    uint32_t section=0,index=0,image=0,entrySp=0;
    int frame=-1;
};
MapImageScope g_mapImages[64];
PS2Runtime::RecompiledFunction g_origMapDraw=nullptr,g_origMapImage=nullptr,g_origMapPrimitive=nullptr;
bool mapMouseAllowed(uint8_t* ram) {
    return activeScreenId(ram)==21 && module().frame().focused && !module().suspended();
}
MapImageScope* mapImageScope(PS2Runtime* rt) {
    const int thread=rt->eeScheduler().currentThreadId();
    return thread>=0 && thread<64?&g_mapImages[thread]:nullptr;
}
void hookMapDraw(uint8_t* ram,R5900Context* c,PS2Runtime* rt) {
    if(c->pc==0x439b28u) {
        if(auto* scope=mapImageScope(rt))*scope={};
        if(mapMouseAllowed(ram)) {
            RecompGuest g(ram,c,rt);
            module().menuMouseRw().beginInventoryMap(g,getRegU32(c,4));
        }
    }
    g_origMapDraw(ram,c,rt);
}
void hookMapImage(uint8_t* ram,R5900Context* c,PS2Runtime* rt) {
    auto* scope=mapImageScope(rt);
    const uint32_t sp=getRegU32(c,29)&0x1fffffffu;
    if(c->pc==0x1c1e90u && scope) {
        *scope={};
        // 439b28's actual marker image, not its background/animated border.
        // Native f13=-1 means use f12 for height (1c1f04..18), not flip Y.
        if(getRegU32(c,31)==0x43a330u && mapMouseAllowed(ram) && c->f[12]==1 && c->f[13]==-1) {
            scope->open=true;scope->section=getRegU32(c,22);scope->index=getRegU32(c,19);
            scope->image=getRegU32(c,4)&0x1fffffffu;scope->entrySp=sp;scope->frame=module().frameNo();
        }
    }
    g_origMapImage(ram,c,rt);
    if(scope && scope->entrySp==sp && c->pc==0x43a330u)scope->open=false;
}
void hookMapPrimitive(uint8_t* ram,R5900Context* c,PS2Runtime* rt) {
    auto* scope=mapImageScope(rt);
    if(c->pc==0x1d9de8u && scope && scope->open && mapMouseAllowed(ram) &&
       scope->frame==module().frameNo()) {
        const uint32_t ra=getRegU32(c,31),sp=getRegU32(c,29)&0x1fffffffu;
        const bool sprite=ra==0x1c2574u && sp+0x170u==scope->entrySp &&
                          getRegU32(c,5)==6 && getRegU32(c,7)==2;
        const bool strip=ra==0x1468a0u && sp+0x180u==scope->entrySp &&
                         getRegU32(c,5)==5 && getRegU32(c,7)==4;
        if(sprite || strip) {
            RecompGuest g(ram,c,rt);
            auto& mouse=module().menuMouseRw();
            const unsigned before=mouse.debug().mapCaptures;
            mouse.captureInventoryMapMarker(g,scope->section,scope->index,
                scope->image,getRegU32(c,6)&0x1fffffffu,getRegU32(c,7));
            static unsigned capturesLogged=0;
            if(mouse.debug().mapCaptures!=before && capturesLogged++<8)
                ilog("[in-map] native marker frame=%d section=%x index=%u image=%x count=%u capture=%u\n",
                    module().frameNo(),scope->section,scope->index,scope->image,getRegU32(c,7),mouse.debug().mapCaptures);
        }
    }
    g_origMapPrimitive(ram,c,rt);
}
void installMapMouseHooks(PS2Runtime& rt) {
    g_origMapDraw=rt.lookupFunction(0x439b28u);
    g_origMapImage=rt.lookupFunction(0x1c1e90u);
    g_origMapPrimitive=rt.lookupFunction(0x1d9de8u);
    // All hooks are required; otherwise Map clicks remain inert.
    if(g_origMapDraw && g_origMapImage && g_origMapPrimitive) {
        rt.replaceFunction(0x439b28u,&hookMapDraw);
        rt.replaceFunction(0x1c1e90u,&hookMapImage);
        rt.replaceFunction(0x1d9de8u,&hookMapPrimitive);
    }
}
