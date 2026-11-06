"""Exercise affinity with tiny PowerShell subprocesses, never the compiler/game build."""

import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH and sys.platform == "win32", "Windows PowerShell 7 affinity tests")
class BuildAffinityTests(unittest.TestCase):
    def run_script(self, body):
        helpers = "; ".join(
            ". '{}'".format(str(ROOT / "scripts/buildtool" / name).replace("'", "''"))
            for name in ("Build-Affinity.ps1", "Build-Policy.ps1")
        )
        result = subprocess.run(
            [PWSH, "-NoProfile", "-Command", f"$ErrorActionPreference = 'Stop'; {helpers}; {body}"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_browser_mask_rejected_before_inputs_or_wsl(self):
        builder = str(ROOT / "scripts/buildtool/Build-LocalGame.ps1").replace("'", "''")
        self.run_script(
            f"""
            $before = [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity
            $rejected = $false
            try {{ & '{builder}' -IsoPath 'missing-affinity-test.iso' -TargetPlatform web -CpuAffinity 0x1 }}
            catch {{
                if ($_.Exception.Message -notlike '*native Windows builds only*') {{ throw }}
                $rejected = $true
            }}
            if (-not $rejected) {{ throw 'Browser mask was accepted' }}
            if ([Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity -ne $before) {{
                throw 'Browser validation changed affinity'
            }}
            """
        )

    def test_validation_and_default_leave_process_unchanged(self):
        self.run_script(
            """
            $p = [Diagnostics.Process]::GetCurrentProcess()
            $before = $p.ProcessorAffinity
            foreach ($bad in @('0', '0x0', '-1', 'xyz', '0x', '10000000000000000', '0x1;exit')) {
                $rejected = $false
                try { Resolve-BuildAffinity $bad | Out-Null } catch { $rejected = $true }
                if (-not $rejected) { throw "Accepted $bad" }
            }
            $available = [BitConverter]::ToUInt64([BitConverter]::GetBytes($before.ToInt64()), 0)
            $missing = [uint64]::MaxValue -bxor $available
            if ($missing) {
                $rejected = $false
                try { Resolve-BuildAffinity ('0x{0:X}' -f $missing) | Out-Null } catch { $rejected = $true }
                if (-not $rejected) { throw 'Accepted unavailable processors' }
            }
            $state = Enter-BuildAffinity ''
            if ($null -ne $state) { throw 'Default changed affinity' }
            Exit-BuildAffinity $state
            if ($p.ProcessorAffinity -ne $before) { throw 'Validation changed affinity' }
            """
        )

    def test_descendant_inheritance_and_failure_restores_caller(self):
        self.run_script(
            """
            $p = [Diagnostics.Process]::GetCurrentProcess()
            $before = $p.ProcessorAffinity
            $env:MSBUILDDISABLENODEREUSE = 'original'
            $bits = [BitConverter]::ToUInt64([BitConverter]::GetBytes($before.ToInt64()), 0)
            $bit = [uint64]1
            while (($bits -band $bit) -eq 0) { $bit = $bit -shl 1 }
            $state = Enter-BuildAffinity ('0x{0:X}' -f $bit)
            try {
                if ($env:MSBUILDDISABLENODEREUSE -ne '1') { throw 'Node reuse enabled' }
                $child = & pwsh -NoProfile -Command '& pwsh -NoProfile -Command "[Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity.ToInt64()"'
                if ($LASTEXITCODE -ne 0 -or [int64]$child -ne [int64]$bit) { throw 'Grandchild did not inherit mask' }
                throw 'Synthetic compiler failure'
            } catch {
                if ($_.Exception.Message -ne 'Synthetic compiler failure') { throw }
            } finally { Exit-BuildAffinity $state }
            if ($p.ProcessorAffinity -ne $before) { throw 'Caller affinity not restored' }
            if ($env:MSBUILDDISABLENODEREUSE -ne 'original') { throw 'Node reuse not restored' }
            """
        )

    def test_mask_count_sizes_the_compilers(self):
        self.run_script(
            """
            $p = [Diagnostics.Process]::GetCurrentProcess()
            $available = [BitConverter]::ToUInt64([BitConverter]::GetBytes($p.ProcessorAffinity.ToInt64()), 0)
            $bits = 0; $v = $available; while ($v) { $bits += [int]($v -band 1); $v = $v -shr 1 }
            if ((Get-BuildAffinityCount ('0x{0:X}' -f $available) 64) -ne $bits) { throw 'Full mask miscounted' }
            if ((Get-BuildAffinityCount '' 24) -ne 24) { throw 'No mask must count every core' }
            $low = [uint64]1; while (($available -band $low) -eq 0) { $low = $low -shl 1 }
            if ((Get-BuildAffinityCount ('0x{0:X}' -f $low) 24) -ne 1) { throw 'One-bit mask miscounted' }

            function Check($cores, $logical, $memory, $usable, $jobs, $processes) {
                $s = Get-BuildParallelism -Cores $cores -LogicalCores $logical -MemoryGb $memory -UsableCores $usable
                if ($s.Jobs -ne $jobs -or $s.CompilerProcesses -ne $processes) {
                    throw "cores=$cores logical=$logical memory=$memory usable=$usable -> $($s.Jobs) x $($s.CompilerProcesses)"
                }
            }
            # Issue 1: 24 logical cores, 63 GB, mask 0xFF (8): 2 x 4 fills the mask's 8 processors, no more.
            Check 0 24 63 8 2 4
            # A 4-processor mask on the same PC must not start 2 x 4 compilers.
            Check 0 24 63 4 2 2
            Check 0 24 63 2 2 1
            Check 0 24 63 1 1 1
            # An explicit core count is capped at the mask.
            Check 12 24 63 8 2 4
            Check 12 24 63 24 2 6
            # No mask: unchanged automatic choice on a large PC; memory still limits small ones.
            Check 0 24 63 24 2 4
            Check 0 16 16 16 1 4
            Check 0 16 8 16 1 4
            Check 0 16 4 16 1 2
            Check 8 16 16 16 1 8
            """
        )


if __name__ == "__main__":
    unittest.main()
