// drakan_save_pages_policy.h: save pages and the quicksave page of Load Game (issue 6 item 2).
#include "../src/lib/drakan_save_pages_policy.h"
#include <cstdio>
#include <cstdlib>

using namespace drakan_menus::policy;

static unsigned checks;
static void check(bool b, const char* message) {
    ++checks;
    if (!b) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

int main() {
    check(kQuickSlot == 33, "the quicksave is Slot33");
    check(logicalCount(4, false) == 4 && logicalCount(4, true) == 5, "one more page when listed");
    check(logicalCount(1, true) == 2 && logicalCount(0, false) == 1, "one manual page at least");
    check(toLogical(kQuickFolderPage, 4) == 4 && toLogical(2, 4) == 2, "logical index of the quicksave page");
    check(toFolder(4, 4, true) == kQuickFolderPage && toFolder(4, 4, false) == 3, "folder of the last page");
    check(toFolder(-3, 4, true) == 0, "clamped");

    // Next / previous over 4 manual pages and the quicksave page.
    check(step(3, 1, 4, true) == kQuickFolderPage, "page 4 -> quicksave");
    check(step(kQuickFolderPage, 1, 4, true) == 0, "quicksave -> page 1");
    check(step(0, -1, 4, true) == kQuickFolderPage, "page 1 back -> quicksave");
    check(step(kQuickFolderPage, -1, 4, true) == 3, "quicksave back -> page 4");
    check(step(3, 1, 4, false) == 0 && step(0, -1, 4, false) == 3, "without it: the manual pages only");
    check(step(kQuickFolderPage, 1, 4, false) == 1, "a stale quicksave page counts as page 1");
    // save_pages = 1: the quicksave page still gives a pager of two.
    check(step(0, 1, 1, true) == kQuickFolderPage && step(kQuickFolderPage, 1, 1, true) == 0, "one manual page");
    check(step(0, 1, 1, false) == 0, "one page, nothing listed: no change");
    // save_pages = 8 (slots 1..32): the quicksave folder never overlaps a manual one.
    check(step(7, 1, 8, true) == kQuickFolderPage && folderPageOfSlot(32) == 7, "8 pages");

    check(folderPageOfSlot(33) == kQuickFolderPage && folderPageOfSlot(1) == 0 && folderPageOfSlot(5) == 1,
          "folder page of a slot");
    check(listsSlot(16, 4, false) && !listsSlot(17, 4, false) && !listsSlot(33, 4, false), "manual slots");
    check(listsSlot(33, 4, true) && !listsSlot(34, 4, true), "the quicksave, not its neighbours");
    check(continueConsiders(33, 4, true) && !continueConsiders(33, 4, false), "Continue and the switch");
    std::printf("save_pages_policy: %u checks passed\n", checks);
    return 0;
}
