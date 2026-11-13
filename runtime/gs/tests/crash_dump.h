#pragma once
// A minidump when a test or replay process dies of an unhandled exception (an access violation and the like), so a
// crash that happens once under load still leaves a stack. Nothing is written unless the process crashes. The file is
// <dir>/<name>-<pid>.dmp with <dir> = GSR_CRASH_DUMP_DIR, or the current directory when that is unset;
// GSR_CRASH_DUMP=0 turns it off. Windows only (elsewhere a no-op). A fast-fail abort (0xC0000409, what wgpu-native
// ends in after a lost device) does not reach an exception filter and leaves no dump.
//
// Open the dump with `cdb -z <file> -y <folder of the .pdb>` (or Visual Studio), then `.ecxr; kn`.

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

namespace crash_dump {

#if defined(_WIN32)
namespace detail {
inline char g_path[MAX_PATH * 2] = {};

inline LONG WINAPI writeDump(EXCEPTION_POINTERS* info) {
    // MINIDUMP_EXCEPTION_INFORMATION (packed to 4 bytes, as dbghelp.h declares it), here so that dbghelp.h is not
    // needed
#pragma pack(push, 4)
    struct ExceptionInfo {
        DWORD threadId;
        EXCEPTION_POINTERS* pointers;
        BOOL clientPointers;
    };
#pragma pack(pop)
    ExceptionInfo exception{GetCurrentThreadId(), info, FALSE};
    using WriteFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    const auto write = dbghelp ? reinterpret_cast<WriteFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    HANDLE file = CreateFileA(g_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL ok = FALSE;
    if (write && file != INVALID_HANDLE_VALUE) {
        // MiniDumpWithIndirectlyReferencedMemory (0x40) | MiniDumpWithThreadInfo (0x1000): stacks, the memory they
        // point at and every thread's state, a few MB.
        ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, 0x40 | 0x1000, &exception, nullptr, nullptr);
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    std::fprintf(stderr, "[crash] exception 0x%08lx; %s %s\n",
                 info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0ul,
                 ok ? "minidump written to" : "no minidump could be written to", g_path);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace detail

// Call once at the start of main().
inline void install(const char* name) {
    if (const char* off = std::getenv("GSR_CRASH_DUMP"); off && off[0] == '0')
        return;
    const char* dir = std::getenv("GSR_CRASH_DUMP_DIR");
    std::snprintf(detail::g_path, sizeof(detail::g_path), "%s%s%s-%lu.dmp", dir && *dir ? dir : "",
                  dir && *dir ? "\\" : "", name, GetCurrentProcessId());
    SetUnhandledExceptionFilter(detail::writeDump);
}
#else
inline void install(const char*) {}
#endif

} // namespace crash_dump
