// The settings apply path shared by the F1 overlay (SettingsMenu's Apply / OK) and the modern pages
// (dsettings::ModernSettings): live apply through ISettingsHost, save through SettingsStore, restart bookkeeping, page
// defaults and the key binding edits. Platform free (std only).
#pragma once
#include <string>
#include <vector>

#include "settings/Host.h"
#include "settings/Store.h"
#include "settings/Types.h"

namespace dsettings {

struct ApplyOutcome {
    bool changed = false;                    // anything differed
    bool needsRestart = false;               // a restart key changed, or a host apply reported needsRestart
    std::vector<std::string> restartReasons; // labels of what needs the restart (settingLabel, or "Display settings")
    std::string error;                       // host / save problems, newline separated
    bool displayRisky = false;               // window mode / size / render scale changed: offer a timed revert
    Settings before;                         // `applied` before this apply (the revert target)
};

// video.world_rate always needs a restart (read at startup), the other keys when they are in caps.restartKeys.
bool isRestartKey(const HostCaps& caps, const std::string& key);

// Applies `pending` over `applied`: the host's live apply* calls (world rate is never sent live), the minimal-diff
// save (`save`), the bindings reload and prompt regeneration. Afterwards `applied` = `pending`, and both carry the
// re-read binding line numbers. With `save` false (a slider being dragged) only the live part runs; a later saving
// apply writes everything that differs from the files.
ApplyOutcome applySettings(ISettingsHost& host, SettingsStore& store, const HostCaps& caps, Settings& applied,
                           Settings& pending, bool save = true);

// The "keep display settings?" revert: puts `to` back live and in the files.
void revertVideo(ISettingsHost& host, SettingsStore& store, Settings& applied, Settings& pending,
                 const VideoSettings& to);

// A page's defaults ("display", "audio", "controls", "gameplay"), as the overlay's "Defaults for this page": the
// window size is kept, the bindings have their own reset. False for an unknown page id.
bool resetPageDefaults(Settings& s, const Settings& defaults, const std::string& pageId);

// Key binding edits on a layer (the overlay's capture / clear / reset):
// bind `source` to `action` in slot `slot` (replacing the slot's old source; slot >= the bound count adds), with
// `replaceOthers` first removing `source` from every other action ("Replace"), else sharing it ("Use for both").
void bindSlot(BindingLayer& layer, const std::string& action, int slot, const std::string& source, bool replaceOthers);
void clearSlot(BindingLayer& layer, const std::string& action, int slot);
// Back to `defaults`, keeping the file line of each source that `applied` already has (in-place rewrite).
void resetLayer(BindingLayer& layer, const BindingLayer& defaults, const BindingLayer& applied);

} // namespace dsettings
