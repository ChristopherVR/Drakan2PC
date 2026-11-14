// Save pages and the quicksave in Load Game (pure arithmetic, unit tested: tests/save_pages_policy_tests.cpp).
//
// A save page names four memory card folders: folder page p is Slot(4p+1)..Slot(4p+4). The manual pages are folder
// pages 0 .. pages-1 ([general] save_pages, at most 8, so slots 1..32). The quicksave (F5) lives on folder page 8
// (Slot33, outside the manual slots). With [general] quicksave_in_load on and a quicksave on the card, Load Game
// lists it as one more page after the manual ones; Save Game never shows it (a manual save must not overwrite it).
// "Logical" pages are what the pager counts (0 .. count-1); "folder" pages are what the slot cells name.
#pragma once
#include <algorithm>

namespace drakan_menus::policy {

constexpr int kQuickFolderPage = 8; // Slot33..Slot36; the quicksave is Slot33
constexpr int kQuickSlot = kQuickFolderPage * 4 + 1;

inline int logicalCount(int pages, bool quickListed) {
    return std::max(1, pages) + (quickListed ? 1 : 0);
}

inline int toLogical(int folderPage, int pages) {
    return folderPage == kQuickFolderPage ? std::max(1, pages) : folderPage;
}

inline int toFolder(int logical, int pages, bool quickListed) {
    pages = std::max(1, pages);
    if (quickListed && logical == pages)
        return kQuickFolderPage;
    return std::clamp(logical, 0, pages - 1);
}

// The folder page `delta` pages on from `folderPage`, wrapping over the manual pages and the quicksave page when
// it is listed. A folder page that is not in the list (the quicksave page in Save Game) counts as the first page.
inline int step(int folderPage, int delta, int pages, bool quickListed) {
    pages = std::max(1, pages);
    const int count = logicalCount(pages, quickListed);
    int at = toLogical(folderPage, pages);
    if (at < 0 || at >= count || (folderPage == kQuickFolderPage && !quickListed))
        at = 0;
    const int next = ((at + delta) % count + count) % count;
    return toFolder(next, pages, quickListed);
}

// The folder page that holds slot `slot` (1 based), and whether a list with these pages shows it.
inline int folderPageOfSlot(int slot) {
    return slot > 0 ? (slot - 1) / 4 : 0;
}
inline bool listsSlot(int slot, int pages, bool quickListed) {
    if (slot >= 1 && slot <= std::max(1, pages) * 4)
        return true;
    return quickListed && slot == kQuickSlot;
}

// The title's Continue picks the newest save among these slots: the manual ones, plus the quicksave when Load Game
// lists it (so Continue after a quit resumes from the quicksave if it is the latest save).
inline bool continueConsiders(int slot, int pages, bool quickInLoad) {
    return listsSlot(slot, pages, quickInLoad);
}

} // namespace drakan_menus::policy
