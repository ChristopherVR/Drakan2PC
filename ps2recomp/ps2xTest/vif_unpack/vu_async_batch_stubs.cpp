// The queue-only mock test has no geometry runtime. The full batch engine is
// exercised by the runtime-linked integration tests instead.
#include "../../ps2xRuntime/src/lib/ps2_vu1_batch.h"
namespace vu1batch {
bool enabled() {
    return false;
}
bool process(std::vector<Input>&, const std::function<void(const Input&)>&) {
    return false;
}
void printStats() {}
} // namespace vu1batch
