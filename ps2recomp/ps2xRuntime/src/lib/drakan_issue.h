#pragma once

#include <string_view>

namespace drakan_issue {
// Reporter subprocess mode. Handle it before starting the game or rotating its log.
bool runHelper(int argc, char* argv[]);
void watchForCrashes();
void markCrash();
bool available();
void report(std::string_view screen = "Game", std::string_view campaign = {}, std::string_view level = {});
} // namespace drakan_issue
