#include "world/Inventory.h"

#include <cstdio>
#include <unordered_map>

int main() {
    std::unordered_map<uint32_t, uint32_t> memory;
    unsigned writes = 0;
    auto read = [&](uint32_t address) { return memory[address]; };
    auto write = [&](uint32_t address, uint32_t value) {
        memory[address] = value;
        ++writes;
    };
    constexpr uint32_t player = 0x10000, item = 0x20000;
    constexpr uint32_t cells = player + 0x584 + 0x30, hands = player + 0xa48;
    // An already collected item can occupy multiple references, including the last cell/hand.
    for (uint32_t i : {0u, 23u}) {
        for (uint32_t offset = 0; offset < 16; offset += 4)
            memory[cells + i * 16 + offset] = offset == 8 ? item : 7;
    }
    memory[hands] = item;
    memory[hands + 48] = item;
    memory[cells + 16 + 8] = item + 4; // another item survives
    memory[hands + 16] = item + 4;
    memory[hands + 4] = 99; // holder metadata survives
    auto check = [](bool ok) {
        if (!ok)
            std::fprintf(stderr, "inventory detachment regression failed\n");
        return ok;
    };
    if (!check(world::inventoryReferencesItem(world::kClassRynn, player, item, read)))
        return 1;
    if (!check(world::detachInventoryItem(world::kClassRynn, player, item, read, write) == 4))
        return 1;
    for (uint32_t i : {0u, 23u})
        for (uint32_t offset = 0; offset < 16; offset += 4)
            if (!check(memory[cells + i * 16 + offset] == 0))
                return 1;
    if (!check(memory[hands] == 0 && memory[hands + 48] == 0 && memory[hands + 4] == 99 &&
               memory[cells + 24] == item + 4 && memory[hands + 16] == item + 4 && memory[player + 0x584 + 0x238] == 1))
        return 1;
    if (!check(!world::inventoryReferencesItem(world::kClassRynn, player, item, read)))
        return 1;
    writes = 0;
    if (!check(world::detachInventoryItem(world::kClassRynn, player, item, read, write) == 0 && writes == 0 &&
               world::detachInventoryItem(world::kClassRynn, 0, item, read, write) == 0 &&
               world::detachInventoryItem(world::kClassRynn, player, 0, read, write) == 0 && writes == 0))
        return 1;
    unsigned reads = 0;
    auto countedRead = [&](uint32_t address) {
        ++reads;
        return read(address);
    };
    // Arokh/NPC data is not an inventory, even when a coincidental word matches the item.
    memory[cells + 8] = item;
    for (uint32_t actorClass : {0u, world::kClassDragon, 0x1001eu}) {
        if (!check(!world::inventoryReferencesItem(actorClass, player, item, countedRead) && reads == 0))
            return 1;
        if (!check(world::detachInventoryItem(actorClass, player, item, countedRead, write) == 0 && reads == 0 &&
                   writes == 0 && memory[cells + 8] == item))
            return 1;
    }
    return 0;
}
