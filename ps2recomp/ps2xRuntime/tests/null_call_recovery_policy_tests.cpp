#include "../src/lib/drakan_null_call_recovery.h"
#include <cassert>
#include <iostream>

using namespace drakan_null_call_recovery;

int main() {
    // The handle update's null call is skipped.
    assert(shouldSkip(true, 0u, 0x10aa6cu));
    // Any other target is a real missing function, not a null resource.
    assert(!shouldSkip(true, 0x10u, 0x10aa6cu));
    assert(!shouldSkip(true, 0x10aa74u, 0x10aa6cu));
    // Other call sites keep the strict behaviour (the second JALR of the same function, the controller walk's).
    assert(!shouldSkip(true, 0u, 0x10aa50u));
    assert(!shouldSkip(true, 0u, 0x10822cu));
    assert(!shouldSkip(true, 0u, 0u));
    // Jumps and returns to 0 are never skipped.
    assert(!shouldSkip(false, 0u, 0x10aa6cu));
    std::cout << "null call recovery policy ok\n";
    return 0;
}
