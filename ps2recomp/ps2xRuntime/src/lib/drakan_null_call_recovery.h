// Recovery for a known call through a null vtable slot (docs/journal/2026-11-07/surdana-null-jalr.md).
//
// 0x10a980 (the controller's resource-handle update, run for every object of the scene walk) asks the resource cache
// 0x12b640 for the object behind a handle, with the "must exist, do not create" flag. A handle whose resource is not
// resident (after a far teleport the target area has not streamed in) gets null back, and 0x10a980 does not check: it
// reads the object's type pointer at null + 0x18 and calls slot +0x64 of it (JALR at 0x10aa6c, target 0). The code that
// follows the call (0x10aa74) clears the handle field (+0x50 or +0x5c) and leaves, which is exactly what is wanted for
// a resource that is not there, so the call is skipped instead of ending the guest thread.
// Pure logic, no guest memory.
#pragma once

#include <cstdint>

namespace drakan_null_call_recovery {

constexpr uint32_t kHandleUpdateCallSite = 0x10aa6cu; // PAL address of the JALR in 0x10a980

// `palSource` is the branch source already converted back to PAL addresses (identity builds only).
inline bool shouldSkip(bool isIndirectCall, uint32_t target, uint32_t palSource) {
    return isIndirectCall && target == 0u && palSource == kHandleUpdateCallSite;
}

} // namespace drakan_null_call_recovery
