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

    // Continue: the newest save of any kind (issue 10), by write time.
    const auto newest = [](const int(*saves)[2], int n, int pages, bool quickInLoad) {
        int best = 0, bestT = 0;
        for (int i = 0; i < n; ++i)
            if (newerSave(saves[i][0], saves[i][1], best, bestT, pages, quickInLoad)) {
                best = saves[i][0];
                bestT = saves[i][1];
            }
        return best;
    };
    const int quickNewer[][2] = {{1, 100}, {4, 200}, {33, 300}};
    check(newest(quickNewer, 3, 4, true) == 33, "a newer quicksave wins Continue");
    check(newest(quickNewer, 3, 4, false) == 4, "quicksave_in_load = 0: the newest manual save");
    const int manualNewer[][2] = {{33, 300}, {2, 400}};
    check(newest(manualNewer, 2, 4, true) == 2, "a newer manual save wins over the quicksave");
    const int tie[][2] = {{33, 300}, {3, 300}};
    check(newest(tie, 2, 4, true) == 3, "equal times: the manual save");
    const int onlyQuick[][2] = {{33, 50}};
    check(newest(onlyQuick, 1, 4, true) == 33, "only a quicksave on the card");
    const int offPage[][2] = {{20, 900}, {33, 300}};
    check(newest(offPage, 2, 4, true) == 33, "a slot beyond save_pages does not count");
    std::printf("save_pages_policy: %u checks passed\n", checks);
    return 0;
}
