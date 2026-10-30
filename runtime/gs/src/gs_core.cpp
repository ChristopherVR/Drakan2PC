// GS front end: GIF tag parsing, register file, vertex queue / primitive assembly, local memory transfers,
// CLUT buffer, privileged registers. Draw calls are batched and handed to the D3D11 backend.
#include "gs_internal.h"
#include "runtime_flags.h"
#include "gs_texup.h"
#include "gs_image_upload.h"

#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <deque>

namespace gsr {

namespace {
std::mutex previewMutex;
std::unordered_map<uint64_t, SavePreview> previewFiles;
std::deque<uint64_t> previewOrder;
} // namespace
void registerSavePreview(uint64_t hash, const std::string& path, bool flipY) {
    if (!hash || path.empty())
        return;
    uint64_t version = 1469598103934665603ull;
    for (unsigned char c : path)
        version = (version ^ c) * 1099511628211ull;
    version ^= static_cast<uint64_t>(flipY);
    std::lock_guard<std::mutex> lock(previewMutex);
    previewOrder.erase(std::remove(previewOrder.begin(), previewOrder.end(), hash), previewOrder.end());
    if (previewFiles.size() >= 256 && !previewFiles.count(hash)) {
        previewFiles.erase(previewOrder.front());
        previewOrder.pop_front();
    }
    previewOrder.push_back(hash);
    previewFiles[hash] = {path, flipY, version};
}
SavePreview savePreview(uint64_t hash) {
    std::lock_guard<std::mutex> lock(previewMutex);
    const auto it = previewFiles.find(hash);
    return it == previewFiles.end() ? SavePreview{} : it->second;
}

namespace {
enum GsReg : uint32_t {
    R_PRIM = 0x00,
    R_RGBAQ = 0x01,
    R_ST = 0x02,
    R_UV = 0x03,
    R_XYZF2 = 0x04,
    R_XYZ2 = 0x05,
    R_TEX0_1 = 0x06,
    R_TEX0_2 = 0x07,
    R_CLAMP_1 = 0x08,
    R_CLAMP_2 = 0x09,
    R_FOG = 0x0A,
    R_XYZF3 = 0x0C,
    R_XYZ3 = 0x0D,
    R_AD = 0x0E,
    R_NOP = 0x0F,
    R_TEX1_1 = 0x14,
    R_TEX1_2 = 0x15,
    R_TEX2_1 = 0x16,
    R_TEX2_2 = 0x17,
    R_XYOFFSET_1 = 0x18,
    R_XYOFFSET_2 = 0x19,
    R_PRMODECONT = 0x1A,
    R_PRMODE = 0x1B,
    R_TEXCLUT = 0x1C,
    R_SCANMSK = 0x22,
    R_MIPTBP1_1 = 0x34,
    R_MIPTBP1_2 = 0x35,
    R_MIPTBP2_1 = 0x36,
    R_MIPTBP2_2 = 0x37,
    R_TEXA = 0x3B,
    R_FOGCOL = 0x3D,
    R_TEXFLUSH = 0x3F,
    R_SCISSOR_1 = 0x40,
    R_SCISSOR_2 = 0x41,
    R_ALPHA_1 = 0x42,
    R_ALPHA_2 = 0x43,
    R_DIMX = 0x44,
    R_DTHE = 0x45,
    R_COLCLAMP = 0x46,
    R_TEST_1 = 0x47,
    R_TEST_2 = 0x48,
    R_PABE = 0x49,
    R_FBA_1 = 0x4A,
    R_FBA_2 = 0x4B,
    R_FRAME_1 = 0x4C,
    R_FRAME_2 = 0x4D,
    R_ZBUF_1 = 0x4E,
    R_ZBUF_2 = 0x4F,
    R_BITBLTBUF = 0x50,
    R_TRXPOS = 0x51,
    R_TRXREG = 0x52,
    R_TRXDIR = 0x53,
    R_HWREG = 0x54,
    R_SIGNAL = 0x60,
    R_FINISH = 0x61,
    R_LABEL = 0x62,
};

struct Vtx {
    float x = 0, y = 0;
    uint32_t z = 0;
    uint8_t r = 0, g = 0, b = 0, a = 0, fog = 0;
    float s = 0, t = 0, q = 1;
    uint16_t u = 0, v = 0;
};

struct Ctx {
    uint64_t tex0 = 0, tex1 = 0, tex2 = 0, clamp = 0, xyoffset = 0, miptbp1 = 0, miptbp2 = 0, scissor = 0, alpha = 0,
             test = 0, fba = 0, frame = 0, zbuf = 0;
};

struct PathState {
    uint32_t qwLeft = 0; // qwords remaining in the current tag's data
    uint32_t flg = 0, nreg = 0;
    uint64_t regs = 0;
    uint32_t regIdx = 0;
    uint32_t valuesLeft = 0; // REGLIST: 64-bit values left
};

constexpr uint32_t kMaxBatchVerts = 3 * 60000;

// Draw batch with fixed capacity: beginDraw() flushes when fewer than 12 slots (the largest primitive) are left, so
// the emitters append without a capacity check.
class VertexBatch {
public:
    bool empty() const { return m_size == 0; }
    size_t size() const { return m_size; }
    const DrawVertex* data() const { return m_data.get(); }
    void clear() { m_size = 0; }
    void push_back(const DrawVertex& v) { m_data[m_size++] = v; }

private:
    std::unique_ptr<DrawVertex[]> m_data{new DrawVertex[kMaxBatchVerts]};
    size_t m_size = 0;
};

float asFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
} // namespace

struct Renderer::Impl {
    std::recursive_mutex mtx; // serialises the public API (host present thread vs. the submitting thread)
    // live settings: written by any thread under setMu, consumed at frame boundaries under mtx
    std::mutex setMu;
    LiveSettings pend;
    std::atomic<bool> hasPend{false};
    std::atomic<bool> waterRequested{false};
    std::atomic<bool> modelNeuralRequested{false}, modelSrRequested{false};
    bool vsyncReq = true;
    void applyPending() // mtx held
    {
        if (!hasPend.load(std::memory_order_acquire))
            return;
        LiveSettings ls;
        {
            std::lock_guard<std::mutex> lk(setMu);
            ls = pend;
            pend = LiveSettings{};
            hasPend.store(false, std::memory_order_release);
        }
        if (!ls.mask)
            return;
        if (ls.mask & LiveSettings::F_DEINT)
            cfg.deinterlace = std::min(ls.deinterlace, 2u);
        if (ls.mask & LiveSettings::F_FLICKER)
            cfg.flickerFilter = ls.flicker;
        if (ls.mask & (LiveSettings::F_SCALE | LiveSettings::F_UPSCALE | LiveSettings::F_NEURAL |
                       LiveSettings::F_WATER | LiveSettings::F_NEURAL_RES))
            flushBatch();
        backend->applyLive(ls);
        stats.liveApplied++;
    }
    template <class F>
    void queue(F&& f) {
        std::lock_guard<std::mutex> lk(setMu);
        f(pend);
        hasPend.store(true, std::memory_order_release);
    }
    uint32_t field = 0; // current interlace field (toggled each vsync)
    Config cfg;
    Stats stats;
    LocalMemory mem;
    std::unique_ptr<Backend> backend;

    // register file
    Ctx ctx[2];
    uint64_t prim = 0, prmode = 0, prmodecont = 1;
    uint8_t cr = 0x80, cg = 0x80, cb = 0x80, ca = 0x80;
    float cq = 1.0f, cs = 0, ct = 0;
    uint16_t cu = 0, cv = 0;
    uint8_t cfog = 0;
    uint64_t texa = 0, fogcol = 0, dimx = 0, dthe = 0, colclamp = 0, pabe = 0, scanmsk = 0, texclut = 0;
    uint64_t bitbltbuf = 0, trxpos = 0, trxreg = 0, trxdir = 3;
    uint64_t vsyncCount = 0;
    bool xferLog = runtimeFlags().xferLog; // debug: log every transfer start to stderr
    uint64_t siglblid = 0;

    // vertex queue
    Vtx q[4];
    int qn = 0;

    // GIF
    PathState paths[3];

    // batch
    VertexBatch batch;
    GsDrawState batchState{};
    bool batchActive = false;

    // CLUT
    ClutBuf clut;
    uint32_t cbp0 = 0, cbp1 = 0;

    // transfer
    struct Xfer {
        bool active = false;
        uint32_t dbp = 0, dbw = 0, dpsm = 0;
        uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
        uint32_t cx = 0, cy = 0; // current pixel
        uint64_t pixelsLeft = 0;
        uint8_t carry[4] = {};
        int carryLen = 0;
        bool nibbleHalf = false;
        bool intoTarget = false;  // applied to a GPU-owned render target (Backend::beginTargetUpload)
        bool movieBlock = false;  // 16x16 CT32 upload while an HD movie is registered (movieBlockUploaded)
        std::vector<uint8_t> raw; // only when cfg.onUpload is set or for a movie block
    } xfer;
    std::vector<uint8_t> l2h; // pending local->host bytes
    size_t l2hPos = 0;

    // privileged
    uint64_t pmode = 0, smode1 = 0, smode2 = 0, srfsh = 0, synch1 = 0, synch2 = 0, syncv = 0, dispfb1 = 0, display1 = 0,
             dispfb2 = 0, display2 = 0, extbuf = 0, extdata = 0, extwrite = 0, bgcolor = 0, imr = 0, busdir = 0;
    uint64_t csr = 0;

    Renderer* owner = nullptr;
    bool noted[8] = {};

    // Previous textured sprite (screen rect in GS pixels, no offset) for the sprite-seam fix, see emitSprite().
    struct LastSprite {
        bool valid = false;
        float lx = 0, rx = 0, ty = 0, by = 0;
        uint64_t frame = 0;
    } lastSpr;

    void note(int id, const char* msg) {
        if (cfg.logUnsupported && !noted[id & 7]) {
            noted[id & 7] = true;
            std::fprintf(stderr, "[gsr] note: %s\n", msg);
        }
    }

    // ------------------------------------------------------------------------------------------------
    uint64_t curAttr() const { return (prmodecont & 1) ? (prim >> 3) : prmode; }
    int curCtx() const { return (int)((curAttr() >> 6) & 1); }

    // ------------------------------------------------------------------------------------------------
    // HD movie frames (docs/reference/fmv.md section 9). The host registers each decoded frame's signature
    // (noteMovieFrame, any thread); every 16x16 CT32 upload updates the signature of the rectangle it lands in, per
    // frame buffer. A buffer whose signature equals a registered frame holds that frame completely: the HD image is
    // drawn over it before the next draw, read-back or display, so fades and overlays still land on top.
    struct MovieReg {
        uint64_t signature = 0;
        uint32_t w = 0, h = 0, movie = 0, frame = 0;
    };
    std::mutex movieMu; // registry + source (written by the host thread)
    std::vector<MovieReg> movieRegs;
    size_t movieRegNext = 0;
    std::atomic<uint64_t> movieRegCount{0};
    MovieFrameSource movieSource;
    uint64_t movieRegSeen = 0, movieActiveVsync = 0;
    uint32_t movieW = 0, movieH = 0; // size of the latest registered frame
    struct MovieTrack {
        std::vector<uint64_t> terms; // per 16x16 slot of the 2048x2048 address space (x/16 + y/16 * 128)
        uint64_t sum = 0;            // over the slots of the rectangle below
        uint32_t x0 = 0, y0 = 0, w = 0, h = 0;
        bool pending = false, drawn = false;
        uint32_t movie = 0, frame = 0;
    };
    std::unordered_map<uint64_t, MovieTrack> movieTracks; // key: dbp | dbw << 32

    bool movieWatching() {
        const uint64_t n = movieRegCount.load(std::memory_order_acquire);
        if (n != movieRegSeen) {
            std::lock_guard<std::mutex> lk(movieMu);
            movieRegSeen = n;
            movieActiveVsync = vsyncCount;
            const MovieReg& last = movieRegs[(movieRegNext + movieRegs.size() - 1) % movieRegs.size()];
            movieW = last.w;
            movieH = last.h;
        }
        const bool on = movieRegSeen != 0 && vsyncCount - movieActiveVsync < 600; // 10 s after the last frame
        if (!on && !movieTracks.empty())
            movieTracks.clear();
        return on;
    }

    void movieBlockUploaded(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, const uint8_t* px) {
        using namespace movie;
        const uint32_t width = dbw * 64u;
        if (!movieW || !movieH || movieW > width || movieH > 512u)
            return;
        MovieTrack& t = movieTracks[dbp | (uint64_t)dbw << 32];
        if (t.terms.empty())
            t.terms.assign(128u * 128u, 0);
        if (t.w != movieW || t.h != movieH) { // the game centres the movie on its 512 line screen (blit 0x177650)
            t.w = movieW;
            t.h = movieH;
            t.x0 = (width - movieW) / 2 & ~(kBlock - 1);
            t.y0 = (512u - movieH) / 2 & ~(kBlock - 1);
            t.sum = 0;
            for (uint32_t by = t.y0; by < t.y0 + t.h; by += kBlock)
                for (uint32_t bx = t.x0; bx < t.x0 + t.w; bx += kBlock)
                    t.sum += t.terms[(by / kBlock) * 128u + bx / kBlock];
        }
        const uint64_t term = blockTerm(blockHash(px));
        const uint32_t slot = ((y & 2047u) / kBlock) * 128u + (x & 2047u) / kBlock;
        const uint64_t old = t.terms[slot];
        t.terms[slot] = term;
        if (x < t.x0 || y < t.y0 || x >= t.x0 + t.w || y >= t.y0 + t.h)
            return;
        t.sum += term - old;
        t.drawn = false; // the native macroblock replaced HD pixels (if any) in the target
        t.pending = false;
        // Newest first: runs of identical frames (fades, lead-in) share a signature, and the newest is the one being
        // shown.
        std::lock_guard<std::mutex> lk(movieMu);
        for (size_t i = 1; i <= movieRegs.size(); ++i)
            if (const MovieReg& r = movieRegs[(movieRegNext + movieRegs.size() - i) % movieRegs.size()];
                r.signature == t.sum && r.w == t.w && r.h == t.h) {
                t.pending = true;
                t.movie = r.movie;
                t.frame = r.frame;
                break;
            }
    }

    void applyMovieFrames() {
        if (movieTracks.empty())
            return;
        for (auto& [key, t] : movieTracks) {
            if (!t.pending || t.drawn)
                continue;
            t.drawn = true;
            MovieFrameSource source;
            {
                std::lock_guard<std::mutex> lk(movieMu);
                source = movieSource;
            }
            const auto image = source ? source(t.movie, t.frame) : nullptr;
            if (image && !image->rgba.empty())
                backend->drawMovieFrame((uint32_t)key, (uint32_t)(key >> 32), t.x0, t.y0, t.w, t.h, *image,
                                        (uint64_t)t.movie << 32 | t.frame);
        }
    }

    void flushBatch() {
        if (!batchActive || batch.empty()) {
            batch.clear();
            batchActive = false;
            return;
        }
        applyMovieFrames();
        backend->draw(batchState, clut, batch.data(), (uint32_t)batch.size());
        batch.clear();
        batchActive = false;
    }

    void signalVblank() {
        csr |= (1ull << 3);
        ++vsyncCount;
        csr ^= (1ull << 13);
        field = (uint32_t)((csr >> 13) & 1);
    }

    void completeDisplayFrame(bool presentFrame) {
        applyMovieFrames();
        flushBatch();
        backend->latchDisplay(buildDisplay(), field);
        if (presentFrame)
            backend->presentLatched();
        backend->endFrame();
        ++stats.frames;
    }

    GsDrawState makeState(uint32_t primType) const {
        const uint64_t a = curAttr();
        const Ctx& c = ctx[(a >> 6) & 1];
        (void)primType;
        GsDrawState s{};
        s.frame = c.frame;
        s.xyoffset = c.xyoffset;
        s.zbuf = c.zbuf;
        s.scissor = c.scissor;
        s.test = c.test;
        s.alpha = c.alpha;
        s.tex0 = c.tex0;
        s.tex1 = c.tex1;
        s.clamp = c.clamp;
        s.miptbp1 = c.miptbp1;
        s.miptbp2 = c.miptbp2;
        s.texa = texa;
        s.fogcol = fogcol;
        s.dimx = dimx;
        s.fba = (uint32_t)(c.fba & 1);
        return s;
    }

    // attr layout (after >>3): bit0 IIP? -> PRIM: IIP[3] TME[4] FGE[5] ABE[6] AA1[7] FST[8] CTXT[9] FIX[10]
    // curAttr() returns prim>>3 (IIP at bit0) or PRMODE (same layout stored >>3). Build flags accordingly.
    GsDrawState buildState(uint32_t primType) const {
        GsDrawState s = makeState(primType);
        const uint64_t a = curAttr();
        uint32_t f = 0;
        if (a & (1 << 0))
            f |= DF_IIP;
        if (a & (1 << 1))
            f |= DF_TME;
        if (a & (1 << 2))
            f |= DF_FGE;
        if (a & (1 << 3))
            f |= DF_ABE;
        if (a & (1 << 4))
            f |= DF_AA1;
        if (a & (1 << 5))
            f |= DF_FST;
        if (colclamp & 1)
            f |= DF_COLCLAMP;
        if (dthe & 1)
            f |= DF_DTHE;
        if (pabe & 1)
            f |= DF_PABE;
        if (primType == 6)
            f |= DF_SPRITE;
        if (primType == 1 || primType == 2)
            f |= DF_LINE;
        if (primType == 0)
            f |= DF_POINT;
        s.flags = f;
        s.clutSerial = clut.serial;
        return s;
    }

    DrawVertex toDV(const Vtx& v, bool fst) const {
        DrawVertex d;
        d.x = v.x;
        d.y = v.y;
        d.z = (float)v.z;
        d.fog = v.fog;
        d.r = v.r;
        d.g = v.g;
        d.b = v.b;
        d.a = v.a;
        if (fst) {
            d.s = v.u * (1.0f / 16.0f);
            d.t = v.v * (1.0f / 16.0f);
            d.q = 1.0f;
        } else {
            d.s = v.s;
            d.t = v.t;
            d.q = v.q;
        }
        return d;
    }

    // buildState() of the last primitive type, valid until a register that feeds it is written (writeReg, loadClut,
    // reset). stateIsBatch: the cached state equals batchState (meaningful while batchActive).
    GsDrawState stateCache{};
    uint32_t stateCachePrim = ~0u;
    bool stateIsBatch = false;
    void invalidateState() { stateCachePrim = ~0u; }

    void beginDraw(uint32_t primType) {
        if (primType != 6)
            lastSpr.valid = false;
        if (primType != stateCachePrim) {
            stateCache = buildState(primType);
            stateCachePrim = primType;
            stateIsBatch = batchActive && std::memcmp(&stateCache, &batchState, sizeof(stateCache)) == 0;
        }
        if (batchActive && !stateIsBatch)
            flushBatch();
        if (batch.size() + 12 > kMaxBatchVerts)
            flushBatch();
        if (!batchActive) {
            batchState = stateCache;
            batchActive = true;
            stateIsBatch = true;
        }
    }

    void emitTri(const Vtx& a, const Vtx& b, const Vtx& c) {
        beginDraw(3);
        const bool fst = (batchState.flags & DF_FST) != 0;
        DrawVertex v0 = toDV(a, fst), v1 = toDV(b, fst), v2 = toDV(c, fst);
        if (!(batchState.flags & DF_IIP)) {
            v0.r = v1.r = v2.r;
            v0.g = v1.g = v2.g;
            v0.b = v1.b = v2.b;
            v0.a = v1.a = v2.a;
        }
        batch.push_back(v0);
        batch.push_back(v1);
        batch.push_back(v2);
        stats.triangles++;
    }

    // Sprite seam fix (deliberately NOT hardware behaviour). On the GS a sprite covers the pixels whose centres lie
    // inside [x0,x1) x [y0,y1), so two textured sprites that were meant to tile a surface but leave a sub-2-pixel gap
    // between them (Drakan's menu background: 0..170.625, 170..340.625 and 342..512, i.e. pixel 341 is never written)
    // leave stale frame buffer pixels in that gap - a dark 1 px line (at 1x; 2*scale px wide at higher scales) that the
    // software GS also shows. When a textured sprite follows another one of identical extent along one axis, both at
    // least 32 px big, and the gap along the other axis is in (0, 2] px, a filler sprite covering exactly the gap is
    // emitted; it replicates the new sprite's border texel row/column (texture coordinates of that edge, constant
    // across the gap). The new sprite itself is left untouched, so its texture mapping is unchanged. Returns true when
    // a filler (f0,f1 = opposite corners with colour/texcoords) was produced. Disable with Config::fixSpriteSeams =
    // false or the environment variable GSR_SEAMFIX=0.
    bool fixSpriteSeam(const DrawVertex& p0, const DrawVertex& p1, DrawVertex& f0, DrawVertex& f1) {
        const float lx = std::min(p0.x, p1.x), rx = std::max(p0.x, p1.x);
        const float ty = std::min(p0.y, p1.y), by = std::max(p0.y, p1.y);
        const float w = rx - lx, h = by - ty;
        const uint64_t fr = batchState.frame;
        constexpr float kMaxGap = 2.0f, kMinSize = 32.0f, kEps = 1.0f / 32.0f;
        bool filler = false;
        if (lastSpr.valid && lastSpr.frame == fr && w >= kMinSize && h >= kMinSize) {
            const LastSprite& L = lastSpr;
            auto build = [&](bool horiz, bool lowEdge, float gap) {
                const float edge = horiz ? (lowEdge ? lx : rx) : (lowEdge ? ty : by);
                // vA = vertex with the smaller coordinate on the other axis (top for horizontal gaps, left for
                // vertical)
                const DrawVertex& vA = (horiz ? (p0.y <= p1.y) : (p0.x <= p1.x)) ? p0 : p1;
                const DrawVertex& vB = (&vA == &p0) ? p1 : p0;
                // the vertex on the facing edge supplies the texcoord that is constant across the gap
                const DrawVertex& edgeVtx = (horiz ? (p0.x == edge) : (p0.y == edge)) ? p0 : p1;
                f0 = edgeVtx;
                f1 = edgeVtx;
                // texcoords are axis aligned for a sprite: the facing-axis value is constant across the gap (edgeVtx),
                // the other-axis value runs from vA to vB.
                if (horiz) {
                    f0.x = lowEdge ? lx - gap : rx;
                    f1.x = lowEdge ? lx : rx + gap;
                    f0.y = ty;
                    f1.y = by;
                    f0.s = f1.s = edgeVtx.s;
                    f0.t = vA.t;
                    f1.t = vB.t;
                } else {
                    f0.y = lowEdge ? ty - gap : by;
                    f1.y = lowEdge ? ty : by + gap;
                    f0.x = lx;
                    f1.x = rx;
                    f0.t = f1.t = edgeVtx.t;
                    f0.s = vA.s;
                    f1.s = vB.s;
                }
                filler = true;
            };
            if (std::fabs(ty - L.ty) < kEps && std::fabs(by - L.by) < kEps) {
                const float gR = lx - L.rx, gL = L.lx - rx;
                if (gR > 0.0f && gR <= kMaxGap)
                    build(true, true, gR);
                else if (gL > 0.0f && gL <= kMaxGap)
                    build(true, false, gL);
            } else if (std::fabs(lx - L.lx) < kEps && std::fabs(rx - L.rx) < kEps) {
                const float gB = ty - L.by, gT = L.ty - by;
                if (gB > 0.0f && gB <= kMaxGap)
                    build(false, true, gB);
                else if (gT > 0.0f && gT <= kMaxGap)
                    build(false, false, gT);
            }
        }
        lastSpr.valid = true;
        lastSpr.lx = lx;
        lastSpr.rx = rx;
        lastSpr.ty = ty;
        lastSpr.by = by;
        lastSpr.frame = fr;
        return filler;
    }

    void emitSprite(const Vtx& a, const Vtx& b) {
        beginDraw(6);
        const bool fst = (batchState.flags & DF_FST) != 0;
        DrawVertex p0 = toDV(a, fst), p1 = toDV(b, fst);
        if (!fst) {
            // sprites are interpolated in (s/q, t/q); normalise so the shader's divide stays valid
            auto norm = [](DrawVertex& d) {
                const float iq = (d.q != 0.0f) ? 1.0f / d.q : 1.0f;
                d.s *= iq;
                d.t *= iq;
                d.q = 1.0f;
            };
            norm(p0);
            norm(p1);
        }
        DrawVertex f0, f1;
        bool hasFiller = false;
        if (cfg.fixSpriteSeams && (batchState.flags & DF_TME))
            hasFiller = fixSpriteSeam(p0, p1, f0, f1);
        else
            lastSpr.valid = false;
        auto make = [&](float x, float y, float s, float t) {
            DrawVertex d = p1;
            d.x = x;
            d.y = y;
            d.s = s;
            d.t = t;
            return d;
        };
        const DrawVertex tl = make(p0.x, p0.y, p0.s, p0.t);
        const DrawVertex tr = make(p1.x, p0.y, p1.s, p0.t);
        const DrawVertex bl = make(p0.x, p1.y, p0.s, p1.t);
        const DrawVertex br = make(p1.x, p1.y, p1.s, p1.t);
        batch.push_back(tl);
        batch.push_back(tr);
        batch.push_back(bl);
        batch.push_back(tr);
        batch.push_back(br);
        batch.push_back(bl);
        stats.triangles += 2;
        if (hasFiller) {
            const DrawVertex ftl = make(f0.x, f0.y, f0.s, f0.t);
            const DrawVertex ftr = make(f1.x, f0.y, f1.s, f0.t);
            const DrawVertex fbl = make(f0.x, f1.y, f0.s, f1.t);
            const DrawVertex fbr = make(f1.x, f1.y, f1.s, f1.t);
            batch.push_back(ftl);
            batch.push_back(ftr);
            batch.push_back(fbl);
            batch.push_back(ftr);
            batch.push_back(fbr);
            batch.push_back(fbl);
            stats.triangles += 2;
        }
    }

    void emitLine(const Vtx& a, const Vtx& b) {
        beginDraw(1);
        const bool fst = (batchState.flags & DF_FST) != 0;
        DrawVertex p0 = toDV(a, fst), p1 = toDV(b, fst);
        if (!(batchState.flags & DF_IIP)) {
            p0.r = p1.r;
            p0.g = p1.g;
            p0.b = p1.b;
            p0.a = p1.a;
        }
        float dx = p1.x - p0.x, dy = p1.y - p0.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        float nx, ny;
        if (len < 1e-4f) {
            nx = 0.5f;
            ny = 0.0f;
            dx = 0.5f;
            dy = 0;
        } else {
            nx = -dy / len * 0.5f;
            ny = dx / len * 0.5f;
            dx = dx / len * 0.5f;
            dy = dy / len * 0.5f;
        }
        auto mk = [&](const DrawVertex& v, float sx, float sy) {
            DrawVertex d = v;
            d.x += sx;
            d.y += sy;
            return d;
        };
        const DrawVertex a0 = mk(p0, nx - dx, ny - dy), a1 = mk(p0, -nx - dx, -ny - dy), b0 = mk(p1, nx + dx, ny + dy),
                         b1 = mk(p1, -nx + dx, -ny + dy);
        batch.push_back(a0);
        batch.push_back(a1);
        batch.push_back(b0);
        batch.push_back(a1);
        batch.push_back(b1);
        batch.push_back(b0);
        stats.triangles += 2;
    }

    void emitPoint(const Vtx& a) {
        beginDraw(0);
        const bool fst = (batchState.flags & DF_FST) != 0;
        const DrawVertex p = toDV(a, fst);
        auto mk = [&](float sx, float sy) {
            DrawVertex d = p;
            d.x += sx;
            d.y += sy;
            return d;
        };
        const DrawVertex v0 = mk(-0.5f, -0.5f), v1 = mk(0.5f, -0.5f), v2 = mk(-0.5f, 0.5f), v3 = mk(0.5f, 0.5f);
        batch.push_back(v0);
        batch.push_back(v1);
        batch.push_back(v2);
        batch.push_back(v1);
        batch.push_back(v3);
        batch.push_back(v2);
        stats.triangles += 2;
    }

    void kick(uint32_t X, uint32_t Y, uint32_t Z, int fogOverride, bool doDraw) {
        const Ctx& c = ctx[curCtx()];
        const int ofx = (int)(c.xyoffset & 0xFFFF), ofy = (int)((c.xyoffset >> 32) & 0xFFFF);
        if (qn >= 4)
            qn = 0;
        Vtx& v = q[qn++];
        v.x = ((int)(X & 0xFFFF) - ofx) * (1.0f / 16.0f);
        v.y = ((int)(Y & 0xFFFF) - ofy) * (1.0f / 16.0f);
        v.z = Z;
        v.r = cr;
        v.g = cg;
        v.b = cb;
        v.a = ca;
        v.fog = (fogOverride >= 0) ? (uint8_t)fogOverride : cfog;
        v.s = cs;
        v.t = ct;
        v.q = cq;
        v.u = cu;
        v.v = cv;
        stats.vertices++;

        const uint32_t type = (uint32_t)(prim & 7);
        static const int need[8] = {1, 2, 2, 3, 3, 3, 2, 1};
        if (qn < need[type])
            return;
        switch (type) {
            case 0:
                if (doDraw)
                    emitPoint(q[0]);
                qn = 0;
                break;
            case 1:
                if (doDraw)
                    emitLine(q[0], q[1]);
                qn = 0;
                break;
            case 2:
                if (doDraw)
                    emitLine(q[0], q[1]);
                q[0] = q[1];
                qn = 1;
                break;
            case 3:
                if (doDraw)
                    emitTri(q[0], q[1], q[2]);
                qn = 0;
                break;
            case 4:
                if (doDraw)
                    emitTri(q[0], q[1], q[2]);
                q[0] = q[1];
                q[1] = q[2];
                qn = 2;
                break;
            case 5:
                if (doDraw)
                    emitTri(q[0], q[1], q[2]);
                q[1] = q[2];
                qn = 2;
                break;
            case 6:
                if (doDraw)
                    emitSprite(q[0], q[1]);
                qn = 0;
                break;
            default:
                qn = 0;
                break;
        }
        if (doDraw)
            stats.primitives++;
    }

    // ------------------------------------------------------------------------------------------------
    // CLUT
    static uint32_t swap34(uint32_t i) { return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1); }

    void loadClut(uint64_t tex0) {
        const uint32_t psm = (uint32_t)bitsOf(tex0, 20, 6);
        const uint32_t cld = (uint32_t)bitsOf(tex0, 61, 3);
        if (cld == 0 || !isIndexed(psm))
            return;
        const uint32_t cbp = (uint32_t)bitsOf(tex0, 37, 14);
        bool load = false;
        switch (cld) {
            case 1:
                load = true;
                break;
            case 2:
                load = true;
                cbp0 = cbp;
                break;
            case 3:
                load = true;
                cbp1 = cbp;
                break;
            case 4:
                if (cbp != cbp0) {
                    load = true;
                    cbp0 = cbp;
                }
                break;
            case 5:
                if (cbp != cbp1) {
                    load = true;
                    cbp1 = cbp;
                }
                break;
            default:
                break;
        }
        if (!load)
            return;
        flushBatch();
        const uint32_t cpsm = (uint32_t)bitsOf(tex0, 51, 4);
        const uint32_t csm = (uint32_t)bitsOf(tex0, 55, 1);
        const uint32_t csa = (uint32_t)bitsOf(tex0, 56, 5);
        const bool is8 = (psm == PSM_T8 || psm == PSM_T8H);
        const bool is16 = (cpsm & 2) != 0; // CT16 = 2, CT16S = 10 ; CT32 = 0, CT24 = 1
        const uint32_t rpsm = is16 ? (cpsm == PSM_CT16S ? PSM_CT16S : PSM_CT16) : (cpsm == 1 ? PSM_CT24 : PSM_CT32);
        const uint32_t count = is8 ? 256 : 16;
        const uint32_t base = is8 ? 0 : csa * 16;

        // make sure GPU-drawn data is visible in memory: exactly the blocks the entries below are read from (a fixed
        // 64-block range reached into the HUD orb target next to the HUD palette and read it back every frame)
        {
            uint32_t lo, hi;
            if (csm == 0)
                spanBlocks(rpsm, cbp, 1, 0, 0, is8 || is16 ? 16 : 8, is8 ? 16 : is16 ? 1 : 2, lo, hi);
            else {
                const uint32_t cbw = (uint32_t)bitsOf(texclut, 0, 6);
                const uint32_t cou = (uint32_t)bitsOf(texclut, 6, 6);
                const uint32_t cov = (uint32_t)bitsOf(texclut, 12, 10);
                spanBlocks(rpsm, cbp, cbw ? cbw : 1, (cou * 16) & 2047, cov, count, 1, lo, hi);
            }
            backend->flushToMemory(lo, hi);
        }

        clut.is16 = is16;
        clut.cpsm = rpsm;
        if (csm == 0) {
            // CSM1 entries lie in the first 16x16 pixels of the page at CBP (buffer width 1): fixed page offsets.
            const uint16_t* offsets = rpsm == PSM_CT16    ? pixel_pages::Page<PSM_CT16>::offsets().data()
                                      : rpsm == PSM_CT16S ? pixel_pages::Page<PSM_CT16S>::offsets().data()
                                                          : pixel_pages::Page<PSM_CT32>::offsets().data();
            const uint8_t* vram = mem.data();
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t x, y;
                if (is8) {
                    const uint32_t r = swap34(j);
                    x = r & 15;
                    y = r >> 4;
                } else if (is16) {
                    x = j & 15;
                    y = 0;
                } else {
                    x = j & 7;
                    y = j >> 3;
                }
                const uint8_t* p = vram + ((cbp * 256u + offsets[y * 64u + x]) & (kVramSize - 1));
                uint32_t val;
                if (is16) {
                    uint16_t half;
                    std::memcpy(&half, p, 2);
                    val = half;
                } else {
                    std::memcpy(&val, p, 4);
                    if (rpsm == PSM_CT24)
                        val &= 0xFFFFFFu;
                }
                if (base + j < 512)
                    clut.raw[base + j] = val;
            }
        }
        for (uint32_t j = 0; j < count && csm != 0; ++j) {
            uint32_t val;
            if (csm == 0) {
                uint32_t x, y;
                if (is8) {
                    const uint32_t r = swap34(j);
                    x = r & 15;
                    y = r >> 4;
                } else if (is16) {
                    x = j & 15;
                    y = 0;
                } else {
                    x = j & 7;
                    y = j >> 3;
                }
                val = mem.read(rpsm, cbp, 1, x, y);
            } else {
                const uint32_t cbw = (uint32_t)bitsOf(texclut, 0, 6);
                const uint32_t cou = (uint32_t)bitsOf(texclut, 6, 6);
                const uint32_t cov = (uint32_t)bitsOf(texclut, 12, 10);
                val = mem.read(rpsm, cbp, cbw ? cbw : 1, (cou * 16 + j) & 2047, cov);
            }
            if (base + j < 512)
                clut.raw[base + j] = val;
        }
        clut.serial++;
        invalidateState();
        stats.clutLoads++;
        if (cfg.onClut) {
            ClutEvent ev;
            ev.tex0 = tex0;
            ev.texclut = texclut;
            ev.raw = clut.raw;
            ev.cpsm = rpsm;
            cfg.onClut(ev);
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Register writes
    void setTex0(int c, uint64_t v) {
        ctx[c].tex0 = v;
        loadClut(v);
    }

    void writeReg(uint32_t reg, uint64_t v) {
        switch (reg) {
            case R_RGBAQ:
            case R_ST:
            case R_UV:
            case R_XYZF2:
            case R_XYZ2:
            case R_XYZF3:
            case R_XYZ3:
            case R_FOG:
                break; // vertex registers: the draw state is unchanged
            default:
                invalidateState();
                break;
        }
        switch (reg) {
            case R_PRIM:
                prim = v & 0x7FF;
                qn = 0;
                break;
            case R_RGBAQ:
                cr = (uint8_t)v;
                cg = (uint8_t)(v >> 8);
                cb = (uint8_t)(v >> 16);
                ca = (uint8_t)(v >> 24);
                cq = asFloat((uint32_t)(v >> 32));
                break;
            case R_ST:
                cs = asFloat((uint32_t)v);
                ct = asFloat((uint32_t)(v >> 32));
                break;
            case R_UV:
                cu = (uint16_t)(v & 0x3FFF);
                cv = (uint16_t)((v >> 16) & 0x3FFF);
                break;
            case R_XYZF2:
                kick((uint32_t)(v & 0xFFFF), (uint32_t)((v >> 16) & 0xFFFF), (uint32_t)((v >> 32) & 0xFFFFFF),
                     (int)(v >> 56), true);
                break;
            case R_XYZ2:
                kick((uint32_t)(v & 0xFFFF), (uint32_t)((v >> 16) & 0xFFFF), (uint32_t)(v >> 32), -1, true);
                break;
            case R_XYZF3:
                kick((uint32_t)(v & 0xFFFF), (uint32_t)((v >> 16) & 0xFFFF), (uint32_t)((v >> 32) & 0xFFFFFF),
                     (int)(v >> 56), false);
                break;
            case R_XYZ3:
                kick((uint32_t)(v & 0xFFFF), (uint32_t)((v >> 16) & 0xFFFF), (uint32_t)(v >> 32), -1, false);
                break;
            case R_TEX0_1:
                setTex0(0, v);
                break;
            case R_TEX0_2:
                setTex0(1, v);
                break;
            case R_CLAMP_1:
                ctx[0].clamp = v;
                break;
            case R_CLAMP_2:
                ctx[1].clamp = v;
                break;
            case R_FOG:
                cfog = (uint8_t)(v >> 56);
                break;
            case R_TEX1_1:
                ctx[0].tex1 = v;
                break;
            case R_TEX1_2:
                ctx[1].tex1 = v;
                break;
            case R_TEX2_1:
            case R_TEX2_2: {
                const int c = (reg == R_TEX2_2) ? 1 : 0;
                const uint64_t mask = (0x3Full << 20) | (~0ull << 37);
                ctx[c].tex0 = (ctx[c].tex0 & ~mask) | (v & mask);
                loadClut(ctx[c].tex0);
                break;
            }
            case R_XYOFFSET_1:
                ctx[0].xyoffset = v;
                break;
            case R_XYOFFSET_2:
                ctx[1].xyoffset = v;
                break;
            case R_PRMODECONT:
                prmodecont = v & 1;
                break;
            case R_PRMODE:
                prmode = (v >> 3) & 0xFF;
                break;
            case R_TEXCLUT:
                texclut = v;
                break;
            case R_SCANMSK:
                scanmsk = v;
                break;
            case R_MIPTBP1_1:
                ctx[0].miptbp1 = v;
                break;
            case R_MIPTBP1_2:
                ctx[1].miptbp1 = v;
                break;
            case R_MIPTBP2_1:
                ctx[0].miptbp2 = v;
                break;
            case R_MIPTBP2_2:
                ctx[1].miptbp2 = v;
                break;
            case R_TEXA:
                texa = v;
                break;
            case R_FOGCOL:
                fogcol = v;
                break;
            case R_TEXFLUSH:
                break;
            case R_SCISSOR_1:
                ctx[0].scissor = v;
                break;
            case R_SCISSOR_2:
                ctx[1].scissor = v;
                break;
            case R_ALPHA_1:
                ctx[0].alpha = v;
                break;
            case R_ALPHA_2:
                ctx[1].alpha = v;
                break;
            case R_DIMX:
                dimx = v;
                break;
            case R_DTHE:
                dthe = v & 1;
                break;
            case R_COLCLAMP:
                colclamp = v & 1;
                break;
            case R_TEST_1:
                ctx[0].test = v;
                break;
            case R_TEST_2:
                ctx[1].test = v;
                break;
            case R_PABE:
                pabe = v & 1;
                break;
            case R_FBA_1:
                ctx[0].fba = v;
                break;
            case R_FBA_2:
                ctx[1].fba = v;
                break;
            case R_FRAME_1:
                ctx[0].frame = v;
                break;
            case R_FRAME_2:
                ctx[1].frame = v;
                break;
            case R_ZBUF_1:
                ctx[0].zbuf = v;
                break;
            case R_ZBUF_2:
                ctx[1].zbuf = v;
                break;
            case R_BITBLTBUF:
                bitbltbuf = v;
                break;
            case R_TRXPOS:
                trxpos = v;
                break;
            case R_TRXREG:
                trxreg = v;
                break;
            case R_TRXDIR:
                startTransfer((uint32_t)(v & 3));
                break;
            case R_HWREG: {
                uint8_t b[8];
                std::memcpy(b, &v, 8);
                imageData(b, 8);
                break;
            }
            case R_SIGNAL:
                siglblid = (siglblid & ~(v >> 32)) | ((v & (v >> 32)) & 0xFFFFFFFFull);
                csr |= 1;
                if (owner->onSignal)
                    owner->onSignal(v & 0xFFFFFFFFull);
                break;
            case R_FINISH:
                flushBatch();
                csr |= 2;
                if (owner->onFinish)
                    owner->onFinish();
                break;
            case R_LABEL:
                siglblid = (siglblid & ~(v >> 32)) | ((v & 0xFFFFFFFFull) & (v >> 32));
                break;
            default:
                break;
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Transfers
    void spanBlocks(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint32_t& lo, uint32_t& hi) {
        lo = 0xFFFFFFFFu;
        hi = 0;
        if (!w || !h) {
            lo = 0;
            return;
        }
        const PsmGeom g = geomOf(psm);
        for (uint32_t by = y & ~(g.blockH - 1); by < y + h; by += g.blockH)
            for (uint32_t bx = x & ~(g.blockW - 1); bx < x + w; bx += g.blockW) {
                const uint32_t b = blockOf(psm, bp, bw, bx, by);
                lo = std::min(lo, b);
                hi = std::max(hi, b);
            }
    }

    void startTransfer(uint32_t dir) {
        if (xfer.active && xfer.intoTarget) // abandoned upload: publish what arrived
            backend->completeTargetUpload(xfer.dpsm, xfer.dbp, xfer.dbw, xfer.x0, xfer.y0, xfer.w, xfer.h, true);
        trxdir = dir;
        xfer = Xfer{};
        const uint32_t sbp = (uint32_t)bitsOf(bitbltbuf, 0, 14), sbw = (uint32_t)bitsOf(bitbltbuf, 16, 6),
                       spsm = (uint32_t)bitsOf(bitbltbuf, 24, 6);
        const uint32_t dbp = (uint32_t)bitsOf(bitbltbuf, 32, 14), dbw = (uint32_t)bitsOf(bitbltbuf, 48, 6),
                       dpsm = (uint32_t)bitsOf(bitbltbuf, 56, 6);
        const uint32_t ssax = (uint32_t)bitsOf(trxpos, 0, 11), ssay = (uint32_t)bitsOf(trxpos, 16, 11);
        const uint32_t dsax = (uint32_t)bitsOf(trxpos, 32, 11), dsay = (uint32_t)bitsOf(trxpos, 48, 11);
        const uint32_t rrw = (uint32_t)bitsOf(trxreg, 0, 12), rrh = (uint32_t)bitsOf(trxreg, 32, 12);
        if (dir == 3)
            return;
        if (xferLog)
            std::fprintf(
                stderr, "[xfer v%llu] dir=%u src bp=%x bw=%u psm=%x (%u,%u)  dst bp=%x bw=%u psm=%x (%u,%u)  %ux%u\n",
                (unsigned long long)vsyncCount, dir, sbp, sbw, spsm, ssax, ssay, dbp, dbw, dpsm, dsax, dsay, rrw, rrh);
        flushBatch();
        if (dir != 0)
            applyMovieFrames(); // the GS reads (or copies) memory the HD frame stands in for
        if (dir == 0) {
            if (!isValidPsm(dpsm))
                return;
            uint32_t lo, hi;
            spanBlocks(dpsm, dbp, dbw, dsax, dsay, rrw, rrh, lo, hi);
            xfer.intoTarget = backend->beginTargetUpload(dpsm, dbp, dbw, dsax, dsay, rrw, rrh);
            if (!xfer.intoTarget)
                backend->flushToMemory(lo, hi);
            xfer.active = true;
            xfer.dbp = dbp;
            xfer.dbw = dbw;
            xfer.dpsm = dpsm;
            xfer.x0 = dsax;
            xfer.y0 = dsay;
            xfer.w = rrw;
            xfer.h = rrh;
            xfer.cx = 0;
            xfer.cy = 0;
            xfer.pixelsLeft = (uint64_t)rrw * rrh;
            xfer.movieBlock = dpsm == PSM_CT32 && rrw == movie::kBlock && rrh == movie::kBlock &&
                              !(dsax % movie::kBlock) && !(dsay % movie::kBlock) && movieWatching();
            if (cfg.onUpload || xfer.movieBlock)
                xfer.raw.clear();
            mem.touchRect(dpsm, dbp, dbw, dsax, dsay, rrw, rrh);
            if (xfer.intoTarget) // memory is not the target's source while the pixels stream in
                backend->completeTargetUpload(dpsm, dbp, dbw, dsax, dsay, rrw, rrh, false);
            stats.transfersH2L++;
        } else if (dir == 1) {
            if (!isValidPsm(spsm))
                return;
            uint32_t lo, hi;
            spanBlocks(spsm, sbp, sbw, ssax, ssay, rrw, rrh, lo, hi);
            backend->flushToMemory(lo, hi);
            // pack the rectangle into the stream
            l2h.clear();
            l2hPos = 0;
            const uint32_t bits = transferBits(spsm);
            std::vector<uint8_t> out;
            out.reserve(((uint64_t)rrw * rrh * bits + 7) / 8 + 16);
            uint32_t nibAcc = 0;
            bool nibHave = false;
            for (uint32_t y = 0; y < rrh; ++y)
                for (uint32_t x = 0; x < rrw; ++x) {
                    const uint32_t v = mem.read(spsm, sbp, sbw, (ssax + x) & 2047, (ssay + y) & 2047);
                    switch (bits) {
                        case 32:
                            for (int k = 0; k < 4; ++k)
                                out.push_back((uint8_t)(v >> (8 * k)));
                            break;
                        case 24:
                            for (int k = 0; k < 3; ++k)
                                out.push_back((uint8_t)(v >> (8 * k)));
                            break;
                        case 16:
                            out.push_back((uint8_t)v);
                            out.push_back((uint8_t)(v >> 8));
                            break;
                        case 8:
                            out.push_back((uint8_t)v);
                            break;
                        default:
                            if (!nibHave) {
                                nibAcc = v & 0xF;
                                nibHave = true;
                            } else {
                                out.push_back((uint8_t)(nibAcc | ((v & 0xF) << 4)));
                                nibHave = false;
                            }
                            break;
                    }
                }
            if (nibHave)
                out.push_back((uint8_t)nibAcc);
            while (out.size() % 16)
                out.push_back(0);
            l2h = std::move(out);
            stats.transfersL2H++;
        } else if (dir == 2) {
            if (!isValidPsm(spsm) || !isValidPsm(dpsm))
                return;
            uint32_t lo, hi, lo2, hi2;
            spanBlocks(spsm, sbp, sbw, ssax, ssay, rrw, rrh, lo, hi);
            spanBlocks(dpsm, dbp, dbw, dsax, dsay, rrw, rrh, lo2, hi2);
            backend->flushToMemory(lo, hi);
            backend->flushToMemory(lo2, hi2);
            for (uint32_t y = 0; y < rrh; ++y)
                for (uint32_t x = 0; x < rrw; ++x) {
                    const uint32_t v = mem.read(spsm, sbp, sbw, (ssax + x) & 2047, (ssay + y) & 2047);
                    mem.write(dpsm, dbp, dbw, (dsax + x) & 2047, (dsay + y) & 2047, v);
                }
            mem.touchRect(dpsm, dbp, dbw, dsax, dsay, rrw, rrh);
            stats.transfersL2L++;
        }
    }

    void putPixel(uint32_t v) {
        if (xfer.pixelsLeft == 0)
            return;
        mem.write(xfer.dpsm, xfer.dbp, xfer.dbw, (xfer.x0 + xfer.cx) & 2047, (xfer.y0 + xfer.cy) & 2047, v);
        xfer.pixelsLeft--;
        if (++xfer.cx >= xfer.w) {
            xfer.cx = 0;
            xfer.cy++;
        }
    }

    void imageData(const uint8_t* d, uint32_t n) {
        if (!xfer.active || trxdir != 0)
            return;
        const uint32_t bits = transferBits(xfer.dpsm);
        uint32_t i = 0;
        if (cfg.onUpload || xfer.movieBlock) {
            const uint64_t need = ((uint64_t)xfer.w * xfer.h * bits + 7) / 8;
            const uint64_t take = std::min<uint64_t>(n, need > xfer.raw.size() ? need - xfer.raw.size() : 0);
            xfer.raw.insert(xfer.raw.end(), d, d + take);
        }
        if (writeImageChunk(mem, xfer.dpsm, xfer.dbp, xfer.dbw, xfer.x0, xfer.y0, xfer.w, xfer.cx, xfer.cy,
                            xfer.pixelsLeft, d, n)) {
            // Pixel storage only; transfer completion below remains shared.
        } else if (bits == 32) {
            // fast aligned path
            while (i + 4 <= n && xfer.pixelsLeft) {
                uint32_t v;
                std::memcpy(&v, d + i, 4);
                putPixel(v);
                i += 4;
            }
        } else if (bits == 8) {
            while (i < n && xfer.pixelsLeft)
                putPixel(d[i++]);
        } else if (bits == 4) {
            while (i < n && xfer.pixelsLeft) {
                putPixel(d[i] & 0xF);
                if (xfer.pixelsLeft)
                    putPixel(d[i] >> 4);
                ++i;
            }
        } else if (bits == 16) {
            while (i + 2 <= n && xfer.pixelsLeft) {
                putPixel((uint32_t)d[i] | ((uint32_t)d[i + 1] << 8));
                i += 2;
            }
        } else // 24
        {
            while (i < n && xfer.pixelsLeft) {
                xfer.carry[xfer.carryLen++] = d[i++];
                if (xfer.carryLen == 3) {
                    putPixel(xfer.carry[0] | (xfer.carry[1] << 8) | (xfer.carry[2] << 16));
                    xfer.carryLen = 0;
                }
            }
        }
        if (xfer.pixelsLeft == 0) {
            xfer.active = false;
            mem.touchRect(xfer.dpsm, xfer.dbp, xfer.dbw, xfer.x0, xfer.y0, xfer.w, xfer.h);
            if (xfer.intoTarget)
                backend->completeTargetUpload(xfer.dpsm, xfer.dbp, xfer.dbw, xfer.x0, xfer.y0, xfer.w, xfer.h, true);
            if (xfer.movieBlock && xfer.raw.size() == movie::kBlockBytes)
                movieBlockUploaded(xfer.dbp, xfer.dbw, xfer.x0, xfer.y0, xfer.raw.data());
            if (cfg.onUpload) {
                UploadEvent ev;
                ev.dbp = xfer.dbp;
                ev.dbw = xfer.dbw;
                ev.dpsm = xfer.dpsm;
                ev.x = xfer.x0;
                ev.y = xfer.y0;
                ev.w = xfer.w;
                ev.h = xfer.h;
                ev.data = xfer.raw.data();
                ev.bytes = (uint32_t)xfer.raw.size();
                cfg.onUpload(ev);
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    // GIF
    void handlePacked(uint32_t desc, const uint8_t* qw) {
        uint64_t lo, hi;
        std::memcpy(&lo, qw, 8);
        std::memcpy(&hi, qw + 8, 8);
        switch (desc) {
            case 0x0:
                writeReg(R_PRIM, lo & 0x7FF);
                break;
            case 0x1:
                cr = qw[0];
                cg = qw[4];
                cb = qw[8];
                ca = qw[12];
                break;
            case 0x2:
                cs = asFloat((uint32_t)lo);
                ct = asFloat((uint32_t)(lo >> 32));
                cq = asFloat((uint32_t)hi);
                break;
            case 0x3:
                cu = (uint16_t)(lo & 0x3FFF);
                cv = (uint16_t)((lo >> 32) & 0x3FFF);
                break;
            case 0x4: {
                const bool adc = (hi >> 47) & 1;
                kick((uint32_t)(lo & 0xFFFF), (uint32_t)((lo >> 32) & 0xFFFF), (uint32_t)((hi >> 4) & 0xFFFFFF),
                     (int)((hi >> 36) & 0xFF), !adc);
                break;
            }
            case 0x5: {
                const bool adc = (hi >> 47) & 1;
                kick((uint32_t)(lo & 0xFFFF), (uint32_t)((lo >> 32) & 0xFFFF), (uint32_t)hi, -1, !adc);
                break;
            }
            case 0x6:
            case 0x7:
            case 0x8:
            case 0x9:
                writeReg(desc, lo);
                break;
            case 0xA:
                cfog = (uint8_t)((hi >> 36) & 0xFF);
                break;
            case 0xC:
                kick((uint32_t)(lo & 0xFFFF), (uint32_t)((lo >> 32) & 0xFFFF), (uint32_t)((hi >> 4) & 0xFFFFFF),
                     (int)((hi >> 36) & 0xFF), false);
                break;
            case 0xD:
                kick((uint32_t)(lo & 0xFFFF), (uint32_t)((lo >> 32) & 0xFFFF), (uint32_t)hi, -1, false);
                break;
            case 0xE:
                writeReg((uint32_t)(hi & 0xFF), lo);
                break;
            default:
                break;
        }
    }

    void gifSubmit(uint32_t pathIdx, const uint8_t* data, uint32_t size) {
        PathState& ps = paths[pathIdx];
        stats.gifPackets++;
        while (size >= 16) {
            if (ps.qwLeft == 0) {
                uint64_t lo, hi;
                std::memcpy(&lo, data, 8);
                std::memcpy(&hi, data + 8, 8);
                data += 16;
                size -= 16;
                stats.gifTags++;
                const uint32_t nloop = (uint32_t)(lo & 0x7FFF);
                const bool pre = (lo >> 46) & 1;
                ps.flg = (uint32_t)((lo >> 58) & 3);
                ps.nreg = (uint32_t)((lo >> 60) & 0xF);
                if (ps.nreg == 0)
                    ps.nreg = 16;
                ps.regs = hi;
                ps.regIdx = 0;
                if (pre && ps.flg == 0)
                    writeReg(R_PRIM, (lo >> 47) & 0x7FF);
                if (nloop == 0)
                    continue;
                if (ps.flg == 0)
                    ps.qwLeft = nloop * ps.nreg;
                else if (ps.flg == 1) {
                    ps.valuesLeft = nloop * ps.nreg;
                    ps.qwLeft = (ps.valuesLeft + 1) / 2;
                } else
                    ps.qwLeft = nloop;
                continue;
            }
            if (ps.flg == 0) {
                handlePacked((uint32_t)((ps.regs >> (4 * ps.regIdx)) & 0xF), data);
                if (++ps.regIdx >= ps.nreg)
                    ps.regIdx = 0;
                ps.qwLeft--;
                data += 16;
                size -= 16;
            } else if (ps.flg == 1) {
                for (int h = 0; h < 2 && ps.valuesLeft; ++h) {
                    uint64_t v;
                    std::memcpy(&v, data + h * 8, 8);
                    const uint32_t desc = (uint32_t)((ps.regs >> (4 * ps.regIdx)) & 0xF);
                    if (desc != 0xF && desc != 0xE && desc != 0xB)
                        writeReg(desc, v);
                    if (++ps.regIdx >= ps.nreg)
                        ps.regIdx = 0;
                    ps.valuesLeft--;
                }
                ps.qwLeft--;
                data += 16;
                size -= 16;
            } else {
                // IMAGE / disabled: consume as many qwords as available
                const uint32_t take = std::min<uint32_t>(ps.qwLeft, size / 16);
                if (ps.flg == 2)
                    imageData(data, take * 16);
                data += take * 16;
                size -= take * 16;
                ps.qwLeft -= take;
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    DisplayState buildDisplay() const {
        DisplayState d;
        d.pmode = pmode;
        d.smode2 = smode2;
        d.bgcolor = bgcolor;
        auto fill = [&](DisplayCircuit& c, uint64_t fb, uint64_t disp, bool en) {
            c.enable = en;
            c.fbp = (uint32_t)bitsOf(fb, 0, 9);
            c.fbw = (uint32_t)bitsOf(fb, 9, 6);
            c.psm = (uint32_t)bitsOf(fb, 15, 5);
            c.dbx = (uint32_t)bitsOf(fb, 32, 11);
            c.dby = (uint32_t)bitsOf(fb, 43, 11);
            c.dx = (uint32_t)bitsOf(disp, 0, 12);
            c.dy = (uint32_t)bitsOf(disp, 12, 11);
            c.magh = (uint32_t)bitsOf(disp, 23, 4);
            c.magv = (uint32_t)bitsOf(disp, 27, 2);
            c.dw = (uint32_t)bitsOf(disp, 32, 12);
            c.dh = (uint32_t)bitsOf(disp, 44, 11);
        };
        fill(d.c[0], dispfb1, display1, (pmode & 1) != 0);
        fill(d.c[1], dispfb2, display2, (pmode & 2) != 0);
        // Anti-flicker filter: libgs reads the same frame buffer with both circuits, one line apart, and blends them
        // 50/50 (ALP). On a progressive output that is only a vertical blur (and at an upscaled resolution a whole
        // native line of it).
        if (!cfg.flickerFilter && d.c[0].enable && d.c[1].enable && d.c[0].fbp == d.c[1].fbp &&
            d.c[0].fbw == d.c[1].fbw && d.c[0].psm == d.c[1].psm && ((pmode >> 5) & 1) != 0 /*MMOD: ALP blend*/ &&
            (d.c[0].dby > d.c[1].dby ? d.c[0].dby - d.c[1].dby : d.c[1].dby - d.c[0].dby) <= 2 &&
            d.c[0].dbx == d.c[1].dbx && d.c[0].width() == d.c[1].width())
            d.c[1].enable = false;
        return d;
    }

    uint64_t* privSlot(uint32_t off) {
        switch (off) {
            case 0x00:
                return &pmode;
            case 0x10:
                return &smode1;
            case 0x20:
                return &smode2;
            case 0x30:
                return &srfsh;
            case 0x40:
                return &synch1;
            case 0x50:
                return &synch2;
            case 0x60:
                return &syncv;
            case 0x70:
                return &dispfb1;
            case 0x80:
                return &display1;
            case 0x90:
                return &dispfb2;
            case 0xA0:
                return &display2;
            case 0xB0:
                return &extbuf;
            case 0xC0:
                return &extdata;
            case 0xD0:
                return &extwrite;
            case 0xE0:
                return &bgcolor;
            case 0x1010:
                return &imr;
            case 0x1040:
                return &busdir;
            default:
                return nullptr;
        }
    }
};

// ====================================================================================================
Renderer::Renderer() = default;
Renderer::~Renderer() = default;

std::unique_ptr<Renderer> Renderer::create(const Config& cfg) {
    std::unique_ptr<Renderer> r(new Renderer());
    r->m_impl = std::make_unique<Impl>();
    r->m_impl->cfg = cfg;
    r->m_impl->modelNeuralRequested.store(cfg.neuralRendering);
    r->m_impl->modelSrRequested.store(cfg.upscaleMode != 0);
    if (runtimeFlags().seamFix)
        r->m_impl->cfg.fixSpriteSeams = *runtimeFlags().seamFix;
    r->m_impl->vsyncReq = cfg.vsync;
    r->m_impl->owner = r.get();
    r->m_impl->backend = std::make_unique<Backend>(r->m_impl->mem, cfg, r->m_impl->stats);
    if (!r->m_impl->backend->init())
        return nullptr;
    return r;
}

bool Renderer::valid() const {
    return m_impl && m_impl->backend && m_impl->backend->valid();
}
std::string Renderer::adapterName() const {
    return m_impl->backend->adapterName();
}
Config::Api Renderer::api() const {
    return m_impl->backend->api();
}
const Stats& Renderer::stats() const {
    return m_impl->stats;
}

void Renderer::reset() {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    Impl& s = *m_impl;
    s.flushBatch();
    s.backend->reset();
    std::memset(s.mem.data(), 0, kVramSize);
    s.mem.touchBlocks(0, kVramBlocks);
    s.ctx[0] = Ctx{};
    s.ctx[1] = Ctx{};
    s.qn = 0;
    s.paths[0] = s.paths[1] = s.paths[2] = PathState{};
    s.xfer = Impl::Xfer{};
    s.clut = ClutBuf{};
    s.invalidateState();
    s.csr = 0;
}

void Renderer::submitGifPacket(GifPath path, const uint8_t* data, uint32_t sizeBytes) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    if (!data)
        return;
    m_impl->gifSubmit((uint32_t)path % 3, data, sizeBytes);
}

void Renderer::writeRegister(uint8_t regAddr, uint64_t value) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->writeReg(regAddr, value);
}

void Renderer::writePriv(uint32_t addr, uint64_t value) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    Impl& s = *m_impl;
    const uint32_t off = (addr >= 0x12000000u) ? (addr - 0x12000000u) : addr;
    if (off == 0x1000) {
        // CSR: write-1-to-clear for event bits, RESET
        s.csr &= ~(value & 0x1Full);
        if (value & 0x200)
            s.csr = 0; // soft reset: keep VRAM, clear events
        return;
    }
    if (uint64_t* p = s.privSlot(off))
        *p = value;
    else if (off == 0x1080)
        s.siglblid = value;
}

uint64_t Renderer::readPriv(uint32_t addr) const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    const Impl& s = *m_impl;
    const uint32_t off = (addr >= 0x12000000u) ? (addr - 0x12000000u) : addr;
    if (off == 0x1000)
        return (s.csr & 0xFFFFull) | 0x551B0000ull;
    if (off == 0x1080)
        return s.siglblid;
    if (uint64_t* p = const_cast<Impl&>(s).privSlot(off))
        return *p;
    return 0;
}

void Renderer::vsync(bool presentFrame) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    Impl& s = *m_impl;
    s.applyPending();
    s.signalVblank();
    s.completeDisplayFrame(presentFrame);
    if (onVsync)
        onVsync();
}

void Renderer::signalVblank() {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->signalVblank();
    if (onVsync)
        onVsync();
}

bool Renderer::completeDisplayFrame(bool requireFieldIndependent) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    Impl& s = *m_impl;
    s.applyPending();
    if (requireFieldIndependent && (s.cfg.deinterlace != 0 || s.cfg.flickerFilter))
        return false;
    s.completeDisplayFrame(false);
    return true;
}

bool Renderer::fieldIndependentPresentation() const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    const Impl& s = *m_impl;
    std::lock_guard<std::mutex> settings(m_impl->setMu);
    const auto mode = (s.pend.mask & LiveSettings::F_DEINT) ? s.pend.deinterlace : s.cfg.deinterlace;
    const bool flicker = (s.pend.mask & LiveSettings::F_FLICKER) ? s.pend.flicker : s.cfg.flickerFilter;
    return mode == 0 && !flicker;
}

void Renderer::present() {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    Impl& s = *m_impl;
    s.applyPending();
    s.flushBatch();
    s.backend->latchDisplay(s.buildDisplay(), s.field);
    s.backend->presentLatched();
}

void Renderer::presentLatched() {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->applyPending();
    m_impl->backend->presentLatched();
}

bool Renderer::waitForPresent(uint32_t timeoutMs) {
    void* wait;
    {
        std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
        wait = m_impl->backend->beginPresentWait();
    }
    if (!wait)
        return true;
    // Outside the lock: the GS worker keeps processing GIF packets during the wait for the display.
    const bool ready = Backend::waitPresentHandle(wait, timeoutMs);
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    m_impl->backend->endPresentWait(ready);
    return ready;
}

namespace {
std::mutex& exclMutex() {
    static std::mutex m;
    return m;
}
std::unordered_set<uint64_t>& exclSet() {
    static std::unordered_set<uint64_t> s;
    return s;
}
std::unordered_set<uint64_t>& photoSet() {
    static std::unordered_set<uint64_t> s;
    return s;
}
struct RuntimeTextures {
    std::unordered_map<uint64_t, std::shared_ptr<const RuntimeTexture>> images;
    std::deque<uint64_t> order;
    size_t bytes = 0;
};
RuntimeTextures& runtimeTextures() {
    static RuntimeTextures r;
    return r;
}
} // namespace
uint64_t textureContentHash(const uint32_t* rgba, uint32_t w, uint32_t h) {
    return texup::hashContent(rgba, w, h);
}
void excludeTextureHash(uint64_t hash) {
    std::lock_guard<std::mutex> l(exclMutex());
    exclSet().insert(hash);
}
bool isTextureHashExcluded(uint64_t hash) {
    std::lock_guard<std::mutex> l(exclMutex());
    return exclSet().count(hash) != 0;
}
void preferPhotographicTexture(uint64_t hash) {
    std::lock_guard<std::mutex> lock(exclMutex());
    auto& photos = photoSet();
    if (photos.size() >= 1024)
        photos.clear();
    photos.insert(hash);
}
bool isPhotographicTexture(uint64_t hash) {
    std::lock_guard<std::mutex> lock(exclMutex());
    return photoSet().count(hash) != 0;
}

namespace {
std::mutex& shotMutex() {
    static std::mutex m;
    return m;
}
std::deque<std::string>& shotQueue() {
    static std::deque<std::string> q;
    return q;
}
} // namespace
void requestWindowShot(const std::string& path) {
    std::lock_guard<std::mutex> lock(shotMutex());
    shotQueue().push_back(path);
}
bool takeWindowShotRequest(std::string& path) {
    std::lock_guard<std::mutex> lock(shotMutex());
    if (shotQueue().empty())
        return false;
    path = std::move(shotQueue().front());
    shotQueue().pop_front();
    return true;
}

namespace {
constexpr size_t kRuntimeTextureBudget = 64u * 1024u * 1024u;
size_t runtimeTextureBytes(const RuntimeTexture& t) {
    return t.pixels.size() * 4 + t.coverage.size();
}
bool storeRuntimeTexture(uint64_t hash, std::shared_ptr<RuntimeTexture> image) {
    std::lock_guard<std::mutex> lock(exclMutex());
    auto& r = runtimeTextures();
    if (r.images.count(hash))
        return true;
    const size_t bytes = runtimeTextureBytes(*image);
    while (!r.order.empty() && (r.bytes + bytes > kRuntimeTextureBudget || r.images.size() >= 8)) {
        const auto old = r.order.front();
        r.order.pop_front();
        auto it = r.images.find(old);
        r.bytes -= runtimeTextureBytes(*it->second);
        r.images.erase(it);
    }
    r.bytes += bytes;
    r.order.push_back(hash);
    r.images.emplace(hash, std::move(image));
    exclSet().insert(hash);
    return true;
}
} // namespace

bool registerRuntimeTexture(uint64_t hash, uint32_t w, uint32_t h, const uint32_t* rgba, bool fontAtlas) {
    if (!hash || !rgba || !w || !h || w > 8192 || h > 8192 || uint64_t(w) * h > kRuntimeTextureBudget / 4)
        return false;
    auto image = std::make_shared<RuntimeTexture>();
    image->width = w;
    image->height = h;
    image->pixels.assign(rgba, rgba + size_t(w) * h);
    image->fontAtlas = fontAtlas;
    return storeRuntimeTexture(hash, std::move(image));
}
bool registerRuntimeOverlay(uint64_t hash, const uint32_t* original, uint32_t w, uint32_t h, uint32_t factor,
                            const uint32_t* rgba, const uint8_t* coverage) {
    if (!hash || !original || !rgba || !coverage || !w || !h || (factor != 2 && factor != 4 && factor != 8))
        return false;
    const uint32_t tw = w * factor, th = h * factor;
    if (tw > 8192 || th > 8192 || uint64_t(tw) * th > kRuntimeTextureBudget / 5)
        return false;
    auto image = std::make_shared<RuntimeTexture>();
    image->width = tw;
    image->height = th;
    image->pixels.assign(rgba, rgba + size_t(tw) * th);
    image->coverage.assign(coverage, coverage + size_t(tw) * th);
    image->packHash = texup::hashContent(original, w, h);
    image->packRgbHash = texup::hashContentRgb(original, w, h);
    return storeRuntimeTexture(hash, std::move(image));
}
std::shared_ptr<const RuntimeTexture> runtimeTexture(uint64_t hash) {
    std::lock_guard<std::mutex> lock(exclMutex());
    const auto& r = runtimeTextures();
    const auto it = r.images.find(hash);
    return it == r.images.end() ? nullptr : it->second;
}

void Renderer::setVsync(bool on) {
    Impl& s = *m_impl;
    s.queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_VSYNC;
        p.vsync = on;
    });
    s.vsyncReq = on;
}

bool Renderer::vsyncEnabled() const {
    return m_impl->vsyncReq;
}

void Renderer::setAspect(float a, bool stretch) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_ASPECT;
        p.forcedAspect = a;
        p.stretch = stretch;
    });
}
void Renderer::setTextureFilter(uint32_t m) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_FILTER;
        p.filter = m;
    });
}
void Renderer::setDeinterlace(uint32_t m) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_DEINT;
        p.deinterlace = m;
    });
}
void Renderer::setFlickerFilter(bool on) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_FLICKER;
        p.flicker = on;
    });
}
void Renderer::setSharpPresent(bool on) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_SHARP;
        p.sharp = on;
    });
}
void Renderer::setAnisotropic(uint32_t t) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_ANISO;
        p.aniso = t;
    });
}
void Renderer::setMipBias(float b, bool a) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_MIP;
        p.mipBias = b;
        p.mipAuto = a;
    });
}
void Renderer::setTextureUpscale(uint32_t f, uint32_t flt) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_TEXUP;
        p.upFactor = f;
        p.upFilter = flt;
    });
}
void Renderer::setTextureReplace(bool on) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_REPLACE;
        p.replace = on;
    });
}
void Renderer::setTextureDump(bool on) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_DUMP;
        p.dump = on;
    });
}
void Renderer::rescanTextures() {
    m_impl->queue([&](LiveSettings& p) { p.mask |= LiveSettings::F_RESCAN; });
}
void Renderer::setInternalScale(uint32_t s) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_SCALE;
        p.scale = s < 1 ? 1 : (s > 8 ? 8 : s);
    });
}
void Renderer::setNativeMenu(bool enabled, int screen) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_NATIVE_MENU;
        p.nativeMenu = enabled;
        p.nativeMenuScreen = enabled ? screen : -1;
    });
}
void Renderer::setHudScaleX(float x) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_HUD;
        p.hudScale = (x > 0.2f && x <= 1.0f) ? x : 1.0f;
    });
}
void Renderer::setHudSize(float s) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_HUDSIZE;
        p.hudSize = (s >= 0.3f && s <= 2.0f) ? s : 1.0f;
    });
}
void Renderer::setSmoothMenuHighlight(bool on) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_HLSMOOTH;
        p.hlSmooth = on;
    });
}
void Renderer::setFrameLatency(uint32_t n) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_LATENCY;
        p.latency = n < 1 ? 1 : (n > 4 ? 4 : n);
    });
}
void Renderer::setUpscale(uint32_t m, uint32_t pr, float sh) {
    m_impl->modelSrRequested.store(m > 0 && m <= 4, std::memory_order_release);
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_UPSCALE;
        p.upMode = m > 4 ? 0 : m;
        p.upPreset = pr;
        p.upSharp = sh < 0 ? 0 : (sh > 1 ? 1 : sh);
    });
}
void Renderer::setNeuralRendering(bool on) {
    m_impl->modelNeuralRequested.store(on, std::memory_order_release);
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_NEURAL;
        p.neural = on;
    });
}
void Renderer::setNeuralResolution(bool renderResolution) {
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_NEURAL_RES;
        p.neuralRender = renderResolution;
    });
}
bool Renderer::neuralRenderingAvailable(std::string& status) const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    return m_impl->backend->neuralRenderingAvailable(status);
}
std::string Renderer::neuralRenderingStatus() const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    return m_impl->backend->neuralRenderingStatus();
}
bool Renderer::upscaleAvailable(std::string& status) const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    return m_impl->backend->upscaleAvailable(status);
}
std::string Renderer::upscaleStatus() const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    return m_impl->backend->upscaleStatus();
}
double Renderer::upscaleGpuMs() const {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    return m_impl->backend->upscaleGpuMs();
}
void Renderer::noteVuUpload(uint32_t a, uint32_t n, const float* rows) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->flushBatch(); // draws submitted before this upload belong to the previous scene camera
    m_impl->backend->noteVuUpload(a, n, rows);
}
void Renderer::noteSceneCamera(const SceneGuideCamera& camera) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->flushBatch();
    m_impl->backend->noteSceneCamera(camera);
}
void Renderer::noteMovieFrame(uint64_t signature, uint32_t width, uint32_t height, uint32_t movie, uint32_t frame) {
    Impl& s = *m_impl;
    std::lock_guard<std::mutex> lk(s.movieMu);
    if (s.movieRegs.empty())
        s.movieRegs.resize(64); // the IPU decodes a few frames ahead of the GS (6 slot ring in the game's player)
    s.movieRegs[s.movieRegNext] = {signature, width, height, movie, frame};
    s.movieRegNext = (s.movieRegNext + 1) % s.movieRegs.size();
    s.movieRegCount.fetch_add(1, std::memory_order_release);
}
void Renderer::setMovieFrameSource(MovieFrameSource source) {
    std::lock_guard<std::mutex> lk(m_impl->movieMu);
    m_impl->movieSource = std::move(source);
}
void Renderer::noteSceneModels(std::shared_ptr<const SceneModels> models) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    m_impl->flushBatch();
    m_impl->backend->noteSceneModels(std::move(models));
}
void Renderer::noteObjectMotion(const ObjectMotionScope& scope) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    m_impl->flushBatch();
    m_impl->backend->noteObjectMotion(scope);
}
void Renderer::upscaleCut() {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->flushBatch();
    m_impl->backend->upscaleCut();
}
void Renderer::noteSceneGeometry(const SceneGeometry& geometry) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    m_impl->flushBatch();
    m_impl->backend->noteSceneGeometry(geometry);
}
uint32_t Renderer::internalScale() const {
    return m_impl->backend->internalScale();
}

void Renderer::getViewport(uint32_t& w, uint32_t& h) const {
    m_impl->backend->getViewport(w, h);
}

void Renderer::setOverlayCallback(std::function<void(const OverlayContext&)> cb) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->backend->setOverlayCallback(std::move(cb));
}

uint32_t Renderer::consumeLocalToHostBytes(uint8_t* dst, uint32_t maxBytes) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    Impl& s = *m_impl;
    const size_t avail = s.l2h.size() - s.l2hPos;
    const uint32_t n = (uint32_t)std::min<size_t>(avail, maxBytes);
    if (s.xferLog)
        std::fprintf(stderr, "[l2h v%llu] consume request %u bytes, %zu staged -> %u\n",
                     (unsigned long long)s.vsyncCount, maxBytes, avail, n);
    if (dst && n)
        std::memcpy(dst, s.l2h.data() + s.l2hPos, n);
    s.l2hPos += n;
    if (s.l2hPos >= s.l2h.size()) {
        s.l2h.clear();
        s.l2hPos = 0;
    }
    return n;
}

uint8_t* Renderer::vram() {
    return m_impl->mem.data();
}

void Renderer::flushToMemory() {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->flushBatch();
    m_impl->backend->flushAll();
}

void Renderer::vramTouched(uint32_t firstBlock, uint32_t blockCount) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->mem.touchBlocks(firstBlock, blockCount);
}

uint32_t Renderer::readVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    flushToMemory();
    return m_impl->mem.read(psm, base, bw, x, y);
}

void Renderer::writeVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    flushToMemory();
    m_impl->mem.write(psm, base, bw, x, y, value);
    m_impl->mem.touchRect(psm, base, bw, x, y, 1, 1);
}

bool Renderer::captureDisplay(Image& out) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->applyPending();
    m_impl->flushBatch();
    return m_impl->backend->capture(m_impl->buildDisplay(), out);
}

bool Renderer::captureLastGameplay(Image& out, float& aspect) {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    return m_impl->backend && m_impl->backend->captureLastGameplay(out, aspect);
}
bool Renderer::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t width, uint32_t height,
                                  Image& out) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->flushBatch();
    return m_impl->backend->captureFramebuffer(fbp, fbw, psm, width, height, out);
}

void Renderer::resizeWindow(uint32_t w, uint32_t h) {
    std::lock_guard<std::recursive_mutex> lk(m_impl->mtx);
    m_impl->backend->resize(w, h);
}

} // namespace gsr

namespace gsr {
bool Renderer::waterRenderingRequested() const {
    return m_impl->waterRequested.load(std::memory_order_acquire);
}
bool Renderer::sceneModelsRequested() const {
    return waterRenderingRequested() || m_impl->modelNeuralRequested.load(std::memory_order_acquire) ||
           m_impl->modelSrRequested.load(std::memory_order_acquire);
}
void Renderer::setWaterRendering(bool water, bool reflections, bool shadows) {
    m_impl->waterRequested.store(water || shadows, std::memory_order_release);
    m_impl->queue([&](LiveSettings& p) {
        p.mask |= LiveSettings::F_WATER;
        p.modernWater = water;
        p.rayReflections = reflections;
        p.rayShadows = shadows;
    });
}
bool Renderer::waterRenderingAvailable(std::string& status) const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    return m_impl->backend->waterRenderingAvailable(status);
}
std::string Renderer::waterRenderingStatus() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    return m_impl->backend->waterRenderingStatus();
}
std::string Renderer::rayTracingStatus() const {
    std::lock_guard<std::recursive_mutex> lock(m_impl->mtx);
    return m_impl->backend->rayTracingStatus();
}
} // namespace gsr
