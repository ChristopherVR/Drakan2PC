#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Guest stacks for EE invocations (interrupt handlers, alarms, RPC / HLE callbacks), one 16 KB slot per (thread id,
// nesting depth). A key keeps its slot while its thread exists, so an invocation at a given depth always runs on the
// same stack. The slots come from a small fixed pool (PS2Runtime::reserveAsyncCallbackStack, 32 slots below the main
// stack), and thread ids are handed out round-robin over 2..255, so the slots of a deleted thread go back to a free
// list: without that the pool ran out after about 64 distinct thread ids and the game thread died with "EE invocation
// stack space exhausted". A released thread has no live invocation (DeleteThread needs a dormant thread,
// ExitDeleteThread clears its invocations first), so a slot is never handed out while still in use.
class EeInvocationStacks {
public:
    static constexpr uint32_t kSlotSize = 0x4000u;

    // Stack top for (threadId, depth); reserve(kSlotSize) is called for a new slot when the free list is empty and
    // returns 0 when the pool is exhausted (then acquire returns 0 too).
    template <typename Reserve>
    uint32_t acquire(int threadId, size_t depth, Reserve&& reserve) {
        const uint64_t key = makeKey(threadId, depth);
        if (const auto existing = m_tops.find(key); existing != m_tops.end()) {
            return existing->second;
        }
        uint32_t top = 0u;
        if (!m_free.empty()) {
            top = m_free.back();
            m_free.pop_back();
        } else {
            top = reserve(kSlotSize);
            if (top == 0u) {
                return 0u;
            }
        }
        m_tops.emplace(key, top);
        return top;
    }

    // Returns every slot of a thread that no longer exists to the free list.
    void releaseThread(int threadId) {
        const uint32_t id = static_cast<uint32_t>(threadId);
        bool released = false;
        for (auto it = m_tops.begin(); it != m_tops.end();) {
            if (static_cast<uint32_t>(it->first >> 32u) == id) {
                m_free.push_back(it->second);
                it = m_tops.erase(it);
                released = true;
            } else {
                ++it;
            }
        }
        // Highest address first (the order the pool hands slots out), independent of the hash map's order.
        if (released) {
            std::sort(m_free.begin(), m_free.end());
        }
    }

    // Forgets every slot. Used on a scheduler reset; the slots stay reserved in the runtime's pool (leaked, never
    // reused twice), which is safe whether or not the pool itself is reset.
    void clear() {
        m_tops.clear();
        m_free.clear();
    }

    [[nodiscard]] size_t slotsInUse() const noexcept { return m_tops.size(); }
    [[nodiscard]] size_t slotsFree() const noexcept { return m_free.size(); }

private:
    static uint64_t makeKey(int threadId, size_t depth) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(threadId)) << 32u) | static_cast<uint32_t>(depth);
    }

    std::unordered_map<uint64_t, uint32_t> m_tops;
    std::vector<uint32_t> m_free;
};
