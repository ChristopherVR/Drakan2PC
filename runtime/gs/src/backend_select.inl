// gsr::Backend: forwards to the backend chosen at construction. Included by backend_select_*.cpp, which say which
// backends the binary has (GSR_HAVE_D3D11_BACKEND / GSR_HAVE_WEBGPU_BACKEND).
#include "gs_internal.h"

#include <cstdio>

namespace gsr {

#if GSR_HAVE_D3D11_BACKEND
std::unique_ptr<BackendApi> createD3D11Backend(LocalMemory& mem, const Config& cfg, Stats& stats);
bool d3d11WaitPresentHandle(void* wait, uint32_t timeoutMs);
#endif
#if GSR_HAVE_WEBGPU_BACKEND
std::unique_ptr<BackendApi> createWgpuBackend(LocalMemory& mem, const Config& cfg, Stats& stats);
#endif

#if !defined(GSR_BACKEND_FORWARDERS_ONLY) // tests that bring their own backend define it
Backend::Backend(LocalMemory& mem, const Config& cfg, Stats& stats) {
    Config::Api want = cfg.api;
#if GSR_HAVE_WEBGPU_BACKEND
    if (want == Config::Api::Auto)
        want = Config::Api::WebGpu;
#else
    if (want == Config::Api::Auto || want == Config::Api::WebGpu)
        want = Config::Api::D3D11;
#endif
#if !GSR_HAVE_D3D11_BACKEND
    want = Config::Api::WebGpu;
#endif
#if GSR_HAVE_D3D11_BACKEND
    if (want == Config::Api::D3D11)
        m_api = createD3D11Backend(mem, cfg, stats);
#endif
#if GSR_HAVE_WEBGPU_BACKEND
    if (want == Config::Api::WebGpu)
        m_api = createWgpuBackend(mem, cfg, stats);
#endif
    m_kind = want;
}
Backend::~Backend() = default;

bool Backend::waitPresentHandle(void* wait, uint32_t timeoutMs) {
#if GSR_HAVE_D3D11_BACKEND
    return d3d11WaitPresentHandle(wait, timeoutMs); // only D3D11 hands out handles (WebGPU returns null)
#else
    (void)wait, (void)timeoutMs;
    return true;
#endif
}
#endif

bool Backend::init() {
    return m_api->init();
}
bool Backend::valid() const {
    return m_api->valid();
}
std::string Backend::adapterName() const {
    return m_api->adapterName();
}
void Backend::draw(const GsDrawState& st, const ClutBuf& clut, const DrawVertex* v, uint32_t count) {
    m_api->draw(st, clut, v, count);
}
void Backend::flushToMemory(uint32_t firstBlock, uint32_t lastBlock) {
    m_api->flushToMemory(firstBlock, lastBlock);
}
bool Backend::beginTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return m_api->beginTargetUpload(psm, bp, bw, x, y, w, h);
}
void Backend::completeTargetUpload(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t w, uint32_t h, bool apply) {
    m_api->completeTargetUpload(psm, bp, bw, x, y, w, h, apply);
}
void Backend::flushAll() {
    m_api->flushAll();
}
void Backend::drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const MovieImage& image, uint64_t imageId) {
    m_api->drawMovieFrame(dbp, dbw, x, y, w, h, image, imageId);
}
void Backend::present(const DisplayState& d) {
    m_api->present(d);
}
void Backend::latchDisplay(const DisplayState& d, uint32_t field) {
    m_api->latchDisplay(d, field);
}
void Backend::presentLatched() {
    m_api->presentLatched();
}
void* Backend::beginPresentWait() {
    return m_api->beginPresentWait();
}
void Backend::endPresentWait(bool ready) {
    m_api->endPresentWait(ready);
}
void Backend::setVsync(bool on) {
    m_api->setVsync(on);
}
bool Backend::vsyncEnabled() const {
    return m_api->vsyncEnabled();
}
void Backend::applyLive(const LiveSettings& ls) {
    m_api->applyLive(ls);
}
uint32_t Backend::internalScale() const {
    return m_api->internalScale();
}
void Backend::getViewport(uint32_t& w, uint32_t& h) const {
    m_api->getViewport(w, h);
}
void Backend::setOverlayCallback(std::function<void(const OverlayContext&)> cb) {
    m_api->setOverlayCallback(cb);
}
bool Backend::capture(const DisplayState& d, Image& out) {
    return m_api->capture(d, out);
}
bool Backend::captureLastGameplay(Image& out, float& aspect) {
    return m_api->captureLastGameplay(out, aspect);
}
bool Backend::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) {
    return m_api->captureFramebuffer(fbp, fbw, psm, w, h, out);
}
void Backend::resize(uint32_t w, uint32_t h) {
    m_api->resize(w, h);
}
void Backend::reset() {
    m_api->reset();
}
void Backend::endFrame() {
    m_api->endFrame();
}
void Backend::noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* vuRows0to6) {
    m_api->noteVuUpload(vuAddr, qwCount, vuRows0to6);
}
void Backend::noteSceneCamera(const SceneGuideCamera& camera) {
    m_api->noteSceneCamera(camera);
}
void Backend::noteObjectMotion(const ObjectMotionScope& scope) {
    m_api->noteObjectMotion(scope);
}
void Backend::noteSceneGeometry(const SceneGeometry& geometry) {
    m_api->noteSceneGeometry(geometry);
}
void Backend::noteSceneModels(std::shared_ptr<const SceneModels> models) {
    m_api->noteSceneModels(models);
}
std::string Backend::upscaleStatus() const {
    return m_api->upscaleStatus();
}
double Backend::upscaleGpuMs() const {
    return m_api->upscaleGpuMs();
}
void Backend::upscaleCut() {
    m_api->upscaleCut();
}
bool Backend::upscaleAvailable(std::string& status) const {
    return m_api->upscaleAvailable(status);
}
bool Backend::neuralRenderingAvailable(std::string& status) const {
    return m_api->neuralRenderingAvailable(status);
}
std::string Backend::neuralRenderingStatus() const {
    return m_api->neuralRenderingStatus();
}
bool Backend::waterRenderingAvailable(std::string& status) const {
    return m_api->waterRenderingAvailable(status);
}
std::string Backend::waterRenderingStatus() const {
    return m_api->waterRenderingStatus();
}
std::string Backend::rayTracingStatus() const {
    return m_api->rayTracingStatus();
}

} // namespace gsr
