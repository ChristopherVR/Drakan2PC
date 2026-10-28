#pragma once
class PS2Runtime;
namespace drakan_crash {
// Install the unhandled-exception filter: on a host crash (access violation, illegal instruction, stack overflow, ...)
// it logs the exception, the faulting module+offset, the live guest context (pc, ra, sp, GPRs, thread id), the dispatch
// history of the crashing thread and the PS2X_TRACE_RING ring, flushes drakan.log and writes crash_<pid>.dmp
// (MiniDumpNormal) next to the exe. Env DRAKAN_CRASH_DUMP=0 skips the dump.
void install(PS2Runtime* rt);
// Fatal error path (std::terminate): same report as the exception filter, then ends the process with exit code 3 (no
// CRT dialog).
void fatal(const char* why);
} // namespace drakan_crash
