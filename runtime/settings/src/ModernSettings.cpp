#include "settings/ModernSettings.h"

#include <cmath>
#include <sstream>

#include "settings/Apply.h"

namespace dsettings {

ModernSettings::ModernSettings(ISettingsHost& host, SettingsStore& store) : host_(host), store_(store) {
    applied_ = store_.loaded();
    link_ = std::fabs(applied_.controls.sensX - applied_.controls.sensY) < 1e-6f;
}

void ModernSettings::open() {
    std::string w;
    store_.load(&w);
    applied_ = store_.loaded();
    link_ = std::fabs(applied_.controls.sensX - applied_.controls.sensY) < 1e-6f;
    caps_ = host_.capabilities();
    monitor_ = host_.monitor();
    devices_ = host_.audioDevices();
    unsaved_ = false;
    dirty_ = true;
}

void ModernSettings::tick(double now, double period) {
    if (now < nextCaps_)
        return;
    nextCaps_ = now + period;
    caps_ = host_.capabilities();
    dirty_ = true;
}

const gameui::SettingsSchema& ModernSettings::schema() {
    if (dirty_) {
        SchemaEnv env;
        env.caps = caps_;
        env.monitor = monitor_;
        env.audioDevices = devices_;
        env.linkSensitivity = link_;
        schema_ = buildSettingsSchema(applied_, env);
        schema_.revision = ++revision_;
        dirty_ = false;
    }
    return schema_;
}

gameui::SettingsFeedback ModernSettings::apply(const gameui::SettingsAction& a) {
    using K = gameui::SettingsActionKind;
    gameui::SettingsFeedback fb;
    dirty_ = true;
    if (a.kind == K::Restart) {
        std::vector<std::string> reasons;
        std::stringstream ss(a.value);
        for (std::string line; std::getline(ss, line);)
            if (!line.empty())
                reasons.push_back(line);
        host_.requestRestart(reasons);
        return fb;
    }
    Settings pending = applied_;
    auto layerOf = [&](Settings& s) -> BindingLayer& { return a.layer == "menu" ? s.controls.menu : s.controls.game; };
    switch (a.kind) {
        case K::Change:
            if (!applySettingChange(pending, link_, a.key, a.value)) {
                fb.ok = false;
                fb.error = "Unknown setting: " + a.key;
                return fb;
            }
            break;
        case K::Bind:
            bindSlot(layerOf(pending), a.action, a.slot, a.source, a.replace);
            break;
        case K::ClearBinding:
            clearSlot(layerOf(pending), a.action, a.slot);
            break;
        case K::ResetBindings: {
            Settings d = store_.defaults();
            resetLayer(layerOf(pending), layerOf(d), layerOf(applied_));
            break;
        }
        case K::ResetPage:
            if (!resetPageDefaults(pending, store_.defaults(), a.page)) {
                fb.ok = false;
                fb.error = "Unknown settings page: " + a.page;
                return fb;
            }
            if (a.page == "controls")
                link_ = std::fabs(pending.controls.sensX - pending.controls.sensY) < 1e-6f;
            break;
        case K::Restart:
            break;
    }
    const bool save = a.kind != K::Change || a.final;
    ApplyOutcome o = applySettings(host_, store_, caps_, applied_, pending, save);
    if (save && unsaved_ && !o.changed) {
        // The value was already applied live while dragging: write it now.
        std::string err;
        if (!store_.save(applied_, &err))
            o.error += err;
    }
    unsaved_ = !save && (o.changed || unsaved_);
    if (o.needsRestart)
        fb.restartReasons = o.restartReasons;
    while (!o.error.empty() && o.error.back() == '\n')
        o.error.pop_back();
    if (!o.error.empty()) {
        fb.ok = false;
        fb.error = o.error;
    }
    return fb;
}

void ModernSettings::applyAll(const std::vector<gameui::SettingsAction>& actions, gameui::SettingsPages& pages) {
    for (const gameui::SettingsAction& a : actions)
        pages.onApplied(apply(a));
}

} // namespace dsettings
