// Abstract interfaces between the RSRCIOP HLE library and its host runtime.
// The library knows nothing about PS2Recomp; the host implements these.
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>

namespace hle {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s16 = int16_t;
using s32 = int32_t;

// Server IDs
constexpr u32 SID_FILE = 0x1201;      // IOP server: File Command
constexpr u32 SID_RESOURCE = 0x1202;  // IOP server: Resource Request
constexpr u32 SID_SOUND = 0x1203;     // IOP server: Sound Command
constexpr u32 SID_EE_STREAM = 0x1301; // EE server: per-request data/notification
constexpr u32 SID_EE_DONE = 0x1302;   // EE server: batched completions
constexpr u32 SID_EE_ALLOC = 0x1303;  // EE server: destination buffer allocation

// Guest (EE) memory access. Addresses are passed exactly as the game supplied them
// (they may carry the 0x20000000 / 0x30000000 uncached aliases; the host may mask
// with 0x1FFFFFFF). Return false on an invalid range.
struct IEeMemory {
    virtual ~IEeMemory() = default;
    virtual bool read(u32 eeAddr, void* dst, size_t n) = 0;
    virtual bool write(u32 eeAddr, const void* src, size_t n) = 0;
};

// Posts an RPC *to a server registered on the EE side* (the IOP is the client).
// The host must (a) copy payload into guest RAM, (b) queue the call on that server's
// sceSifRpcLoop thread, (c) if blocking, wait until the guest handler returned and copy
// its return buffer (up to replyCap bytes) into `reply`. Returns reply byte count (>=0) or <0 on
// failure (e.g. server not registered yet).
struct IEeRpcPoster {
    virtual ~IEeRpcPoster() = default;
    virtual int post(u32 sid, u32 fno, const void* payload, u32 size, void* reply, u32 replyCap, bool blocking) = 0;
};

// SPU2 sink. The library never emulates SPU2; it only forwards. All methods are called
// from the HLE service thread.
struct ISpu2 {
    virtual ~ISpu2() = default;
    // SPU2 local RAM (byte addresses, 2 MiB)
    virtual void ramWrite(u32 spuAddr, const void* src, u32 n) = 0;
    virtual void ramRead(u32 spuAddr, void* dst, u32 n) = 0;
    // libsd calls issued through sceSdProcBatch (func 1..4, 0x11..0x14)
    virtual void setParam(u16 entry, u16 value) = 0;
    virtual u16 getParam(u16 entry) = 0;
    virtual void setSwitch(u16 entry, u32 value) = 0;
    virtual u32 getSwitch(u16 entry) = 0;
    virtual void setAddr(u16 entry, u32 value) = 0;
    virtual u32 getAddr(u16 entry) = 0;
    virtual void setCoreAttr(u16 entry, u16 value) = 0;
    virtual u16 getCoreAttr(u16 entry) = 0;
};

// Optional logging (default sink: stderr)
using LogSink = std::function<void(const std::string&)>;
void setLogSink(LogSink sink);
void logf(const char* fmt, ...);
void setVerbose(bool v);
bool verbose();

} // namespace hle
