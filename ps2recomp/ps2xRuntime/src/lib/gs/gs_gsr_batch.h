#pragma once
// GIF packets that one thread submits to the D3D11 renderer bridge inside a Scope are staged and queued together when
// the outermost scope ends (or earlier, before any other record of that thread or a wait for the renderer), instead of
// taking the bridge queue lock once per packet. Used by the VU1 worker around each VIF1 job.
namespace gsr_batch {
// gs_gsr_bridge.cpp defines GSR_BATCH_DEFINE: it implements these even where PS2X_HAVE_GSR is not set (tests).
#if (defined(PS2X_HAVE_GSR) && PS2X_HAVE_GSR) || defined(GSR_BATCH_DEFINE)
void begin();
void end();
#else
inline void begin() {}
inline void end() {}
#endif
struct Scope {
    Scope() { begin(); }
    ~Scope() { end(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace gsr_batch
