// ModernSettings: the glue between the modern PC Settings pages (gameui::SettingsPages, drawn inside the modern pause
// menu) and the settings files / the running game. It produces the pages' schema and applies their actions through
// the same path as the F1 overlay's Apply (applySettings: the host's live apply* calls, the minimal-diff save, the
// bindings reload, restart bookkeeping), one action at a time, at once. Platform free; runs on the thread that draws
// the pages (the overlay's present thread, like SettingsMenu). Contract: runtime/gameui/README.md, "PC Settings
// pages".
#pragma once
#include <string>
#include <vector>

#include "gameui/SettingsPages.h"
#include "gameui/SettingsSchema.h"
#include "settings/Host.h"
#include "settings/Schema.h"
#include "settings/Store.h"
#include "settings/Types.h"

namespace dsettings {

class ModernSettings {
public:
    // Share the F1 overlay's store (SettingsOverlay::store()) so both UIs see each other's saves.
    ModernSettings(ISettingsHost& host, SettingsStore& store);

    // Call when the pages open (and after the F1 overlay closed over them): re-reads the files, the host's
    // capabilities, monitor and audio devices.
    void open();
    // Refreshes the capabilities (DLSS / ray tracing status lines) every `period` seconds of `now`.
    void tick(double now, double period = 0.5);

    // The schema for SettingsPages::frame (rebuilt after every apply and capability refresh).
    const gameui::SettingsSchema& schema();

    // Applies one action and reports what the pages should show (restart card, problem card). Change with
    // final = false (a slider drag) is applied live but not saved; the final one saves.
    gameui::SettingsFeedback apply(const gameui::SettingsAction& a);
    // All actions of a frame, in order; feed each result to SettingsPages::onApplied (or PauseScreen::pcSettings()).
    void applyAll(const std::vector<gameui::SettingsAction>& actions, gameui::SettingsPages& pages);

    const Settings& applied() const { return applied_; }
    bool linkSensitivity() const { return link_; }

private:
    ISettingsHost& host_;
    SettingsStore& store_;
    Settings applied_;
    HostCaps caps_;
    MonitorInfo monitor_;
    std::vector<std::string> devices_;
    bool link_ = true;
    bool unsaved_ = false; // a live-only (dragged) value waits for the final save
    bool dirty_ = true;
    double nextCaps_ = 0;
    uint32_t revision_ = 0;
    gameui::SettingsSchema schema_;
};

} // namespace dsettings
