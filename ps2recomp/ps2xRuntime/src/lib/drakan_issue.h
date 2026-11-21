#pragma once

namespace drakan_issue {
// Reporter subprocess mode. Handle it before starting the game or rotating its log.
bool runHelper(int argc, char* argv[]);
void watchForCrashes();
void markCrash();
bool available();
void report();
} // namespace drakan_issue
