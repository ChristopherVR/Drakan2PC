// The save slot model shared by the modern Save / Load screen and the title menu: what the native slot screens
// (Load = screen 4, Save = screen 5) list per slot, a reader over their entry array, and date / time formatting.
// docs/journal/2026-10-25/modern-save-load-title.md section 1 has the native layout.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gameui/Draw.h"
#include "gameui/GuestMemory.h"

namespace gameui {

// The memory card as the slot screens see it. `37e810(0)` answers 0 (card, no save), 1 (a save exists), 2 (no card),
// 3 (unformatted), 4 (no save and too little free space).
enum class CardState : uint8_t {
    Ready,       // the list is read
    Reading,     // the list is not built yet (entry count 0) or a page change is rescanning
    NoCard,      // no memory card (the native list shows its "no memory card" box)
    Unformatted, // card present, not formatted
    Full,        // no save on the card and not enough free space for one
    Error,       // anything else the glue reports (SaveSlotsState::message says what)
};
CardState cardStateFromAnySave(int32_t anySave);
const char* cardStateName(CardState c);

enum class SlotState : uint8_t {
    Unknown, // not read yet (another page, or the list is rebuilding)
    Empty,   // no save in the slot; a save can go there
    Used,    // a save exists
    NoSpace, // empty, but the card has no room for a new save (native caption = property 0xfb)
    Corrupt, // a save exists but cannot be loaded (known only after a failed load, or from the glue)
};
const char* slotStateName(SlotState s);

// The card's time stamp of the save (the entry's six ints at +0x130: second, minute, hour, day, month, year).
struct SaveTimestamp {
    int32_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    bool valid() const;
    bool operator==(const SaveTimestamp&) const = default;
};
// Later than (for "the newest save"): compares year, month, day, hour, minute, second.
bool newerThan(const SaveTimestamp& a, const SaveTimestamp& b);

// How the console formats dates (`ctx+0x4f8` in the native grid draw: 0 month / day / year, 1 day / month / year,
// 2 year / month / day) and the clock (`ctx+0x4f4`: 0 = 12 hour with AM / PM).
enum class DateOrder : uint8_t { MonthDayYear, DayMonthYear, YearMonthDay };
// "25 Oct 2026" style with the month's name (order kept), "16:23" / "4:23 PM".
std::string formatSaveDate(const SaveTimestamp& t, DateOrder order);
std::string formatSaveTime(const SaveTimestamp& t, bool clock24);
// "1 h 05 min" / "12 min" (play time; empty for a negative value).
std::string formatPlayTime(int32_t seconds);

struct SaveSlot {
    int number = 0; // 1-based slot number across pages: page * 4 + row + 1 (folder BESCES-50006Slot<n>)
    SlotState state = SlotState::Unknown;
    std::string location;     // the native caption: the level's name (e.g. "Surdana"); empty for empty slots
    std::string file;         // the save's file name without the folder ("Level 1A - Surdana.rsg"), optional
    SaveTimestamp time;       // when the save was written (invalid = unknown)
    int32_t playSeconds = -1; // play time when the glue knows it (no guest field is known yet): < 0 = not shown
    ImageRef preview;         // {SavePreview, number} for a used slot; None = no picture
    bool operator==(const SaveSlot&) const = default;
};

struct SaveSlotsState {
    CardState card = CardState::Ready;
    int page = 0;                // the page the native list shows (drakan_menus' pager: folders Slot(4p+1)..Slot(4p+4))
    int pageCount = 1;           // [general] save_pages
    std::vector<SaveSlot> slots; // the shown page's slots in row order (4 when read); other pages are not listed
    int nativeRow = -1;          // the native selection (screen +0xa4) on this page, -1 = unknown
    bool busy = false;   // a native box, the Saving screen or a rescan is in progress: the host screen takes no input
    bool inGame = false; // a level is running: a load asks first (the native 43d028 asks in that case too)
    DateOrder dateOrder = DateOrder::DayMonthYear;
    bool clock24 = true;
    std::string message; // optional status / error line from the glue ("The save could not be written.")
    // The glue is loading this slot on its own (the title's Continue): the screen shows the loading card at once,
    // whatever the list shows. 0 = none.
    int loadingSlot = 0;
    int quickOperation = 0; // 1 = quicksave, 2 = quickload: progress only, no interactive slot browser

    const SaveSlot* slot(int number) const;
    int firstNumber() const { return page * 4 + 1; }
};

struct SaveReadOptions {
    int page = 0; // the page the cells currently name
    int pageCount = 1;
    std::string noSpaceCaption; // the text of property 0xfb as the string manager returns it (UTF-8); empty = unknown
    DateOrder dateOrder = DateOrder::DayMonthYear;
    bool clock24 = true;
};

// Reads the slot list of a Load / Save screen object (`screen` = *0x4fb60c while it is screen 4 or 5): entry count,
// entries, selection. Plain reads; returns false (state.card = Error) when the screen is not a slot screen. An entry
// count of 0 means the list is not built yet (Reading). Captions are Latin-1 in the guest and UTF-8 here.
bool readSaveSlots(const IGuestMemory& mem, uint32_t screen, const SaveReadOptions& opt, SaveSlotsState& out);

} // namespace gameui
