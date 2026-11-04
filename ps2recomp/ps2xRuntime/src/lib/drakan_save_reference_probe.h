#pragma once
class PS2Runtime;
namespace drakan_save_reference_probe {
// PAL-only, bounded read-only save-reference observations. Disabled unless explicitly requested.
void install(PS2Runtime& runtime);
} // namespace drakan_save_reference_probe
