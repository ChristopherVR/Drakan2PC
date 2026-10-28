// SRSC container header/index (see analysis/scripts/iop_srsc.py).
// Header 16 bytes: 'SRSC', u16 ver, u32 indexOffset, u32 count, u16 pad.
// Index at indexOffset: count * {u16 type,u16 id,u16 sub,u32 off,u32 size}.
#pragma once
#include "Interfaces.h"
#include <vector>

namespace hle {
struct SrscEntry {
    u16 type, id, sub;
    u32 off, size;
};
struct Srsc {
    bool valid = false;
    u32 indexOffset = 0, count = 0;
    std::vector<SrscEntry> entries;
    // (type,id,sub) match; nullptr if absent. sub == 0xFFFF (-1) means "any instance": first entry with that (type,id).
    const SrscEntry* find(u16 type, u16 id, u16 sub) const {
        for (auto& e : entries)
            if (e.type == type && e.id == id && (sub == 0xFFFF || e.sub == sub))
                return &e;
        return nullptr;
    }
};
} // namespace hle
