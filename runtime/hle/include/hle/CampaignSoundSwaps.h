#pragma once
// Campaign sound swaps (docs/reference/modding.md "Campaign sound swaps"). A campaign pack's `sounds/swaps.ini` lists
// play ids (`database index << 16 | sound id`, as the game's PlaySound takes them) that the campaign plays differently:
//
//   # comment
//   0x202d6 0x202d7 = 0x3041a 0x3041b     whenever 0x202d6 or 0x202d7 is played, play 0x3041a, then 0x3041b, in turn
//   0x503ee 0x503ef = none                play nothing there
//
// The swaps apply only while that pack's campaign is the current game, so Drakan 2 in the same session keeps its sounds
// (Rynn's and Arokh's records and the Drakan 2 creature clips are shared and never changed). Platform free.
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace hle {

class CampaignSoundSwaps {
public:
    // Parse one pack's table. Returns the number of swapped ids; malformed lines are skipped and counted in `bad`.
    size_t add(const std::string& modId, const std::string& text, size_t* bad = nullptr);
    void setActive(std::function<std::string()> f) { active_ = std::move(f); }
    // The id to play for `id` now: `id` itself when the current game has no swap for it, 0 = play nothing.
    uint32_t map(uint32_t id);
    // The current game has a table (cheap check before `map`).
    bool campaignActive() const;
    size_t size() const { return tables_.size(); }

private:
    struct Group {
        std::vector<uint32_t> to; // empty: nothing
        size_t next = 0;
    };
    struct Table {
        std::vector<Group> groups;
        std::map<uint32_t, size_t> from; // id -> group
    };
    std::map<std::string, Table> tables_;
    std::function<std::string()> active_;
    std::mutex mu_;
};

} // namespace hle
