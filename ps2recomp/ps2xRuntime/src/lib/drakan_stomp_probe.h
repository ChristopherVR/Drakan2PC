#pragma once
class PS2Runtime;
namespace drakan_stomp_probe {
// PAL-only, opt-in diagnostic. DRAKAN_STOMP_PROBE=1, optional DRAKAN_STOMP_PROBE_LINES=1..4096.
void install(PS2Runtime& runtime);
} // namespace drakan_stomp_probe
