#pragma once

void ps2xNoPreemptEnter() noexcept;
void ps2xNoPreemptLeave() noexcept;

// Synchronous host-to-guest calls may still block and unwind to the EE dispatcher.
class ScopedEeNoPreempt {
public:
    ScopedEeNoPreempt() { ps2xNoPreemptEnter(); }
    ~ScopedEeNoPreempt() { ps2xNoPreemptLeave(); }

    ScopedEeNoPreempt(const ScopedEeNoPreempt&) = delete;
    ScopedEeNoPreempt& operator=(const ScopedEeNoPreempt&) = delete;
};

// Restore temporarily edited guest fields before another guest thread can observe them,
// including when a blocking syscall transfers control to the dispatcher.
template <typename Call, typename Restore>
void withTemporaryGuestState(Call call, Restore restore) {
    const ScopedEeNoPreempt noPreempt;
    struct Cleanup {
        Restore& restore;
        ~Cleanup() { restore(); }
    } cleanup{restore};
    call();
}
