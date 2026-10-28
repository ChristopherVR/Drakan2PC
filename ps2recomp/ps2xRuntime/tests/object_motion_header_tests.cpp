#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1_async.h"
#include <cstdio>
#include <type_traits>

static_assert(std::is_same_v<drakan_object_motion::JobPtr, std::shared_ptr<const drakan_object_motion::Job>>);
static_assert(std::is_same_v<decltype(PS2Memory::PendingTransfer{}.objectMotion), drakan_object_motion::JobPtr>);
static_assert(std::is_same_v<decltype(vu1async::Meta{}.objectMotion), drakan_object_motion::JobPtr>);

int main() {
    // These operations must work without a complete Job: construction, copy,
    // move, reset and destruction of the public containing types.
    PS2Memory::PendingTransfer transfer;
    auto copy = transfer;
    auto moved = std::move(copy);
    moved.objectMotion.reset();
    vu1async::Meta meta;
    auto metaCopy = meta;
    metaCopy.objectMotion.reset();
    std::printf("PS2Memory=%zu/%zu PendingTransfer=%zu/%zu scopeOffset=%zu Meta=%zu/%zu scopeOffset=%zu\n",
                sizeof(PS2Memory), alignof(PS2Memory), sizeof(transfer), alignof(decltype(transfer)),
                offsetof(PS2Memory::PendingTransfer, objectMotion), sizeof(meta), alignof(decltype(meta)),
                offsetof(vu1async::Meta, objectMotion));
}
