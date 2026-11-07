import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "ray_gpu_summary", Path(__file__).parents[1] / "perf" / "ray_gpu_summary.py"
)
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)

WEBGPU = (
    "[ray-gpu] backend=webgpu pass={n} sky=0.030ms low={low}ms transport={transport}ms main=2.000ms "
    "total={total}ms size=2560x1440 low_size=640x360 transport_size=640x360 instances=900 frame_chunks=3 "
    "frame_triangles=20000 static_triangles=180000 nodes=400000 upload_bytes=1000000 "
    "cpu: build=1.2 models=0.4 top=0.3 upload=0.6"
)
DXR = (
    "[ray-gpu] pass=7 size=2560x1440 effects=3 build=0.5000ms sky=0.0100ms transport=1.0000ms compose=0.7000ms "
    "total=2.2100ms upload_bytes=10 dynamic_bytes=5 dynamic_total=5 rejected_frame_triangles=0"
)


class RayGpuSummaryTests(unittest.TestCase):
    def log(self, lines):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "stdout.log"
        path.write_text("\n".join(lines) + "\n")
        return path

    def run_main(self, args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            code = summary.main(args)
        return code, json.loads(out.getvalue())

    def test_webgpu_stages_facts_and_cpu(self):
        lines = [WEBGPU.format(n=i, low=4, transport=10 + i, total=16 + i) for i in range(10)]
        lines += ["[perf 1] flips/s=48.0 flips2/s=0", "[perf 2] flips/s=0.0 flips2/s=0", "[perf 3] flips/s=52.0"]
        code, result = self.run_main([str(self.log(lines)), "--skip", "0"])
        self.assertEqual(code, 0)
        self.assertEqual(result["passes"], 10)
        self.assertEqual(result["gpu_ms"]["low"]["median"], 4.0)
        self.assertEqual(result["gpu_ms"]["transport"]["median"], 14.5)
        self.assertEqual(result["gpu_ms"]["transport"]["p90"], 19.0)
        self.assertEqual(result["cpu_ms"]["build"]["median"], 1.2)
        self.assertEqual(result["last_facts"]["transport_size"], "640x360")
        self.assertEqual(result["last_facts"]["instances"], "900")
        self.assertEqual(result["flips_per_second"]["median"], 50.0)

    def test_dxr_lines_parse(self):
        parsed = summary.parse(DXR)
        self.assertEqual(parsed["passes"][0]["stages"]["compose"], 0.7)
        self.assertEqual(parsed["passes"][0]["facts"]["size"], "2560x1440")

    def test_budget(self):
        lines = [WEBGPU.format(n=i, low=4, transport=30, total=36) for i in range(40)]
        path = self.log(lines)
        code, result = self.run_main([str(path), "--budget", "transport=12", "--budget", "low=8"])
        self.assertEqual(code, 1)
        self.assertEqual(result["passes"], 10)  # the default skips the first 30
        self.assertEqual(result["budget_failures"], ["transport: median 30.0 ms > budget 12.0 ms"])
        code, _ = self.run_main([str(path), "--budget", "transport=40"])
        self.assertEqual(code, 0)
        code, result = self.run_main([str(path), "--budget", "compose=1"])
        self.assertEqual(result["budget_failures"], ["compose: no samples"])

    def test_no_timing_lines_fails(self):
        code, result = self.run_main([str(self.log(["[perf 1] flips/s=60.0"]))])
        self.assertEqual(code, 1)
        self.assertEqual(result["passes"], 0)


if __name__ == "__main__":
    unittest.main()
