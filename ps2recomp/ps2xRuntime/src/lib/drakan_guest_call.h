#pragma once

#include "input/GuestInterface.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_no_preempt.h"
#include "runtime/ee_scheduler.h"

#include <atomic>
#include <memory>

namespace drakan_guest {

struct CallOptions {
    PS2Runtime::RecompiledFunction original = nullptr;
    uint32_t stack = 0;
    unsigned maxDispatches = 64;
    void (*trace)(const char*, ...) = nullptr;
};

inline std::optional<uint32_t> callImmediate(uint8_t* ram, const R5900Context& current, PS2Runtime& runtime,
                                             const drakan_input::GuestCall& call, CallOptions options = {}) {
    if (!call.valid())
        return std::nullopt;
    auto function = options.original;
    if (!function) {
        if (!runtime.hasFunction(call.addr))
            return std::nullopt;
        function = runtime.lookupFunction(call.addr);
    }

    auto context = std::make_unique<R5900Context>(current);
    for (size_t i = 0; i < call.integers().size(); ++i)
        SET_GPR_U32(context.get(), static_cast<int>(4 + i), call.integers()[i]);
    for (size_t i = 0; i < call.floats().size(); ++i)
        context->f[12 + i] = call.floats()[i];
    if (options.stack)
        SET_GPR_U32(context.get(), 29, options.stack);
    SET_GPR_U32(context.get(), 31, 0);
    context->pc = call.addr;
    context->in_delay_slot = false;

    const ScopedEeNoPreempt noPreempt;
    for (unsigned step = 0; step < options.maxDispatches && context->pc; ++step) {
        if (!function) {
            if (!runtime.hasFunction(context->pc))
                break;
            function = runtime.lookupFunction(context->pc);
        }
        if (options.trace)
            options.trace("[combat-call] step=%u pc=%x ra=%x sp=%x\n", step, context->pc, getRegU32(context.get(), 31),
                          getRegU32(context.get(), 29));
        function(ram, context.get(), &runtime);
        function = nullptr;
        if (options.trace)
            options.trace("[combat-call] step=%u returned pc=%x ra=%x sp=%x\n", step, context->pc,
                          getRegU32(context.get(), 31), getRegU32(context.get(), 29));
    }
    if (options.trace)
        options.trace("[combat-call] leave pc=%x result=%u sp=%x\n", context->pc, getRegU32(context.get(), 2),
                      getRegU32(context.get(), 29));
    if (context->pc)
        return std::nullopt;
    return getRegU32(context.get(), 2);
}

inline bool enqueueCall(PS2Runtime& runtime, const drakan_input::GuestCall& call, std::atomic<unsigned>& pending) {
    if (!runtime.hasFunction(call.addr))
        return false;
    constexpr int kRpcPriority = 6;
    ++pending;
    try {
        runtime.eeScheduler().postGuestCall(
            call.addr, call.integerRegisters(), call.floatRegisters(), [count = &pending](uint32_t) { --*count; },
            kRpcPriority);
    } catch (...) {
        --pending;
        throw;
    }
    return true;
}

} // namespace drakan_guest
