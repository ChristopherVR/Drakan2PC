#include "hle/CampaignSoundSwaps.h"

#include <cstdlib>
#include <sstream>

namespace hle {

namespace {

bool parseId(const std::string& word, uint32_t& out) {
    if (word.empty())
        return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(word.c_str(), &end, 0);
    if (!end || *end || v == 0 || v > 0xffffffffUL)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseIds(const std::string& text, std::vector<uint32_t>& out) {
    std::istringstream in(text);
    std::string w;
    while (in >> w) {
        uint32_t id = 0;
        if (!parseId(w, id))
            return false;
        out.push_back(id);
    }
    return true;
}

} // namespace

size_t CampaignSoundSwaps::add(const std::string& modId, const std::string& text, size_t* bad) {
    Table& t = tables_[modId];
    size_t added = 0, wrong = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (const size_t hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        if (line.find_first_not_of(" \t\r") == std::string::npos)
            continue;
        const size_t eq = line.find('=');
        std::vector<uint32_t> from, to;
        std::string rhs = eq == std::string::npos ? std::string() : line.substr(eq + 1);
        const size_t a = rhs.find_first_not_of(" \t\r"), b = rhs.find_last_not_of(" \t\r");
        rhs = a == std::string::npos ? std::string() : rhs.substr(a, b - a + 1);
        if (eq == std::string::npos || !parseIds(line.substr(0, eq), from) || from.empty() ||
            (rhs != "none" && (!parseIds(rhs, to) || to.empty()))) {
            ++wrong;
            continue;
        }
        t.groups.push_back({std::move(to), 0});
        for (uint32_t id : from)
            if (t.from.emplace(id, t.groups.size() - 1).second)
                ++added;
    }
    if (bad)
        *bad = wrong;
    if (t.groups.empty())
        tables_.erase(modId);
    return added;
}

bool CampaignSoundSwaps::campaignActive() const {
    return active_ && !tables_.empty() && tables_.count(active_()) != 0;
}

uint32_t CampaignSoundSwaps::map(uint32_t id) {
    if (!active_ || tables_.empty())
        return id;
    const auto t = tables_.find(active_());
    if (t == tables_.end())
        return id;
    const auto f = t->second.from.find(id);
    if (f == t->second.from.end())
        return id;
    std::lock_guard<std::mutex> lock(mu_);
    Group& g = t->second.groups[f->second];
    if (g.to.empty())
        return 0;
    const uint32_t out = g.to[g.next % g.to.size()];
    g.next = (g.next + 1) % g.to.size();
    return out;
}

} // namespace hle
