// Texture cache: decode, HD texture pack, background upscaling and replacement.
#include "d3d11_backend_impl.h"

namespace gsr {

bool D3D11Backend::Impl::iequals(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

void D3D11Backend::Impl::initTexQuality() {
    upFactorCfg = cfg.textureUpscale;
    upAlgoCfg = (texup::Algo)std::min(cfg.textureUpscaleFilter, 2u);
    aniso = cfg.anisotropic;
    userMipBias = cfg.mipBias;
    mipBias = cfg.mipBiasAuto ? -std::log2((float)scale) : cfg.mipBias;
    texDump = cfg.textureDump;
    texReplace = cfg.textureReplace;
    hudScale = cfg.hudScaleX > 0.2f && cfg.hudScaleX <= 1.0f ? cfg.hudScaleX : 1.0f;
    smoothHL = cfg.smoothMenuHighlight;
    if (runtimeFlags().hudScale)
        hudScale = *runtimeFlags().hudScale;
    if (runtimeFlags().smoothHighlight)
        smoothHL = *runtimeFlags().smoothHighlight;
    texDir = cfg.textureDir.empty() ? "textures" : cfg.textureDir;
    upSync = cfg.textureUpscaleSync;
    texBudget = (uint64_t)std::max(64u, cfg.textureBudgetMB ? cfg.textureBudgetMB : 1536u) << 20;
    texBudgetAuto = cfg.textureBudgetMB == 0; // 0 = 40 % of the dedicated VRAM (set when the adapter is known)
    const RuntimeFlags& f = runtimeFlags();
    if (f.texUpFactor)
        upFactorCfg = *f.texUpFactor;
    if (f.texUpFilter)
        upAlgoCfg = (texup::Algo)*f.texUpFilter;
    if (f.aniso)
        aniso = *f.aniso;
    if (f.mipBias) {
        userMipBias = f.mipBias->value;
        mipBias = f.mipBias->automatic ? -std::log2((float)scale) : userMipBias;
    }
    if (f.texDump)
        texDump = *f.texDump;
    if (f.texReplace)
        texReplace = *f.texReplace;
    if (f.texDir)
        texDir = *f.texDir;
    if (f.texBudget) {
        texBudget = (uint64_t)std::max(64, *f.texBudget) << 20;
        texBudgetAuto = *f.texBudget <= 0;
    }
    if (f.texUpSync)
        upSync = *f.texUpSync;
    if (f.paletteHd)
        texPaletteHd = *f.paletteHd;
    upLog = f.texUpLog;
    upFactorCfg = normUpFactor(upFactorCfg);
    aniso = aniso > 16 ? 16 : aniso;
    scanTexDir();
    ensurePool();
}

uint32_t D3D11Backend::Impl::normUpFactor(uint32_t f) {
    if (f != 0 && f != 1 && f != 2 && f != 4 && f != 8)
        f = f > 8 ? 8 : (f >= 3 ? 4 : 1);
    return f;
}

void D3D11Backend::Impl::ensurePool() {
    const bool anyJob = upFactorCfg != 1 || texDump || (texReplace && !replIndex.empty());
    if (anyJob && !upSync && !pool) {
        const unsigned hc = std::max(2u, std::thread::hardware_concurrency());
        pool = std::make_unique<texup::Pool>(std::clamp(hc / 2, 1u, 4u));
    }
}

// (re)read the replacement index / exclude list / already dumped hashes from texDir
void D3D11Backend::Impl::scanTexDir() {
    replIndex.clear();
    aliasIndex.clear();
    excluded.clear();
    dumped.clear();
    dumpDir = texDir + "/dump";
    if (texReplace || texDump) {
        for (const std::string& f : texup::listDir(texDir)) {
            if (f.size() < 20)
                continue;
            const std::string ext = f.substr(f.size() - 4);
            if (!iequals(ext.c_str(), ".png") && !iequals(ext.c_str(), ".dds"))
                continue;
            char* end = nullptr;
            const std::string head = f.substr(0, 16);
            const uint64_t h = std::strtoull(head.c_str(), &end, 16);
            if (end == head.c_str() + 16)
                replIndex[h] = texDir + "/" + f;
        }
        if (FILE* af = std::fopen((texDir + "/alias.txt").c_str(), "r")) {
            char line[256];
            while (std::fgets(line, sizeof(line), af))
                if (line[0] != '#' && std::strlen(line) >= 33)
                    aliasIndex[std::strtoull(line, nullptr, 16)] = std::strtoull(line + 17, nullptr, 16);
            std::fclose(af);
        }
        if (FILE* ef = std::fopen((texDir + "/exclude.txt").c_str(), "r")) {
            char line[256];
            while (std::fgets(line, sizeof(line), ef))
                if (line[0] != '#' && std::strlen(line) >= 16)
                    excluded.insert(std::strtoull(line, nullptr, 16));
            std::fclose(ef);
        }
    }
    if (texDump) {
        texup::makeDirs(dumpDir);
        for (const std::string& f : texup::listDir(dumpDir))
            if (f.size() >= 20)
                dumped.insert(std::strtoull(f.substr(0, 16).c_str(), nullptr, 16));
    }
}

// auto: scale 1 -> off, 2-3 -> 4x, 4+ -> 8x (UI sprites: always the largest factor, they are tiny); every factor is
// capped by the texture budget (one texture with its mips may use at most 1/24 of it) and 4096 px
uint32_t D3D11Backend::Impl::pickFactor(uint32_t W, uint32_t H, bool ui) const {
    uint32_t n = upFactorCfg;
    if (n == 0)
        n = scale >= 4 ? 8u : (scale >= 2 ? 4u : 1u);
    if (n > 1 && ui && upFactorCfg == 0)
        n = 8;
    while (n > 1 && (W * n > 4096 || H * n > 4096 || (uint64_t)W * n * H * n * 5 / 3 > texBudget / 24))
        n >>= 1;
    return n;
}

void D3D11Backend::Impl::submitJob(std::function<void()> job, bool urgent) {
    if (pool)
        pool->submit(std::move(job), urgent);
    else
        job();
}

void D3D11Backend::Impl::pushDone(UpDone&& d) {
    {
        std::lock_guard<std::mutex> lk(doneMu);
        done.push_back(std::move(d));
    }
    doneCount.fetch_add(1);
}

// create the GPU texture of a finished job (render thread only)
void D3D11Backend::Impl::pumpUp() {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::PumpUp);
    if (doneCount.load() == 0)
        return;
    std::vector<UpDone> local;
    {
        std::lock_guard<std::mutex> lk(doneMu);
        local.swap(done);
        doneCount.store(0);
    }
    for (UpDone& d : local) {
        if (d.gen != upGen)
            continue; // started before a live settings change flushed the caches
        auto ue = std::make_shared<UpEntry>();
        ue->factor = d.factor;
        ue->lastFrame = frame;
        if (!d.levels.empty() && d.w) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = d.w;
            td.Height = d.h;
            td.MipLevels = (UINT)d.levels.size();
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            std::vector<D3D11_SUBRESOURCE_DATA> init(d.levels.size());
            uint64_t bytes = 0;
            uint32_t lw = d.w, lh = d.h;
            for (size_t l = 0; l < d.levels.size(); ++l) {
                init[l].pSysMem = d.levels[l].data();
                init[l].SysMemPitch = lw * 4;
                bytes += (uint64_t)lw * lh * 4;
                lw = std::max(1u, lw / 2);
                lh = std::max(1u, lh / 2);
            }
            if (SUCCEEDED(dev->CreateTexture2D(&td, init.data(), &ue->tex)) &&
                SUCCEEDED(dev->CreateShaderResourceView(ue->tex.Get(), nullptr, &ue->srv))) {
                ue->bytes = bytes;
                upBytes += bytes;
                if (d.replaced)
                    st.texReplaced++;
                else {
                    st.texUpscaled++;
                    st.texUpscaleUs += d.us;
                    st.texUpscaleBytes += bytes;
                }
            } else {
                ue->srv.Reset();
                ue->tex.Reset();
                ue->factor = 1;
            }
        } else
            ue->factor = 1;
        upCache[d.key] = ue;
        upInflight.erase(d.key);
        if (st.texUpPending)
            st.texUpPending--;
    }
}

// does the texture entry have a ready upscaled version? (adopt it)
void D3D11Backend::Impl::adoptUp(TexEntry& e) {
    if (!e.upWanted || e.up)
        return;
    auto it = upCache.find(e.upKey);
    if (it == upCache.end())
        return;
    if (it->second->srv)
        e.up = it->second;
    e.upWanted = false; // a failed job (null srv) leaves the native texture
}

void D3D11Backend::Impl::resampleBilinear(const std::vector<uint32_t>& img, uint32_t rw, uint32_t rh, uint32_t tw,
                                          uint32_t th, std::vector<uint32_t>& out) {
    out.resize((size_t)tw * th);
    for (uint32_t y = 0; y < th; ++y)
        for (uint32_t x = 0; x < tw; ++x) {
            const float fx = std::max(0.0f, ((float)x + 0.5f) * rw / tw - 0.5f),
                        fy = std::max(0.0f, ((float)y + 0.5f) * rh / th - 0.5f);
            const uint32_t x0 = std::min((uint32_t)fx, rw - 1), y0 = std::min((uint32_t)fy, rh - 1),
                           x1 = std::min(x0 + 1, rw - 1), y1 = std::min(y0 + 1, rh - 1);
            const float ax = fx - (float)x0, ay = fy - (float)y0;
            uint32_t o = 0;
            for (int c = 0; c < 4; ++c) {
                auto g = [&](uint32_t xx, uint32_t yy) {
                    return (float)((img[(size_t)yy * rw + xx] >> (8 * c)) & 255);
                };
                const float v =
                    (g(x0, y0) * (1 - ax) + g(x1, y0) * ax) * (1 - ay) + (g(x0, y1) * (1 - ax) + g(x1, y1) * ax) * ay;
                o |= (uint32_t)(v + 0.5f) << (8 * c);
            }
            out[(size_t)y * tw + x] = o;
        }
}

// alias match: keep the RGB of the pack texture but take the alpha the game actually uses (bilinear upscale; binary
// alpha (alpha test cut-outs, foliage) is pushed to a ~2 px ramp so the edges stay crisp instead of fat and blurry)
void D3D11Backend::Impl::applyGameAlpha(std::vector<uint32_t>& hi, uint32_t tw, uint32_t th,
                                        const std::vector<uint32_t>& base, uint32_t W, uint32_t H) {
    uint32_t amax = 0;
    bool binary = true;
    for (uint32_t p : base)
        amax = std::max(amax, p >> 24);
    for (uint32_t p : base) {
        const uint32_t a = p >> 24;
        if (a != 0 && a != amax) {
            binary = false;
            break;
        }
    }
    std::vector<uint32_t> al(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        const uint32_t a = base[i] >> 24;
        al[i] = a | (a << 8) | (a << 16) | (a << 24);
    }
    std::vector<uint32_t> up;
    resampleBilinear(al, W, H, tw, th, up);
    for (size_t i = 0; i < hi.size(); ++i) {
        float a = (float)(up[i] & 255);
        if (binary && amax) {
            const float t = a / (float)amax;
            a = std::clamp((t - 0.5f) * 4.0f + 0.5f, 0.0f, 1.0f) * (float)amax;
        }
        hi[i] = (hi[i] & 0x00FFFFFFu) | ((uint32_t)(a + 0.5f) << 24);
    }
}

// separable [1 2 1] / 4 binomial blur (a 2x2 box filter convolved with itself, centred), all four channels, clamped
// edges
void D3D11Backend::Impl::smoothBinomial3(std::vector<uint32_t>& img, uint32_t w, uint32_t h) {
    std::vector<uint32_t> tmp(img.size());
    for (int pass = 0; pass < 2; ++pass) {
        const bool horiz = pass == 0;
        const std::vector<uint32_t>& src = horiz ? img : tmp;
        std::vector<uint32_t>& dst = horiz ? tmp : img;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const uint32_t xm = horiz ? (x ? x - 1 : 0) : x, xp = horiz ? (x + 1 < w ? x + 1 : x) : x;
                const uint32_t ym = horiz ? y : (y ? y - 1 : 0), yp = horiz ? y : (y + 1 < h ? y + 1 : y);
                const uint32_t a = src[(size_t)ym * w + xm], b = src[(size_t)y * w + x], c = src[(size_t)yp * w + xp];
                uint32_t o = 0;
                for (int ch = 0; ch < 4; ++ch) {
                    const uint32_t v =
                        (((a >> (8 * ch)) & 255) + 2 * ((b >> (8 * ch)) & 255) + ((c >> (8 * ch)) & 255) + 2) >> 2;
                    o |= v << (8 * ch);
                }
                dst[(size_t)y * w + x] = o;
            }
    }
}

// called when a texture was freshly decoded: dump / replace / upscale
void D3D11Backend::Impl::setupUpscale(TexEntry& e, const std::vector<uint32_t>& base, uint32_t W, uint32_t H,
                                      uint32_t mxl, const GsDrawState& s, bool ui, bool volatileSlot,
                                      const uint64_t* decodedHash) {
    const bool wantDumpReplace = texDump || (texReplace && !replIndex.empty());
    const uint32_t factor = pickFactor(W, H, ui);
    const uint64_t hash = decodedHash ? *decodedHash : texup::hashContent(base.data(), W, H);
    e.menuArtwork = hud::menuArtwork(hash);
    auto generated = runtimeTexture(hash);
    const auto preview = W == 128 && H == 128 ? savePreview(hash) : SavePreview{};
    e.previewVersion = preview.version;
    e.previewHash = hash;
    uint32_t generatedFactor = generated && W && H ? generated->width / W : 0;
    if (generated && ((generatedFactor != 2 && generatedFactor != 4 && generatedFactor != 8) ||
                      generated->width != W * generatedFactor || generated->height != H * generatedFactor))
        generated.reset();
    // Streaming can recycle a slot previously used for animated pixels.
    // Exact authored content still identifies the selected immutable font;
    // volatility must not send those glyphs back to the native atlas.
    // A known exact disk-pack image is equally immutable. Streaming UI
    // artwork through a recycled slot is not a palette animation.
    const auto policy = texup::contentPolicy(isTextureHashExcluded(hash), excluded.count(hash) != 0);
    // Keep volatile full-color effect uploads on their existing path:
    // an HD pack's orb-mask perimeter is not safe to expose as artwork.
    const bool exactReplacement = (isIndexed((uint32_t)bitsOf(s.tex0, 20, 6)) || e.menuArtwork) && texReplace &&
                                  policy.externalFiles && replIndex.count(hash);
    if (volatileSlot && !generated && !exactReplacement && preview.path.empty())
        return;
    if (factor <= 1 && !wantDumpReplace && !generated && preview.path.empty())
        return;
    if (!policy.automaticScale)
        return;
    e.fontAtlas = generated && generated->fontAtlas;
    if (texDump && policy.externalFiles && !dumped.count(hash)) {
        dumped.insert(hash);
        std::vector<uint32_t> copy = base;
        const std::string path = dumpDir + "/" + texup::hashName(hash) + ".png";
        st.texDumped++;
        bool allA0 = true; // constant alpha 0 (CT24 / TEXA 0): the PNG still holds the real RGB; the marker tells
                           // external tools never to premultiply it
        for (uint32_t px : copy)
            if (px >> 24) {
                allA0 = false;
                break;
            }
        submitJob([copy = std::move(copy), W, H, path, allA0] {
            texup::writePngRGBA(path, copy.data(), W, H);
            if (allA0)
                if (std::FILE* mf = std::fopen((path.substr(0, path.size() - 4) + ".a0").c_str(), "wb"))
                    std::fclose(mf);
        });
    }
    const bool wrapS = bitsOf(s.clamp, 0, 2) == 0, wrapT = bitsOf(s.clamp, 2, 2) == 0;
    UpKey key{};
    key.hash = hash;
    auto rit = texReplace && policy.externalFiles ? replIndex.find(hash) : replIndex.end();
    bool viaAlias = false;
    if (texReplace && policy.externalFiles && rit == replIndex.end() && !replIndex.empty()) {
        // same picture, other alpha (the game edits palette alpha at load time): look it up by the RGB-only hash
        const uint64_t rgbHash = texup::hashContentRgb(base.data(), W, H);
        const auto ai = aliasIndex.find(rgbHash);
        if (ai != aliasIndex.end()) {
            rit = replIndex.find(ai->second);
            viaAlias = rit != replIndex.end();
        }
        if (!viaAlias) {
            rit = replIndex.find(rgbHash);
            viaAlias = rit != replIndex.end();
        } // pack texture whose own alpha was 0
    }
    const bool replace = !preview.path.empty() || rit != replIndex.end();
    // An overlay's own hash is excluded; its base is the pack image of the disc texture it was generated from.
    std::string overlayPack;
    bool overlayAlias = false;
    if (generated && !generated->coverage.empty() && texReplace) {
        auto pit = replIndex.find(generated->packHash);
        if (pit == replIndex.end()) {
            const auto ai = aliasIndex.find(generated->packRgbHash);
            pit = replIndex.find(ai != aliasIndex.end() ? ai->second : generated->packRgbHash);
            overlayAlias = pit != replIndex.end();
        }
        if (pit != replIndex.end() && !excluded.count(generated->packHash))
            overlayPack = pit->second;
    }
    if (!preview.path.empty()) {
        key.hash ^= preview.version;
        viaAlias = true;
    }
    texup::Algo algo = upAlgoCfg;
    if (ui)
        algo = texup::Algo::XBrz; // HUD / font / sprite textures at 1:1 texel mapping stay crisp (pixel-art scaler)
    if (isPhotographicTexture(hash))
        algo = texup::Algo::Smooth;
    if (generated) {
        key.factor = generatedFactor;
        key.packed = (mxl << 12) | (2u << 16) | (overlayPack.empty() ? 0u : (1u << 18));
    } else if (replace) {
        key.factor = 0; // resolved by the job
        key.packed = (mxl << 12) | (1u << 16) | (viaAlias ? (1u << 17) : 0u);
    } else {
        if (factor <= 1 || texup::looksUninitialized(base.data(), W, H))
            return;
        key.factor = factor;
        key.packed = (uint32_t)algo | ((wrapS ? 1u : 0u) << 8) | ((wrapT ? 1u : 0u) << 9) | (mxl << 12);
    }
    e.upKey = key;
    e.upWanted = true;
    adoptUp(e);
    if (!e.upWanted || upInflight.count(key))
        return;
    upInflight.insert(key);
    st.texUpPending++;
    const std::string rpath = !preview.path.empty() ? preview.path : (replace ? rit->second : std::string());
    if (upLog && e.menuArtwork)
        std::fprintf(stderr, "[texup] menu artwork: %s\n", rpath.c_str());
    std::vector<uint32_t> copy = base;
    const uint32_t gen = upGen;
    auto job = [this, gen, key, copy = std::move(copy), W, H, mxl, factor, algo, wrapS, wrapT, replace, rpath, viaAlias,
                generated, generatedFactor, preview, overlayPack, overlayAlias] {
        const auto t0 = std::chrono::steady_clock::now();
        UpDone d;
        d.key = key;
        d.gen = gen;
        d.replaced = replace;
        std::vector<uint32_t> b0;
        uint32_t N = factor;
        texup::Algo used = algo;
        if (generated && !generated->coverage.empty()) {
            N = generatedFactor;
            const uint32_t tw = W * N, th = H * N;
            std::vector<uint32_t> img;
            uint32_t rw = 0, rh = 0;
            if (!overlayPack.empty() && texup::readImageFile(overlayPack, img, rw, rh) && rw && rh) {
                if (rw == tw && rh == th)
                    b0 = std::move(img);
                else
                    resampleBilinear(img, rw, rh, tw, th, b0);
                if (overlayAlias)
                    applyGameAlpha(b0, tw, th, copy, W, H);
            } else
                texup::upscale(copy.data(), W, H, N, texup::Algo::XBrz, wrapS, wrapT, b0);
            for (size_t i = 0; i < b0.size(); ++i) {
                const uint32_t c = generated->coverage[i];
                if (!c)
                    continue;
                const uint32_t a = b0[i], b = generated->pixels[i];
                uint32_t o = 0;
                for (int ch = 0; ch < 32; ch += 8)
                    o |= ((((a >> ch) & 255) * (255 - c) + ((b >> ch) & 255) * c + 127) / 255) << ch;
                b0[i] = o;
            }
        } else if (generated) {
            N = generatedFactor;
            b0 = generated->pixels;
        } else if (replace) {
            std::vector<uint32_t> img;
            uint32_t rw = 0, rh = 0;
            if (texup::readImageFile(rpath, img, rw, rh) && rw && rh) {
                if (preview.flipY)
                    for (uint32_t y = 0; y < rh / 2; ++y)
                        for (uint32_t x = 0; x < rw; ++x)
                            std::swap(img[size_t(y) * rw + x], img[size_t(rh - 1 - y) * rw + x]);
                const double ratio = std::max((double)rw / W, (double)rh / H);
                N = 1;
                while (N < 8 && (double)N * 1.4142 < ratio)
                    N *= 2;
                while (N > 1 && (W * N > 8192 || H * N > 8192))
                    N >>= 1;
                const uint32_t tw = W * N, th = H * N;
                if (rw == tw && rh == th)
                    b0 = std::move(img);
                else
                    resampleBilinear(img, rw, rh, tw, th, b0);
                if (viaAlias)
                    applyGameAlpha(b0, tw, th, copy, W, H);
            } else {
                N = 1;
                std::fprintf(stderr, "[gsr] texture replacement '%s' could not be loaded\n", rpath.c_str());
                if (!preview.path.empty()) {
                    N = factor;
                    texup::upscale(copy.data(), W, H, N, texup::Algo::Smooth, wrapS, wrapT, b0);
                }
            }
        } else {
            if (used == texup::Algo::Auto)
                used = texup::classify(texup::analyse(copy.data(), W, H));
            texup::upscale(copy.data(), W, H, N, used, wrapS, wrapT, b0);
        }
        if (!b0.empty()) {
            d.factor = N;
            d.w = W * N;
            d.h = H * N;
            d.levels.push_back(std::move(b0));
            uint32_t lw = d.w, lh = d.h;
            for (uint32_t l = 1; l <= mxl; ++l) {
                std::vector<uint32_t> nx;
                texup::downsample2x(d.levels.back().data(), lw, lh, nx);
                lw = std::max(1u, lw / 2);
                lh = std::max(1u, lh / 2);
                d.levels.push_back(std::move(nx));
            }
        }
        d.us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
                   .count();
        if (upLog)
            std::fprintf(stderr, "[texup] %s %ux%u -> x%u %s %.2f ms\n", texup::hashName(key.hash).c_str(), W, H,
                         d.factor,
                         generated ? (overlayPack.empty() ? "runtime-atlas" : "runtime-overlay+pack")
                                   : (replace ? "replace" : (used == texup::Algo::XBrz ? "xbrz" : "smooth")),
                         d.us / 1000.0);
        pushDone(std::move(d));
    };
    // Authored typography is a selected UI resource, not background image
    // enhancement. Upload it before this draw so loading never flashes the
    // native font while the worker queue catches up (including upscale=1).
    //
    // The GUI page on screen (inventory, menus, help boxes; the world behind them is not part of it) and 1:1 UI
    // sprites must not wait behind the world's background upscales: the first inventory open queued its HD images
    // behind every world texture and showed the native art for a while. Every image of the page (pack image or live
    // upscale) is built right here, so the page is HD from its first frame; the first open of a page pays that once
    // (later opens hit the cache). 1:1 UI sprites outside a page take the pool's urgent lane.
    const bool guiPage = !engineWorldOpen && sceneCamera.guiScreen >= 0;
    const bool runHere = generated || guiPage;
    if (runHere)
        job();
    else
        submitJob(std::move(job), ui);
    if (upSync || runHere) {
        pumpUp();
        adoptUp(e);
    }
}

// Palette-only animated texture (fixed indices, cycling CLUT): draw this palette step from the HD index map of its
// index plane. Returns false when the native texture has to be used (no factor / pack image, map not built yet).
bool D3D11Backend::Impl::setupPaletteHd(TexEntry& e, const std::vector<uint8_t>& indices, uint64_t indexHash,
                                        const std::vector<uint32_t>& base, uint32_t W, uint32_t H, uint32_t mxl,
                                        const GsDrawState& s, bool ui) {
    const uint64_t hash = texup::hashContent(base.data(), W, H);
    const auto policy = texup::contentPolicy(isTextureHashExcluded(hash), excluded.count(hash) != 0);
    if (!policy.automaticScale || indices.size() != base.size())
        return false;
    // An HD pack image of this palette step guides the map (exact hash first, then the RGB alias like setupUpscale).
    std::string packPath;
    if (texReplace && policy.externalFiles && !replIndex.empty()) {
        auto rit = replIndex.find(hash);
        if (rit == replIndex.end()) {
            const uint64_t rgbHash = texup::hashContentRgb(base.data(), W, H);
            const auto ai = aliasIndex.find(rgbHash);
            rit = replIndex.find(ai != aliasIndex.end() ? ai->second : rgbHash);
        }
        if (rit != replIndex.end())
            packPath = rit->second;
    }
    // Recolouring runs on the render thread for every new palette step: cap the map at 4x (512 px for 128 px).
    uint32_t factor = std::min(pickFactor(W, H, ui), 4u);
    if (!packPath.empty())
        factor = std::max(factor, std::min(4u, 4096u / std::max(W, H)));
    if (factor <= 1)
        return false;

    std::shared_ptr<PalettePlane>& plane = palettePlanes[indexHash];
    if (!plane || (!plane->fromPack && !packPath.empty() && plane->ready.load(std::memory_order_acquire))) {
        plane = std::make_shared<PalettePlane>();
        plane->fromPack = !packPath.empty();
        plane->factor = factor;
        plane->lastFrame = frame;
        st.texPalettePlanes++;
        const bool wrapS = bitsOf(s.clamp, 0, 2) == 0, wrapT = bitsOf(s.clamp, 2, 2) == 0;
        texup::Algo algo = ui ? texup::Algo::XBrz : upAlgoCfg;
        submitJob([plane, indices, base, W, H, factor, packPath, wrapS, wrapT, algo, log = upLog] {
            std::vector<uint32_t> hd;
            if (!packPath.empty()) {
                std::vector<uint32_t> img;
                uint32_t rw = 0, rh = 0;
                if (texup::readImageFile(packPath, img, rw, rh) && rw && rh)
                    resampleBilinear(img, rw, rh, W * factor, H * factor, hd);
            }
            if (hd.empty()) {
                texup::Algo used = algo;
                if (used == texup::Algo::Auto)
                    used = texup::classify(texup::analyse(base.data(), W, H));
                texup::upscale(base.data(), W, H, factor, used, wrapS, wrapT, hd);
            }
            palhd::buildIndexMap(indices, base, W, H, hd, factor, wrapS, wrapT, plane->map);
            if (log)
                std::fprintf(stderr, "[texup] palette index map %ux%u x%u (%s)\n", W, H, factor,
                             packPath.empty() ? "live upscale" : packPath.c_str());
            plane->ready.store(true, std::memory_order_release);
        });
    }
    // Palette-swapped variants (NPC skins, terrain splats sharing one index plane) often have their own pack image:
    // that file is drawn as is by setupUpscale (exact replacement); it only guides the map for the other steps.
    if (replIndex.count(hash) && texReplace && policy.externalFiles)
        return false;
    if (!plane->ready.load(std::memory_order_acquire)) {
        e.paletteWait = indexHash;
        return false;
    }
    plane->lastFrame = frame;

    UpKey key{};
    key.hash = hash ^ (plane->fromPack ? 0x9E3779B97F4A7C15ull : 0);
    key.factor = plane->factor;
    key.packed = (mxl << 12) | (3u << 16); // kind 3: palette step drawn from an HD index map
    e.upKey = key;
    e.upWanted = true;
    adoptUp(e);
    if (!e.upWanted) // this palette step is cached already (cycles repeat)
        return true;

    uint32_t lut[256];
    palhd::paletteFromDecode(indices, base, lut);
    UpDone d;
    d.key = key;
    d.gen = upGen;
    d.factor = plane->factor;
    d.w = W * plane->factor;
    d.h = H * plane->factor;
    d.replaced = plane->fromPack;
    d.levels.emplace_back();
    palhd::applyPalette(plane->map, lut, d.levels.back());
    if (const char* dump = cstr(runtimeFlags().paletteHdDump); dump && st.texPaletteHd < 32) {
        // debug: the native palette step next to its HD version
        const std::string name = std::string(dump) + "/" + texup::hashName(hash);
        texup::writePngRGBA(name + "_native.png", base.data(), W, H);
        texup::writePngRGBA(name + "_hd.png", d.levels.back().data(), d.w, d.h);
    }
    uint32_t lw = d.w, lh = d.h;
    for (uint32_t l = 1; l <= mxl; ++l) {
        std::vector<uint32_t> next;
        texup::downsample2x(d.levels.back().data(), lw, lh, next);
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
        d.levels.push_back(std::move(next));
    }
    upInflight.insert(key);
    st.texUpPending++;
    pushDone(std::move(d));
    pumpUp();
    adoptUp(e);
    st.texPaletteHd++;
    return true;
}

void D3D11Backend::Impl::decodeLevel(uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h, uint64_t texa,
                                     const ClutBuf& clut, uint32_t csa, std::vector<uint32_t>& out) {
    decodeTextureLevel(mem, psm, tbp, tbw, w, h, texa, clut, csa, out);
}

uint64_t D3D11Backend::Impl::hashClut(const ClutBuf& c, uint32_t psm, uint32_t csa) {
    const uint32_t n = (psm == PSM_T8 || psm == PSM_T8H) ? 256u : 16u;
    const uint32_t base = (n == 16) ? csa * 16u : 0u;
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; ++i) {
        h ^= c.raw[(base + i) & 511];
        h *= 1099511628211ull;
    }
    h ^= c.is16 ? 0x1111 : 0;
    h ^= (uint64_t)c.cpsm << 40;
    return h;
}

uint64_t D3D11Backend::Impl::rawHash(const std::vector<std::pair<uint32_t, uint32_t>>& spans) const {
    uint64_t h = spans.size();
    for (const auto& sp : spans)
        h = h * 0x9e3779b97f4a7c15ull ^ mem.hashBlocks(sp.first, sp.second);
    return h;
}

bool D3D11Backend::Impl::resolveTexture(const GsDrawState& s, const ClutBuf& clut, TexBind& out, bool uiHint) {
    CpuProfile::Scope profile(cpuProfile, CpuProfile::ResolveTexture);
    const uint64_t t0 = s.tex0;
    const uint32_t tbp = (uint32_t)bitsOf(t0, 0, 14), tbw = (uint32_t)bitsOf(t0, 14, 6),
                   psm = (uint32_t)bitsOf(t0, 20, 6);
    const uint32_t tw = (uint32_t)std::min<uint64_t>(bitsOf(t0, 26, 4), 10),
                   th = (uint32_t)std::min<uint64_t>(bitsOf(t0, 30, 4), 10);
    const uint32_t csa = (uint32_t)bitsOf(t0, 56, 5);
    const uint32_t W = 1u << tw, H = 1u << th;
    if (!isValidPsm(psm) || isZ(psm))
        return false;
    const uint32_t mxl = (uint32_t)bitsOf(s.tex1, 2, 3);

    // framebuffer-as-texture
    float ox, oy;
    if (RT* rt = findRTForTexture(tbp, tbw, psm, W, H, ox, oy)) {
        syncRT(*rt);
        if (texLog)
            std::fprintf(
                stderr,
                "[gsr-tex] sample render target fbp=%u fbw=%u cls=%u as tbp=%u psm=%02x %ux%u tcc=%u texa=%llx\n",
                rt->fbp, rt->fbw, rt->cls, tbp, psm, W, H, (unsigned)bitsOf(t0, 34, 1), (unsigned long long)s.texa);
        out.isRT = true;
        out.rt = rt;
        out.ox = ox;
        out.oy = oy;
        out.mxl = 0;
        st.rtSamples++;
        return true;
    }

    TexKey key{};
    key.tbp = tbp;
    key.tbw = tbw;
    key.psm = psm;
    key.tw = tw;
    key.th = th;
    key.mxl = mxl;
    const bool indexed = isIndexed(psm);
    key.clutKind = indexed ? (1u | (csa << 4)) : 0u;
    key.clutHash = indexed ? hashClut(clut, psm, csa) : 0;
    key.texa = (psm == PSM_CT32 || indexed) && !indexed ? 0 : s.texa;
    if (psm == PSM_CT32)
        key.texa = 0;
    key.mip1 = mxl ? s.miptbp1 : 0;
    key.mip2 = mxl ? s.miptbp2 : 0;

    auto it = texCache.find(key);
    if (it != texCache.end()) {
        TexEntry& e = it->second;
        bool valid = true;
        if (W == 128 && H == 128 && e.previewVersion != savePreview(e.previewHash).version)
            valid = false;
        if (e.paletteWait) {
            const auto pit = palettePlanes.find(e.paletteWait);
            if (pit == palettePlanes.end() || pit->second->ready.load(std::memory_order_acquire))
                valid = false;
        }
        // The guest may finish loading a thumbnail during its first draw;
        // the EE identifies it at the next input boundary. Rebuild an
        // already-cached pixel-art upscale once after that registration.
        if (W == 128 && H == 128 && e.upWanted && !(e.upKey.packed & (3u << 16)) &&
            (e.upKey.packed & 255u) != (uint32_t)texup::Algo::Smooth && isPhotographicTexture(e.upKey.hash))
            valid = false;
        // GPU draws mark RT rows dirty without advancing the RAM clock.
        // Flush overlapping rows before the clock shortcut, or a decoded
        // fallback (e.g. a shifted-row alias) can reuse pre-draw pixels.
        {
            CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureCoherenceFlush);
            RT* aliased = aliasedTarget(psm, mxl, e.spans);
            if (aliased) {
                // Built from the target's GPU copy: current until the target changes again.
                if (e.aliasTarget != aliased || e.aliasWrites != aliased->writes)
                    valid = false;
            } else {
                if (e.aliasTarget)
                    valid = false; // the target was read back: memory is authoritative again
                for (auto& sp : e.spans)
                    flushToMemory(sp.first, sp.second);
            }
        }
        if (valid && e.checkedClock != mem.clock()) {
            CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureStampCheck);
            bool written = false;
            for (auto& sp : e.spans)
                if (mem.maxStamp(sp.first, sp.second - sp.first + 1) > e.stamp) {
                    written = true;
                    break;
                }
            // The game re-uploads most textures every frame. Identical bytes keep the decoded, created and
            // upscaled texture; only a real content change decodes again.
            if (written && rawHash(e.spans) != e.rawHash)
                valid = false;
            else {
                if (written) {
                    e.stamp = mem.clock();
                    st.textureRevalidated++;
                }
                e.checkedClock = mem.clock();
            }
        }
        if (valid) {
            if (cfg.onTexture && !e.cpu.empty()) {
                TextureEvent ev;
                ev.tex0 = s.tex0;
                ev.tex1 = s.tex1;
                ev.texa = s.texa;
                ev.clamp = s.clamp;
                ev.miptbp1 = s.miptbp1;
                ev.miptbp2 = s.miptbp2;
                ev.width = e.w;
                ev.height = e.h;
                ev.mipLevels = mxl + 1;
                ev.cached = true;
                for (uint32_t l = 0; l <= mxl && l < 7 && l < e.cpu.size(); ++l) {
                    ev.level[l] = e.cpu[l].data();
                    ev.levelW[l] = std::max(1u, e.w >> l);
                    ev.levelH[l] = std::max(1u, e.h >> l);
                }
                ev.clutRaw = clut.raw;
                ev.clutCpsm = clut.cpsm;
                ev.frame = frame;
                {
                    CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureCallback);
                    cfg.onTexture(ev);
                }
            }
            e.lastFrame = frame;
            if (e.upWanted) {
                pumpUp();
                adoptUp(e);
            }
            if (e.up) {
                e.up->lastFrame = frame;
                out.srv = e.up->srv.Get();
                out.factor = e.up->factor;
            } else
                out.srv = e.srv.Get();
            out.mxl = mxl;
            out.fontAtlas = e.fontAtlas;
            out.menuArtwork = e.menuArtwork;
            std::copy(e.averageColor, e.averageColor + 3, out.averageColor);
            out.minimumAlpha = e.minimumAlpha;
            out.reflectionTexture = e.reflectionTexture;
            st.textureCacheHits++;
            return true;
        }
        if (texLog) {
            uint32_t wb = 0;
            uint64_t ws = 0;
            for (auto& sp : e.spans)
                for (uint32_t b = sp.first; b <= sp.second; ++b)
                    if (mem.blockStamp(b) > e.stamp && mem.blockStamp(b) > ws) {
                        ws = mem.blockStamp(b);
                        wb = b;
                    }
            std::fprintf(stderr,
                         "[gsr-tex] invalidate tbp=%u psm=%u %ux%u spans=%u..%u entryStamp=%llu newest block %u "
                         "stamp %llu (clock %llu)\n",
                         key.tbp, key.psm, 1u << key.tw, 1u << key.th, e.spans[0].first, e.spans[0].second,
                         (unsigned long long)e.stamp, wb, (unsigned long long)ws, (unsigned long long)mem.clock());
        }
        texBytes -= e.bytes;
        texCache.erase(it);
    }

    // decode
    RT* aliased = nullptr;
    if (mxl == 0) {
        uint32_t lo, hi;
        spanBlocks(psm, tbp, std::max(tbw, 1u), 0, 0, W, H, lo, hi);
        aliased = aliasedTarget(psm, mxl, {{lo, hi}});
    }
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    std::vector<std::vector<uint32_t>> levels;
    std::vector<uint32_t> lw, lh;
    for (uint32_t l = 0; l <= mxl; ++l) {
        const uint32_t w = std::max(1u, W >> l), h = std::max(1u, H >> l);
        uint32_t lbp = tbp, lbw = tbw;
        if (l > 0) {
            const uint64_t m = (l <= 3) ? s.miptbp1 : s.miptbp2;
            const uint32_t k = (l - 1) % 3;
            lbp = (uint32_t)bitsOf(m, 20 * k, 14);
            lbw = (uint32_t)bitsOf(m, 20 * k + 14, 6);
        }
        uint32_t lo, hi;
        spanBlocks(psm, lbp, std::max(lbw, 1u), 0, 0, w, h, lo, hi);
        spans.push_back({lo, hi});
        if (!aliased) {
            CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureCoherenceFlush);
            flushToMemory(lo, hi);
        }
        levels.emplace_back();
        {
            CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureDecode);
            decodeLevel(psm, lbp, lbw, w, h, s.texa, clut, csa, levels.back());
        }
        lw.push_back(w);
        lh.push_back(h);
    }
    // --- palette / pixel animated textures: a texture-memory slot whose decoded content keeps changing
    // ("volatile": the highlighted menu entry, palette cycled water / fire). Content hashing only starts once a
    // slot was re-decoded; slots the game merely re-uploads unchanged every frame (the title background tiles)
    // never become volatile.
    bool volatileSlot = false, paletteOnly = false;
    std::vector<uint8_t> paletteIndices;
    uint64_t paletteIndexHash = 0;
    uint64_t decodedHash = 0;
    bool decodedHashValid = false;
    {
        const uint64_t slotKey = ((uint64_t)tbp << 40) ^ ((uint64_t)tbw << 32) ^ ((uint64_t)psm << 24) ^
                                 ((uint64_t)tw << 12) ^ ((uint64_t)th << 4) ^ ((uint64_t)(indexed ? 1 : 0));
        SlotInfo& sl = slots[slotKey];
        if (sl.isVolatile && frame - sl.lastDecode > 600) {
            sl.isVolatile = false;
            sl.count = sl.changes = sl.sameRun = 0;
            sl.hashValid = false;
        }
        if (frame - sl.windowStart > 90) {
            sl.windowStart = frame;
            sl.count = sl.changes = sl.sameRun = 0;
            sl.hashValid = false;
        }
        ++sl.count;
        sl.lastDecode = frame;
        if (!sl.isVolatile &&
            sl.sameRun < 6) // after 6 identical re-decodes the slot is a plain re-upload: stop hashing for this window
        {
            const uint64_t h = texup::hashContent(levels[0].data(), W, H);
            decodedHash = h;
            decodedHashValid = true;
            if (sl.hashValid && h != sl.lastHash) {
                sl.changes++;
                sl.sameRun = 0;
            } else if (sl.hashValid)
                sl.sameRun++;
            sl.lastHash = h;
            sl.hashValid = true;
        }
        if (!sl.isVolatile && sl.changes >= 3) {
            sl.isVolatile = true;
            st.texVolatile++;
            if (texLog || texDump)
                std::fprintf(stderr,
                             "[gsr-tex] volatile (content changed %u times in %llu frames): tbp=%u psm=%02x %ux%u%s\n",
                             sl.changes, (unsigned long long)(frame - sl.windowStart), tbp, psm, W, H,
                             indexed ? " indexed" : "");
            if (texDump && !dumpDir.empty()) {
                if (std::FILE* vf = std::fopen((dumpDir + "/volatile.txt").c_str(), "ab")) {
                    std::fprintf(vf, "tbp=%u psm=%02x %ux%u %s\n", tbp, psm, W, H,
                                 indexed ? "palette-animated (not dumped, never replaced)"
                                         : "pixel-animated (not dumped, never replaced)");
                    std::fclose(vf);
                }
            }
        }
        volatileSlot = sl.isVolatile;
        // Palette-only animation: the indices did not change since the last decode of this slot, only the CLUT.
        if (volatileSlot && indexed && texPaletteHd) {
            palhd::decodeIndexPlane(mem, psm, tbp, tbw, W, H, paletteIndices);
            const uint64_t ih = palhd::hashIndexPlane(paletteIndices, W, H, psm);
            paletteOnly = sl.indexValid && sl.lastIndexHash == ih;
            paletteIndexHash = ih;
            sl.lastIndexHash = ih;
            sl.indexValid = true;
        }
    }
    // --- smooth_menu_highlight: the highlighted menu entry is a 128x128 fire cellular automaton (T8 heat map +
    // CT16 glow, both rewritten every frame) drawn as an additive sprite (ALPHA = Cs*As + Cd) with nearest
    // sampling. A TV blurred the 1-texel checkerboard it produces; on a PC it is noise.
    if (const char* hd = cstr(runtimeFlags().hlDump)) {
        static int cand = 0;
        if (cand < 40 && W == 128 && H == 128 && mxl == 0 && (psm == PSM_T8 || psm == PSM_CT16) &&
            (s.flags & DF_SPRITE) && (s.flags & DF_ABE) && (s.alpha & 0xFF) == 0x48) {
            char nm[512];
            std::snprintf(nm, sizeof nm, "%s/cand_%d_tbp%u_psm%02x%s.png", hd, cand++, tbp, psm,
                          volatileSlot ? "_vol" : "");
            texup::writePngRGBA(nm, levels[0].data(), W, H);
        }
    }
    const bool hlShape = W == 128 && H == 128 && mxl == 0 && (psm == PSM_T8 || psm == PSM_CT16) &&
                         (s.flags & DF_SPRITE) && (s.flags & DF_ABE) && (s.alpha & 0xFF) == 0x48;
    // the fire heat map (T8, volatile) starts a highlight frame; the stable CT16 glyph-outline overlay drawn with
    // the same state in that frame belongs to it
    const bool hlHit =
        smoothHL && hlShape && ((volatileSlot && psm == PSM_T8) || (psm == PSM_CT16 && frame - hlFrame <= 1));
    if (smoothHL && hlShape && volatileSlot && psm == PSM_T8)
        hlFrame = frame;
    if (hlHit) {
        static int hlDumped = 0;
        const char* hd = cstr(runtimeFlags().hlDump);
        if (hd && hlDumped < 6) {
            char nm[512];
            std::snprintf(nm, sizeof nm, "%s/hl_%d_%02x_before.png", hd, hlDumped, psm);
            texup::writePngRGBA(nm, levels[0].data(), W, H);
        }
        smoothBinomial3(levels[0], W, H);
        decodedHashValid = false; // Replacement identity follows the smoothed pixels.
        if (hd && hlDumped < 6) {
            char nm[512];
            std::snprintf(nm, sizeof nm, "%s/hl_%d_%02x_after.png", hd, hlDumped, psm);
            texup::writePngRGBA(nm, levels[0].data(), W, H);
            hlDumped++;
        }
        st.hlSmoothed++;
    }
    if (texLog)
        std::fprintf(stderr, "[gsr-tex] decode f%llu tbp=%u tbw=%u psm=%02x %ux%u csa=%u clut=%016llx%s\n",
                     (unsigned long long)frame, tbp, tbw, psm, W, H, csa, (unsigned long long)key.clutHash,
                     volatileSlot ? " VOLATILE" : "");
    D3D11_TEXTURE2D_DESC d{};
    d.Width = W;
    d.Height = H;
    d.MipLevels = mxl + 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = aliased ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE; // aliased: target blocks are copied in
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(mxl + 1);
    uint64_t bytes = 0;
    for (uint32_t l = 0; l <= mxl; ++l) {
        init[l].pSysMem = levels[l].data();
        init[l].SysMemPitch = lw[l] * 4;
        bytes += (uint64_t)lw[l] * lh[l] * 4;
    }
    if (cfg.onTexture) {
        TextureEvent ev;
        ev.tex0 = s.tex0;
        ev.tex1 = s.tex1;
        ev.texa = s.texa;
        ev.clamp = s.clamp;
        ev.miptbp1 = s.miptbp1;
        ev.miptbp2 = s.miptbp2;
        ev.width = W;
        ev.height = H;
        ev.mipLevels = mxl + 1;
        for (uint32_t l = 0; l <= mxl && l < 7; ++l) {
            ev.level[l] = levels[l].data();
            ev.levelW[l] = lw[l];
            ev.levelH[l] = lh[l];
        }
        ev.clutRaw = clut.raw;
        ev.clutCpsm = clut.cpsm;
        ev.frame = frame;
        {
            CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureCallback);
            cfg.onTexture(ev);
        }
    }
    TexEntry e;
    e.w = W;
    e.h = H;
    if ((modernWater || rayShadows) && !levels.empty() && !levels[0].empty()) {
        if (engineWorldOpen && !(s.zbuf & (1ull << 32))) {
            // Owned material texels survive the current view and GS slot reuse.
            // Never use a per-triangle average as reflected surface detail.
            uint64_t hash = 14695981039346656037ull;
            surface::Texture texture;
            texture.pixels.resize(surface::Texture::size * surface::Texture::size);
            for (unsigned y = 0; y < surface::Texture::size; ++y)
                for (unsigned x = 0; x < surface::Texture::size; ++x) {
                    auto pixel = levels[0][size_t(y * H / surface::Texture::size) * W + x * W / surface::Texture::size];
                    texture.pixels[y * surface::Texture::size + x] = pixel;
                    hash ^= pixel;
                    hash *= 1099511628211ull;
                }
            auto existing = reflectionTextureHashes.find(hash);
            if (existing != reflectionTextureHashes.end())
                e.reflectionTexture = existing->second;
            else if (reflectionTextures.size() < 256) {
                e.reflectionTexture = uint32_t(reflectionTextures.size());
                texture.identity = hash;
                reflectionTextures.push_back(std::move(texture));
                reflectionTextureHashes[hash] = e.reflectionTexture;
            }
        }
        e.minimumAlpha = 255;
        for (auto pixel : levels[0])
            e.minimumAlpha = std::min(e.minimumAlpha, uint8_t(pixel >> 24));
        double sum[3]{};
        uint64_t samples = 0;
        const size_t step = std::max<size_t>(1, levels[0].size() / 256);
        for (size_t i = 0; i < levels[0].size(); i += step) {
            const auto color = levels[0][i];
            if ((color >> 24) == 0)
                continue;
            for (unsigned c = 0; c < 3; ++c)
                sum[c] += float((color >> (c * 8)) & 255) / 255.f;
            ++samples;
        }
        if (samples)
            for (unsigned c = 0; c < 3; ++c)
                e.averageColor[c] = float(sum[c] / samples);
    }
    std::copy(e.averageColor, e.averageColor + 3, out.averageColor);
    out.minimumAlpha = e.minimumAlpha;
    out.reflectionTexture = e.reflectionTexture;
    if (cfg.onTexture)
        e.cpu = levels;
    {
        CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureCreate);
        if (FAILED(dev->CreateTexture2D(&d, init.data(), &e.tex)))
            return false;
        dev->CreateShaderResourceView(e.tex.Get(), nullptr, &e.srv);
    }
    if (aliased) {
        copyAliasedBlocks(*aliased, e.tex.Get(), tbp, tbw, W, H);
        e.aliasTarget = aliased;
        e.aliasWrites = aliased->writes;
    }
    e.stamp = mem.clock();
    e.checkedClock = mem.clock();
    e.lastFrame = frame;
    e.bytes = bytes;
    e.rawHash = rawHash(spans);
    e.spans = std::move(spans);
    { // Explicit runtime atlases also apply when automatic upscaling is off.
        CpuProfile::Scope detail(cpuProfile, CpuProfile::TextureUpscale);
        // The game streams some UI atlases through a scratch target's memory every frame (the inventory icon atlas
        // at tbp 0x3e7f). The upload reaches memory too, so an exact authored hash of the decoded blocks (a
        // runtime atlas or a pack image) identifies the texture; render output never matches one.
        bool authored = false;
        if (aliased) {
            if (!decodedHashValid) {
                decodedHash = texup::hashContent(levels[0].data(), W, H);
                decodedHashValid = true;
            }
            authored =
                runtimeTexture(decodedHash) || (texReplace && replIndex.count(decodedHash) &&
                                                !excluded.count(decodedHash) && !isTextureHashExcluded(decodedHash));
        }
        if ((!aliased || authored) &&
            !(paletteOnly && !hlHit &&
              setupPaletteHd(e, paletteIndices, paletteIndexHash, levels[0], W, H, mxl, s, uiHint)))
            setupUpscale(e, levels[0], W, H, mxl, s, uiHint, volatileSlot, decodedHashValid ? &decodedHash : nullptr);
    }
    out.srv = e.srv.Get();
    out.mxl = mxl;
    out.fontAtlas = e.fontAtlas;
    out.menuArtwork = e.menuArtwork;
    if (e.up) {
        e.up->lastFrame = frame;
        out.srv = e.up->srv.Get();
        out.factor = e.up->factor;
    }
    texBytes += bytes;
    texCache[key] = std::move(e);
    st.textureDecodes++;
    return true;
}

void D3D11Backend::Impl::flushTexCaches() {
    texCache.clear();
    texBytes = 0;
    upCache.clear();
    palettePlanes.clear();
    upBytes = 0;
    upInflight.clear();
    st.texUpPending = 0;
    ++upGen; // results of jobs still running on the pool are dropped when they arrive
}

} // namespace gsr
