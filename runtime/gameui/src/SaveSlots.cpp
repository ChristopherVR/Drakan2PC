#include "gameui/SaveSlots.h"

#include <algorithm>
#include <cstdio>

#include "gameui/Addresses.h"
#include "gameui/MenuText.h"

namespace gameui {

CardState cardStateFromAnySave(int32_t anySave) {
    switch (anySave) {
        case 0:
        case 1:
            return CardState::Ready;
        case 2:
            return CardState::NoCard;
        case 3:
            return CardState::Unformatted;
        case 4:
            return CardState::Full;
        default:
            return CardState::Error;
    }
}

const char* cardStateName(CardState c) {
    switch (c) {
        case CardState::Ready:
            return "Ready";
        case CardState::Reading:
            return "Reading";
        case CardState::NoCard:
            return "NoCard";
        case CardState::Unformatted:
            return "Unformatted";
        case CardState::Full:
            return "Full";
        case CardState::Error:
            return "Error";
    }
    return "?";
}

const char* slotStateName(SlotState s) {
    switch (s) {
        case SlotState::Unknown:
            return "Unknown";
        case SlotState::Empty:
            return "Empty";
        case SlotState::Used:
            return "Used";
        case SlotState::NoSpace:
            return "NoSpace";
        case SlotState::Corrupt:
            return "Corrupt";
    }
    return "?";
}

bool SaveTimestamp::valid() const {
    return year >= 1990 && year <= 2200 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour >= 0 &&
           hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 60;
}

bool newerThan(const SaveTimestamp& a, const SaveTimestamp& b) {
    const int32_t ka[] = {a.year, a.month, a.day, a.hour, a.minute, a.second};
    const int32_t kb[] = {b.year, b.month, b.day, b.hour, b.minute, b.second};
    return std::lexicographical_compare(std::begin(kb), std::end(kb), std::begin(ka), std::end(ka));
}

namespace {
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
}

std::string formatSaveDate(const SaveTimestamp& t, DateOrder order) {
    if (!t.valid())
        return {};
    char b[48];
    const char* m = kMonths[t.month - 1];
    switch (order) {
        case DateOrder::MonthDayYear:
            std::snprintf(b, sizeof b, "%s %d, %d", m, t.day, t.year);
            break;
        case DateOrder::YearMonthDay:
            std::snprintf(b, sizeof b, "%d %s %d", t.year, m, t.day);
            break;
        case DateOrder::DayMonthYear:
        default:
            std::snprintf(b, sizeof b, "%d %s %d", t.day, m, t.year);
            break;
    }
    return b;
}

std::string formatSaveTime(const SaveTimestamp& t, bool clock24) {
    if (!t.valid())
        return {};
    char b[24];
    if (clock24) {
        std::snprintf(b, sizeof b, "%02d:%02d", t.hour, t.minute);
    } else {
        const int h = t.hour % 12 == 0 ? 12 : t.hour % 12;
        std::snprintf(b, sizeof b, "%d:%02d %s", h, t.minute, t.hour < 12 ? "AM" : "PM");
    }
    return b;
}

std::string formatPlayTime(int32_t seconds) {
    if (seconds < 0)
        return {};
    const int32_t minutes = seconds / 60, hours = minutes / 60;
    char b[32];
    if (hours > 0)
        std::snprintf(b, sizeof b, "%d h %02d min", hours, minutes % 60);
    else
        std::snprintf(b, sizeof b, "%d min", minutes);
    return b;
}

const SaveSlot* SaveSlotsState::slot(int number) const {
    for (const SaveSlot& s : slots)
        if (s.number == number)
            return &s;
    return nullptr;
}

bool readSaveSlots(const IGuestMemory& mem, uint32_t screen, const SaveReadOptions& opt, SaveSlotsState& out) {
    namespace su = addr::saveui;
    out.page = std::max(0, opt.page);
    out.pageCount = std::max(1, opt.pageCount);
    out.dateOrder = opt.dateOrder;
    out.clock24 = opt.clock24;
    out.slots.clear();
    out.nativeRow = -1;
    const uint32_t id = screen ? mem.read32(screen + addr::screen::kId) : 0;
    if (screen < addr::kRamPtrMin || screen >= addr::kRamPtrEnd || (id != su::kScreenLoad && id != su::kScreenSave)) {
        out.card = CardState::Error;
        return false;
    }
    const int32_t count = mem.readS32(screen + su::kCount);
    const uint32_t entries = mem.read32(screen + su::kEntries);
    if (count <= 0 || entries < addr::kRamPtrMin || entries >= addr::kRamPtrEnd) {
        out.card = CardState::Reading;
        for (int i = 0; i < su::kSlotsPerPage; ++i)
            out.slots.push_back({out.page * su::kSlotsPerPage + i + 1});
        return true;
    }
    out.card = CardState::Ready;
    const int n = std::min<int32_t>(count, su::kSlotsPerPage);
    for (int i = 0; i < n; ++i) {
        const uint32_t e = entries + static_cast<uint32_t>(i) * su::kEntrySize;
        SaveSlot s;
        s.number = out.page * su::kSlotsPerPage + i + 1;
        const std::string caption = mem.readString(e + su::kEntryCaption, su::kEntryCaptionMax);
        const uint32_t lo = mem.read32(e + su::kEntrySortKey), hi = mem.read32(e + su::kEntrySortKey + 4);
        const int64_t key = static_cast<int64_t>((static_cast<uint64_t>(hi) << 32) | lo);
        const bool hasFile = mem.read8(e + su::kEntryPath) != 0;
        if (hasFile) {
            s.state = SlotState::Used;
            s.location = caption;
            const std::string path = mem.readString(e + su::kEntryPath, 0x100);
            const size_t slash = path.find_last_of("/\\");
            s.file = slash == std::string::npos ? path : path.substr(slash + 1);
            const uint32_t t = e + su::kEntryTime;
            s.time = {mem.readS32(t + 20), mem.readS32(t + 16), mem.readS32(t + 12),
                      mem.readS32(t + 8),  mem.readS32(t + 4),  mem.readS32(t)};
            if (s.time.year >= 0 && s.time.year < 100)
                s.time.year += 2000; // in case the card stamp keeps two digits [U]
            s.preview = {ImageKind::SavePreview, static_cast<uint32_t>(s.number)};
        } else if (!opt.noSpaceCaption.empty() && caption == opt.noSpaceCaption) {
            s.state = SlotState::NoSpace;
        } else {
            s.state = key == su::kNoSaveKey || key == 0 ? SlotState::Empty : SlotState::Unknown;
        }
        out.slots.push_back(std::move(s));
    }
    const int32_t sel = mem.readS32(screen + su::kSelection);
    out.nativeRow = sel >= 0 && sel < n ? sel : -1;
    return true;
}

std::string savePackProblemText(SavePackProblem problem, const std::string& pack) {
    switch (problem) {
        case SavePackProblem::None:
            return {};
        case SavePackProblem::DifferentBuild:
            return menuFormat("This save was made with a different version of the mod pack {0}. It cannot be loaded "
                              "with the version installed now.",
                              {pack});
        case SavePackProblem::OlderThanPack:
            return menuFormat("This save was made with an earlier version of the mod pack {0}. It cannot be loaded "
                              "with the version installed now.",
                              {pack});
        case SavePackProblem::MissingPack:
            return menuFormat("This save needs the mod pack {0}. Enable it on the Mods page and restart the game to "
                              "load it.",
                              {pack});
        case SavePackProblem::BadRecord:
            return menuText("The list of mod packs stored with this save is damaged. The save cannot be loaded.");
    }
    return {};
}

std::string savePackSlotNote(SavePackProblem problem) {
    switch (problem) {
        case SavePackProblem::None:
            return {};
        case SavePackProblem::DifferentBuild:
            return menuText("Made with another version of a mod pack.");
        case SavePackProblem::OlderThanPack:
            return menuText("Made with an earlier version of a mod pack.");
        case SavePackProblem::MissingPack:
            return menuText("Needs a mod pack that is not enabled.");
        case SavePackProblem::BadRecord:
            return menuText("Its list of mod packs is damaged.");
    }
    return {};
}

std::vector<std::string> nativeSlotNoteLines(SavePackProblem problem,
                                             const std::function<int(const std::string&)>& measure, int width,
                                             int maxLines) {
    std::vector<std::string> lines;
    const std::string reason = savePackSlotNote(problem);
    if (reason.empty() || maxLines <= 0)
        return lines;
    lines.push_back(menuText("Unavailable"));
    std::string line;
    for (size_t i = 0; i <= reason.size();) {
        const size_t end = std::min(reason.find(' ', i), reason.size());
        const std::string word = reason.substr(i, end - i);
        i = end + 1;
        if (word.empty())
            continue;
        const std::string longer = line.empty() ? word : line + " " + word;
        if (line.empty() || measure(longer) <= width) {
            line = longer;
        } else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty())
        lines.push_back(line);
    if (lines.size() > static_cast<size_t>(maxLines))
        lines.resize(static_cast<size_t>(maxLines));
    return lines;
}

std::vector<NativeSlotMark> nativeRefusedSlots(const IGuestMemory& mem, uint32_t screen,
                                               const std::function<SavePackProblem(const std::string& path)>& check) {
    namespace su = addr::saveui;
    std::vector<NativeSlotMark> marks;
    if (screen < addr::kRamPtrMin || screen >= addr::kRamPtrEnd ||
        mem.read32(screen + addr::screen::kId) != su::kScreenLoad)
        return marks;
    const int32_t count = mem.readS32(screen + su::kCount);
    const uint32_t entries = mem.read32(screen + su::kEntries);
    if (count <= 0 || entries < addr::kRamPtrMin || entries >= addr::kRamPtrEnd)
        return marks;
    for (int i = 0; i < std::min<int32_t>(count, su::kSlotsPerPage); ++i) {
        const uint32_t e = entries + static_cast<uint32_t>(i) * su::kEntrySize + su::kEntryPath;
        std::string path;
        for (uint32_t k = 0; k < 0x100; ++k) {
            const uint8_t ch = mem.read8(e + k);
            if (!ch)
                break;
            path.push_back(static_cast<char>(ch));
        }
        if (path.empty())
            continue;
        if (const SavePackProblem p = check(path); p != SavePackProblem::None)
            marks.push_back({i, p});
    }
    return marks;
}

} // namespace gameui
