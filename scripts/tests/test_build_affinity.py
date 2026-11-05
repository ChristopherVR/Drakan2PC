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
        helper = str(ROOT / "scripts/buildtool/Build-Affinity.ps1").replace("'", "''")
        result = subprocess.run(
            [PWSH, "-NoProfile", "-Command", f"$ErrorActionPreference = 'Stop'; . '{helper}'; {body}"],
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


if __name__ == "__main__":
    unittest.main()
