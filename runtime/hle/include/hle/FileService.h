// SID 0x1201 "File Command"
#pragma once
#include "IsoFs.h"
#include "Srsc.h"
#include "Interfaces.h"
#include <mutex>
#include <string>
#include <vector>
#include <functional>

namespace hle {

class FileService {
public:
    FileService(IsoFs& fs, IEeMemory& mem, int maxHandles = 64) : fs_(fs), mem_(mem), files_(maxHandles) {}

    // Returns reply size written to `reply` (<= replyCap), or <0 on malformed call.
    int handle(u32 fno, const void* send, u32 sendSize, void* reply, u32 replyCap);

    struct Open {
        bool used = false;
        IsoFs::Node node;
        u64 pos = 0;
        Srsc srsc;
    };
    const IsoFs& fs() const { return fs_; }
    // Copy of an open-file record (for ResourceService). False if bad handle.
    bool get(s32 handle, Open& out) const;

    // Test aid (DRAKAN_START_LEVEL): every open of a path containing `from` (case-insensitive; a level name such as
    // "Level 1A - Surdana", which is both the folder and the file stem) opens the same path with `to` instead, so the
    // game loads another level under the original name. Empty `from` or `to` disables it.
    void setLevelRedirect(std::string from, std::string to);
    // Per-open view of an existing file (e.g. a campaign-free arena LVL). Never mutates the merged disc or another
    // handle's bytes. Runs under the file-service lock; use IsoFs directly, not FileService, inside the callback.
    using OpenFilter = std::function<bool(const IsoFs&, IsoFs::Node&, std::string&)>;
    void setOpenFilter(OpenFilter filter);
    // Per-read view of an open file: called after every plain read with the bytes just read (`data`, `n`, starting at
    // file offset `offset`) and may rewrite them in place. Evaluated at read time (not open time), so it can depend on
    // state that changes after the file was opened, e.g. the game chosen on the title. Runs under the file-service lock.
    using ReadOverlay = std::function<void(const IsoFs&, const IsoFs::Node&, u64 offset, u8* data, size_t n)>;
    void setReadOverlay(ReadOverlay overlay);
    // `path` with the redirect applied (unchanged when it does not match).
    std::string redirectPath(const std::string& path) const;

    // Error codes. -7 (no free handle) and -4 (bad handle) are documented; "not found" is a guess (TODO).
    static constexpr s32 ERR_NOT_FOUND = -1; // measured: -2 makes the game retry the open forever, -1 lets it continue
    static constexpr s32 ERR_BAD_HANDLE = -4;
    static constexpr s32 ERR_NO_HANDLE = -7;

private:
    IsoFs& fs_;
    IEeMemory& mem_;
    mutable std::mutex mu_;
    std::vector<Open> files_;
    std::string redirectFrom_, redirectTo_;
    OpenFilter openFilter_;
    ReadOverlay readOverlay_;
};

} // namespace hle
