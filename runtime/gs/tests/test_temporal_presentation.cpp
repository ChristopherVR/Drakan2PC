#include "temporal_presentation.h"
#include <cassert>
#include <iostream>
int main() {
    gsr::TemporalPresentation p;
    assert(p.status("ready").find("no completed") != std::string::npos);
    p.present(true, true, 7, true);
    assert(p.status("").find("active") != std::string::npos);
    assert(p.processedPresents == 1);
    p.present(false, true, 8, true);
    assert(p.status("depth missing").find("BYPASSED") != std::string::npos);
    assert(p.status("depth missing").find("depth missing") != std::string::npos);
    p.present(false, false, 0, true);
    assert(p.status("").find("menu/loading") != std::string::npos);
    p.present(true, true, 9, false);
    assert(p.status("").find("failed") != std::string::npos);
    p.present(true, true, 0, true);
    assert(p.status("").find("BYPASSED") != std::string::npos);
    p.present(true, false, 10, true);
    assert(p.status("").find("menu/loading") != std::string::npos);
    p.reset();
    assert(!p.seen && !p.processedPresents && !p.bypassedPresents);
    std::cout << "temporal presentation transitions passed\n";
}
