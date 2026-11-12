// GSR_GPU_PASS_TIMING=1: Direct3D 11 timestamp queries at the pass boundaries of every presented frame, read a few
// frames later without waiting, printed as "[gpu-pass] ..." every 120 frames. The arithmetic and the meaning of the
// numbers are in gpu_pass_stats.h. Off: no query is created and every call returns at once.
#pragma once
#include "gpu_pass_stats.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

namespace gsr {

class GpuPassTimer {
public:
    void enable(ID3D11Device* device, ID3D11DeviceContext* context, bool raw = false) {
        m_raw = raw;
        m_device = device;
        m_context = context;
        m_on = device && context;
    }
    bool on() const { return m_on; }
    gpupass::Pass current() const { return m_current; }

    // From here on the GPU works for `pass`. worldBegin: the engine opens a world here.
    void mark(gpupass::Pass pass, bool worldBegin = false) {
        m_current = pass;
        if (!m_on)
            return;
        Slot& slot = m_ring[m_slot];
        if (!slot.open && !open(slot))
            return;
        if (slot.used >= kMaxMarks) { // keep the last boundary's pass: the tail of the frame goes to it
            ++m_dropped;
            return;
        }
        if (slot.stamps.size() <= slot.used) {
            Microsoft::WRL::ComPtr<ID3D11Query> query;
            const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
            if (FAILED(m_device->CreateQuery(&desc, &query)))
                return;
            slot.stamps.push_back(std::move(query));
            slot.passes.push_back(pass);
            slot.begins.push_back(false);
        }
        slot.passes[slot.used] = pass;
        slot.begins[slot.used] = worldBegin;
        m_context->End(slot.stamps[slot.used].Get());
        ++slot.used;
    }

    // Call right before the swap chain's Present: ends this frame, starts the next one in the pass that was open.
    void frameBoundary() {
        if (!m_on)
            return;
        Slot& slot = m_ring[m_slot];
        if (slot.open) {
            if (!slot.end) {
                const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
                m_device->CreateQuery(&desc, &slot.end);
            }
            if (slot.end)
                m_context->End(slot.end.Get());
            m_context->End(slot.disjoint.Get());
            slot.open = false;
            slot.pending = slot.end && slot.used;
            m_slot = (m_slot + 1) % kRing;
        }
        collect();
        mark(m_current); // a world or its upscale can straddle a present: the pass goes on
    }

    // Marks `pass` now and the pass that was current before when it goes out of scope.
    class Scope {
    public:
        Scope(GpuPassTimer& timer, gpupass::Pass pass) : m_timer(timer), m_previous(timer.current()) {
            m_timer.mark(pass);
        }
        ~Scope() { m_timer.mark(m_previous); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        GpuPassTimer& m_timer;
        gpupass::Pass m_previous;
    };

private:
    static constexpr size_t kRing = 8, kMaxMarks = 96, kWindow = 120;
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, end;
        std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> stamps;
        std::vector<gpupass::Pass> passes;
        std::vector<bool> begins;
        size_t used = 0;
        bool open = false, pending = false;
    };

    bool open(Slot& slot) {
        if (slot.pending) { // the GPU is more than a ring behind: this frame is not measured
            ++m_dropped;
            return false;
        }
        if (!slot.disjoint) {
            const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            if (FAILED(m_device->CreateQuery(&desc, &slot.disjoint)))
                return false;
        }
        m_context->Begin(slot.disjoint.Get());
        slot.used = 0;
        slot.open = true;
        return true;
    }

    void collect() {
        for (Slot& slot : m_ring) {
            if (!slot.pending)
                continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            UINT64 end = 0;
            if (m_context->GetData(slot.disjoint.Get(), &disjoint, sizeof disjoint, D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
                    S_OK ||
                m_context->GetData(slot.end.Get(), &end, sizeof end, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                continue;
            m_marks.resize(slot.used);
            bool complete = true;
            for (size_t i = 0; i < slot.used && complete; ++i) {
                UINT64 ticks = 0;
                complete = m_context->GetData(slot.stamps[i].Get(), &ticks, sizeof ticks,
                                              D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
                m_marks[i] = {slot.passes[i], ticks, slot.begins[i]};
            }
            if (!complete)
                continue;
            slot.pending = false;
            gpupass::Frame frame;
            if (disjoint.Disjoint ||
                !gpupass::attribute(m_marks.data(), m_marks.size(), end, disjoint.Frequency, frame)) {
                ++m_dropped;
                continue;
            }
            m_window.add(frame);
            if (m_raw && m_window.size() <= 4) { // GSR_GPU_PASS_TIMING=2: the marks of the first frames of a window
                std::string line;
                char text[48];
                for (size_t i = 0; i < m_marks.size(); ++i) {
                    const uint64_t next = i + 1 < m_marks.size() ? m_marks[i + 1].ticks : end;
                    std::snprintf(text, sizeof text, " %s%s:%.3f", m_marks[i].worldBegin ? "*" : "",
                                  gpupass::name(m_marks[i].pass),
                                  1000.0 * double(next - m_marks[i].ticks) / double(disjoint.Frequency));
                    line += text;
                }
                std::fprintf(stderr, "[gpu-pass-raw]%s\n", line.c_str());
            }
        }
        if (m_window.size() >= kWindow) {
            std::fprintf(stderr, "[gpu-pass] %s dropped=%llu\n", m_window.report().c_str(),
                         static_cast<unsigned long long>(m_dropped));
            m_window.clear();
            m_dropped = 0;
        }
    }

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    bool m_on = false, m_raw = false;
    gpupass::Pass m_current = gpupass::Pass::Frame;
    Slot m_ring[kRing];
    size_t m_slot = 0;
    std::vector<gpupass::Mark> m_marks;
    gpupass::Window m_window;
    uint64_t m_dropped = 0;
};

} // namespace gsr
