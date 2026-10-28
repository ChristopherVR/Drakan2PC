#pragma once
#include "runtime/ps2_vu1_async.h"
#include <functional>
#include <vector>

class VU1Interpreter;
namespace vu1batch {
struct Input {
    PS2Memory* memory = nullptr;
    std::vector<uint8_t> data;
    vu1async::Meta meta;
    bool path3 = false;
    std::function<void()> task;
    uint32_t offset = 0;
    bool begin = true, end = true;
    bool vif() const { return !path3 && !task; }
};
bool enabled();
bool healthy(); // false after an ordered verification mismatch
void bind(PS2Memory&, VU1Interpreter&, std::function<void(uint64_t cycles, uint64_t runs, uint64_t pairs)>);
void unbind(PS2Memory&);
// Called only by the ordered VIF executor. Returns false without changing the
// owner when the batch cannot be isolated. serial executes an original job.
bool process(std::vector<Input>&, const std::function<void(const Input&)>& serial);
void shutdown();
void printStats();
} // namespace vu1batch
