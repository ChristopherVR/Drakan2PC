# A Windows job object that kills everything the build started when the builder ends, however it ends.
#
# The build runs cmake, MSBuild and a dozen cl.exe compilers as children of this PowerShell. Closing the window, pressing
# Ctrl+C or ending PowerShell in Task Manager used to leave them compiling in the background (the VU1 files alone run for
# minutes each). Enter-BuildJob puts this process in a job with "kill on job close"; Windows closes the job's last handle when
# the process dies, and then ends every process that was started inside it. Children (and MSBuild's reusable nodes) inherit
# the job, so the whole tree goes.
#
# Disable-BuildJobKill turns the kill off again; Build-Drakan.ps1 calls it before it starts the finished game
# (-LaunchWhenDone), which must outlive the builder window.
if ($env:OS -eq 'Windows_NT' -and -not ('DrakanBuildJob' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class DrakanBuildJob {
    [StructLayout(LayoutKind.Sequential)]
    struct BasicLimits {
        public long PerProcessUserTimeLimit, PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct IoCounters { public ulong A, B, C, D, E, F; }
    [StructLayout(LayoutKind.Sequential)]
    struct ExtendedLimits {
        public BasicLimits Basic;
        public IoCounters Io;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref ExtendedLimits info, int length);
    [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();

    const int ExtendedLimitInformation = 9;
    const uint KillOnJobClose = 0x2000;
    static IntPtr job = IntPtr.Zero;

    static bool Limit(uint flags) {
        var info = new ExtendedLimits();
        info.Basic.LimitFlags = flags;
        return SetInformationJobObject(job, ExtendedLimitInformation, ref info, Marshal.SizeOf(typeof(ExtendedLimits)));
    }

    // True when this process (and from now on its children) are in a kill-on-close job.
    public static bool Enter() {
        if (job != IntPtr.Zero) return true;
        IntPtr created = CreateJobObject(IntPtr.Zero, null);
        if (created == IntPtr.Zero) return false;
        job = created;
        if (!Limit(KillOnJobClose) || !AssignProcessToJobObject(job, GetCurrentProcess())) { job = IntPtr.Zero; return false; }
        return true;
    }

    // Stops the job from killing its processes when it closes (the handle stays open until this process ends).
    public static bool Release() { return job != IntPtr.Zero && Limit(0); }
}
'@
}

# Makes the whole build tree end with this process. Returns $false (and the build goes on) where the job cannot be made.
function Enter-BuildJob {
    if ($env:OS -ne 'Windows_NT') { return $false }
    $ok = [DrakanBuildJob]::Enter()
    if (-not $ok) { Write-Warning 'Could not tie the compilers to this window: if you close it, stop the compilers in Task Manager.' }
    return $ok
}

function Disable-BuildJobKill {
    if ($env:OS -eq 'Windows_NT') { [void][DrakanBuildJob]::Release() }
}
