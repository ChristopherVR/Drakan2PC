"""Extract scheduler deadline methods for deterministic, CPU-only regression tests.

The executable uses the production enum, frequency, constants, policy state and
method bodies. Only steady_clock::now() is replaced by a controlled clock; guest
execution, renderer callbacks, condition-variable waits and EE timer registers
are test doubles. This is not a complete runtime/scheduler integration test.

CMake integration (owned separately): generate scheduler_policy_fixture.h in the
binary directory with this script, make scheduler_policy_test.cpp depend on it,
add that directory to the executable's includes, require C++20, and register the
executable with add_test. Depend on this script, EeScheduler.cpp and ee_scheduler.h
so edits regenerate the fixture. The standalone PowerShell runner does this too.
"""

import argparse
import re
from pathlib import Path

TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


def extract_braced(text: str, marker: str) -> str:
    """Fail explicitly on missing/ambiguous boundaries; ignore braces in comments."""
    if text.count(marker) != 1:
        raise ValueError(f"Expected exactly one scheduler boundary: {marker}")
    start = text.index(marker)
    depth = 0
    for token in TOKENS.finditer(text, start):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start : token.end()]
    raise ValueError(f"Unterminated scheduler boundary: {marker}")


def generate(source: Path, header: Path) -> str:
    cpp = source.read_text(encoding="utf-8-sig")
    hpp = header.read_text(encoding="utf-8-sig")
    frequency = re.search(r"static constexpr uint64_t kEeClockHz\s*=\s*([^;]+);", hpp)
    if not frequency:
        raise ValueError("Missing EE clock frequency")
    enum = extract_braced(hpp, "enum class EeEventType") + ";"
    event = extract_braced(hpp, "struct EeEvent\n") + ";"
    state_start = cpp.index("thread_local bool g_hostVblank")
    state_end = cpp.index("bool hostVblank(EeEventType type)", state_start)
    declarations = (
        f"constexpr uint64_t eeHz = {frequency.group(1)};\n" + enum + "\n" + event + "\n" + cpp[state_start:state_end]
    )

    constants = []
    for name in ("kVBlankPeriod", "kVBlankDuration", "kVBlankPeriodCycles", "kVBlankDurationCycles"):
        match = re.search(rf"constexpr [^;\n]+\b{re.escape(name)}\s*=[^;]+;", cpp)
        if not match:
            raise ValueError(f"Missing scheduler constant: {name}")
        constants.append(match.group())

    signatures = (
        "constexpr uint64_t microsecondsToEeCycles(uint64_t microseconds)",
        "std::chrono::nanoseconds eeCyclesToHostDuration(uint64_t cycles)",
        "bool hostVblank(EeEventType type)",
        "bool hostVblankDue()",
        "void EeScheduler::processDueDeadlines()",
        "void EeScheduler::waitForEvent()",
        "void EeScheduler::updateNextDeadline()",
    )
    bodies = [extract_braced(cpp, signature) for signature in signatures]
    # The cycle constants use microsecondsToEeCycles; emit that definition first.
    implementation = bodies[0] + "\n" + "\n".join(constants) + "\n" + "\n".join(bodies[1:])
    implementation = implementation.replace("std::chrono::steady_clock::now()", "testNow()")
    return (
        "// Generated from production scheduler source. Do not edit.\n"
        "// Intentionally included twice: declarations, then implementation.\n"
        "#if defined(SCHEDULER_POLICY_DECLARATIONS)\n"
        + declarations
        + "\n#elif defined(SCHEDULER_POLICY_IMPLEMENTATION)\n"
        + implementation
        + "\n#else\n#error Select a scheduler fixture phase\n#endif\n"
    )


def main() -> None:
    repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=repo / "ps2recomp/ps2xRuntime/src/lib/Kernel/EeScheduler.cpp")
    parser.add_argument("--header", type=Path, default=repo / "ps2recomp/ps2xRuntime/include/runtime/ee_scheduler.h")
    args = parser.parse_args()
    fixture = generate(args.source, args.header)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(fixture, encoding="utf-8")


if __name__ == "__main__":
    main()
