#include "ps2_syscalls.h"
#include "ps2_log.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include "ps2x/iop/ps2_path.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <ThreadNaming.h>

std::string translatePs2Path(const char* ps2Path);

#include "Helpers/Path.h"
#include "Helpers/State.h"
#include "Helpers/Loader.h"
#include "Helpers/Runtime.h"

// A guest struct the kernel call was handed a pointer to: null unless the pointer is aligned and the whole struct lies
// in RAM or the scratchpad without wrapping (resolveDirectGuestSpan, runtime/ps2_memory.h).
template <typename T>
inline const T* getEeGuestStruct(const uint8_t* rdram, uint32_t address) {
    if ((address & (alignof(T) - 1u)) != 0u) {
        return nullptr;
    }
    return reinterpret_cast<const T*>(resolveDirectGuestSpan(rdram, address, sizeof(T)));
}

template <typename T>
inline T* getEeGuestStruct(uint8_t* rdram, uint32_t address) {
    return const_cast<T*>(getEeGuestStruct<T>(static_cast<const uint8_t*>(rdram), address));
}
