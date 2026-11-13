// A removed Direct3D 11 device: the result checks behind every call site whose failure would otherwise be used, and
// the forced-loss test hook. Which results count and how a loss is described are backend-neutral (device_loss.h);
// what follows a loss is shared too: BackendApi::lostNow closes the latch in gsr::Backend (backend_select.inl) at the
// next forwarded call, and the host ends the session in order (gsr/renderer_session.h, gs_gsr_bridge.cpp).
#include "d3d11_backend_impl.h"

namespace gsr {

bool D3D11Backend::Impl::deviceFailed(HRESULT hr, const char* site, bool simulated) const {
    const uint32_t result = (uint32_t)hr;
    // A failure that does not name a loss by itself (a failed Map, out of memory) is one only when the device says so;
    // for one that does, the device's reason is the better description. Only failures get here.
    const uint32_t removed = dev ? (uint32_t)dev->GetDeviceRemovedReason() : 0u;
    const device_loss::Cause cause = device_loss::afterFailure(result, removed);
    if (cause == device_loss::Cause::None)
        return false;
    std::lock_guard<std::mutex> lock(lossMutex);
    if (!lost->load(std::memory_order_relaxed)) {
        lossReason = device_loss::plainWords(cause);
        const uint32_t named = removed ? removed : result;
        const char* code = device_loss::codeName(named);
        std::fprintf(stderr,
                     "[gsr] Direct3D 11 device removed%s at %s (thread %lu): call result 0x%08x, removed reason "
                     "0x%08x%s%s (%s); no further call reaches the device\n",
                     simulated ? " (test: GSR_TEST_DEVICE_LOSS)" : "", site, (unsigned long)GetCurrentThreadId(),
                     result, removed, *code ? " " : "", code, lossReason.c_str());
        lost->store(true, std::memory_order_release);
    }
    return false;
}

void D3D11Backend::Impl::probeDevice(const char* site) const {
    if (gone() || !dev)
        return;
    const HRESULT removed = dev->GetDeviceRemovedReason();
    if (FAILED(removed))
        deviceFailed(removed, site);
}

HRESULT D3D11Backend::Impl::map(ID3D11Resource* resource, D3D11_MAP mode, D3D11_MAPPED_SUBRESOURCE& out,
                                const char* site, device_loss::Site test) {
    if (gone() || !resource)
        return E_FAIL;
    if (testLoss(test)) {
        deviceFailed((HRESULT)device_loss::kDeviceRemoved, site, true);
        return (HRESULT)device_loss::kDeviceRemoved;
    }
    const HRESULT hr = ctx->Map(resource, 0, mode, 0, &out);
    if (FAILED(hr))
        deviceFailed(hr, site);
    return hr;
}

bool D3D11Backend::Impl::testLoss(device_loss::Site site) const {
    if (site == device_loss::Site::Off || site != lossTest.site || !lossTestArmed.load(std::memory_order_relaxed))
        return false;
    return !lossTestFired.exchange(true);
}

void D3D11Backend::Impl::testLossFrameEnd() {
    if (lossTest.site == device_loss::Site::Off || lossTestFired.load(std::memory_order_relaxed))
        return;
    // `frame` is the frame that is ending: the hook is armed from frame N on, and the frame site fires at its end.
    if ((long long)frame + 1 >= lossTest.frame)
        lossTestArmed.store(true, std::memory_order_relaxed);
    if (lossTest.site == device_loss::Site::Frame && (long long)frame >= lossTest.frame && testLoss(lossTest.site))
        deviceFailed((HRESULT)device_loss::kDeviceRemoved, "frame end", true);
}

bool D3D11Backend::deviceLost(std::string* reason) const {
    if (!lostNow.load(std::memory_order_acquire))
        return false;
    if (reason) {
        std::lock_guard<std::mutex> lock(m_impl->lossMutex);
        *reason = m_impl->lossReason;
    }
    return true;
}

} // namespace gsr
