#include "settings/Apply.h"

#include <algorithm>

namespace dsettings {

bool isRestartKey(const HostCaps& caps, const std::string& key) {
    return key == "video.world_rate" || key == "video.renderer" || caps.restartKeys.count(key) != 0;
}

ApplyOutcome applySettings(ISettingsHost& host, SettingsStore& store, const HostCaps& caps, Settings& applied,
                           Settings& pending, bool save) {
    ApplyOutcome o;
    o.before = applied;
    const std::vector<std::string> ch = changedKeys(applied, pending);
    if (ch.empty())
        return o;
    o.changed = true;
    auto add = [&](const std::string& label) {
        if (std::find(o.restartReasons.begin(), o.restartReasons.end(), label) == o.restartReasons.end())
            o.restartReasons.push_back(label);
    };
    bool bindingsChanged = false, promptsChanged = false;
    for (const auto& k : ch) {
        if (isRestartKey(caps, k)) {
            o.needsRestart = true;
            add(settingLabel(k));
        }
        if (k == "video.fullscreen" || k == "video.window_mode" || k == "video.window_width" ||
            k == "video.window_height" || k == "video.scale")
            o.displayRisky = true;
        if (k == "input.bindings" || k == "input.menu_bindings")
            bindingsChanged = true;
        if (k == "general.ui_prompts")
            promptsChanged = true;
    }
    // World rate and renderer are read at startup. Apply other video edits without sending them to the live host.
    VideoSettings liveVideo = pending.video;
    liveVideo.worldRate = applied.video.worldRate;
    liveVideo.renderer = applied.video.renderer;
    if (!(applied.video == liveVideo)) {
        ApplyResult r = host.applyVideo(liveVideo, applied.video);
        if (!r.ok)
            o.error += r.message + "\n";
        if (r.needsRestart) {
            o.needsRestart = true;
            o.restartReasons.push_back("Display settings");
        }
    }
    if (!(applied.audio == pending.audio)) {
        ApplyResult r = host.applyAudio(pending.audio, applied.audio);
        if (!r.ok)
            o.error += r.message + "\n";
        if (r.needsRestart) {
            o.needsRestart = true;
            o.restartReasons.push_back("Audio settings");
        }
    }
    if (!(applied.subtitles == pending.subtitles))
        host.applySubtitles(pending.subtitles, applied.subtitles);
    if (!(applied.general == pending.general)) {
        ApplyResult r = host.applyGeneral(pending.general, applied.general);
        if (!r.ok)
            o.error += r.message + "\n";
        if (r.needsRestart) {
            o.needsRestart = true;
            o.restartReasons.push_back("General settings");
        }
    }
    if (save) {
        std::string serr;
        if (!store.save(pending, &serr))
            o.error += serr;
        if (!(applied.controls == pending.controls)) {
            host.reloadBindings(store.paths().inputIni);
            if ((bindingsChanged || promptsChanged) && pending.general.prompts == PromptStyle::Keyboard)
                host.regeneratePrompts();
        }
        if (promptsChanged && !(applied.general == pending.general))
            host.regeneratePrompts();
        // After the save the store has re-read the files: bindings get fresh docLine indices.
        pending.controls.game = store.loaded().controls.game;
        pending.controls.menu = store.loaded().controls.menu;
    }
    applied = pending;
    return o;
}

void revertVideo(ISettingsHost& host, SettingsStore& store, Settings& applied, Settings& pending,
                 const VideoSettings& to) {
    host.applyVideo(to, pending.video);
    pending.video = to;
    std::string e;
    store.save(pending, &e);
    applied = pending;
}

bool resetPageDefaults(Settings& s, const Settings& d, const std::string& pageId) {
    if (pageId == "display") {
        const VideoSettings keep = s.video;
        s.video = d.video;
        s.video.windowWidth = keep.windowWidth;
        s.video.windowHeight = keep.windowHeight;
        return true;
    }
    if (pageId == "audio") {
        s.audio = d.audio;
        return true;
    }
    if (pageId == "gameplay") {
        s.general = d.general;
        return true;
    }
    if (pageId == "controls") {
        const ControlSettings keepBind = s.controls;
        s.controls = d.controls;
        s.controls.game = keepBind.game;
        s.controls.menu = keepBind.menu; // bindings have their own reset buttons
        return true;
    }
    return false;
}

void bindSlot(BindingLayer& layer, const std::string& action, int slot, const std::string& source, bool replaceOthers) {
    if (replaceOthers)
        layer.clearSource(source); // removes the other actions bound to that source (and this action's binding of it)
    auto cur = layer.sourcesFor(action);
    if (slot >= 0 && slot < static_cast<int>(cur.size())) {
        if (cur[static_cast<size_t>(slot)] != source)
            layer.unbind(action, cur[static_cast<size_t>(slot)]);
    }
    layer.bind(action, source);
}

void clearSlot(BindingLayer& layer, const std::string& action, int slot) {
    auto cur = layer.sourcesFor(action);
    if (slot >= 0 && slot < static_cast<int>(cur.size()))
        layer.unbind(action, cur[static_cast<size_t>(slot)]);
}

void resetLayer(BindingLayer& layer, const BindingLayer& defaults, const BindingLayer& applied) {
    layer = defaults;
    // keep docLine association so the writer can rewrite in place
    for (auto& l : layer.lines) {
        l.docLine = -1;
        for (const auto& o : applied.lines)
            if (o.source == l.source) {
                l.docLine = o.docLine;
                break;
            }
    }
}

} // namespace dsettings
