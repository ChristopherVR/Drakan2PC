// Drakan: tap on VIF1 UNPACKs into the VU1 constant block (rows 0..6: view-projection matrix, viewport offset / scale).
// Used by the temporal upscaler (DLSS) to know the camera of the 3D scene; see docs/reference/dlss.md. Set to nullptr
// when unused (the default): one pointer test per UNPACK.
#pragma once
#include <cstdint>
#include <atomic>

namespace drakan_vu1 {
// vuAddr: first VU data qword written, qwCount: qwords written, data: the VU1 data memory (16 bytes per qword, index 0
// = row 0). Called on the VU1 / VIF worker thread.
using UploadHook = void (*)(uint32_t vuAddr, uint32_t qwCount, const uint8_t* vu1Data);
extern std::atomic<UploadHook> g_uploadHook;
} // namespace drakan_vu1
