// A data-only description of a settings UI: pages of sections of rows (toggle, choice, slider, info, size), each with
// its label, help text, choices / range / units, "restart required" and enabled state, plus read-only views of the
// key binding layers. One schema drives both the F1 overlay (dsettings::SettingsMenu, ImGui) and the modern pages
// (gameui::SettingsPages); dsettings::buildSettingsSchema (runtime/settings/include/settings/Schema.h) produces it
// from the settings model, so a setting added there shows up in both. std only, platform free.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gameui {

enum class SettingKind : uint8_t {
    Toggle, // on / off
    Choice, // one of `choices` (a combo in the overlay, "<  value  >" plus a list card in the modern pages)
    Slider, // a number in [min, max]
    Info,   // read-only status text (no value)
    Size,   // a width x height pair (the overlay's custom window size)
};

// Which UI shows a row: some rows only make sense in one of them (the overlay's own menu size, its free-form size
// field; the modern pages' "same vertical sensitivity" switch, which the overlay draws as a check box in the row).
enum class SettingScope : uint8_t { Both, OverlayOnly, ModernOnly };

struct SettingChoice {
    std::string value; // what a SettingChange carries (the ini spelling: "borderless", "16:9", "auto", "60" ...)
    std::string label; // what the user reads
    bool enabled = true;
};

struct SettingRow {
    std::string key; // "video.vsync", "input.mouse.sensitivity_x" (= dsettings::changedKeys names), or a pseudo key
    SettingKind kind = SettingKind::Toggle;
    SettingScope scope = SettingScope::Both;
    std::string label;
    std::string help;                                        // one or two sentences; tooltip / help panel
    std::string unavailable = "not available in this build"; // help text while !enabled (the overlay's tooltip)
    std::string needs; // what a dependent row waits for ("Needs modern water."): the modern pages' note while !enabled
    bool enabled = true;
    bool restart = false; // takes effect after a restart (amber "*")
    bool confirm = false; // a risky change (window mode / size / render scale): "keep these settings?" with a revert
    std::string currentValue; // the current value as a SettingChange would carry it (revert target, held values)

    // Toggle
    bool on = false;
    std::string onValue = "1", offValue = "0";

    // Choice
    std::vector<SettingChoice> choices;
    int current = -1;         // index into choices, -1 = none matches
    std::string currentLabel; // shown for the current value (may differ from the list label; "Custom" when -1)
    std::string emptyText;    // shown inside the list when there are no choices ("none available")

    // Slider
    float value = 0, min = 0, max = 1;
    float step = 0;           // keyboard / pad step in value units; 0 = 1/20 of the range (log scale: of the track)
    bool integer = false;     // integer values ("%d" formats)
    bool logarithmic = false; // the track maps log(value)
    std::string format;       // printf with one number: "%.0f degrees", "%+.2f", "%d%%"
    std::string zeroLabel;    // shown instead of the number at <= 0 ("Original")
    std::string linkLabel;    // overlay: a check box in the row ("Same as horizontal"); when `linked` no slider
    std::string linkKey;      // the pseudo key the check box sets
    bool linked = false;

    // Size
    int width = 0, height = 0, minWidth = 0, minHeight = 0, maxWidth = 0, maxHeight = 0;

    // Info
    std::string text;

    bool shownIn(bool modern) const {
        return scope == SettingScope::Both ||
               (modern ? scope == SettingScope::ModernOnly : scope == SettingScope::OverlayOnly);
    }
    // The text of the current value ("On", "16:9 widescreen", "80 degrees").
    std::string valueText() const;
    // A slider value formatted with `format` / `zeroLabel`.
    std::string formatValue(float v) const;
};

struct SettingSection {
    std::string title;
    std::vector<SettingRow> rows;
};

struct SettingPage {
    std::string id;          // "display", "audio", "controls", "gameplay"
    std::string title;       // "Display"
    std::string description; // one line (the modern landing page's help panel)
    std::vector<SettingSection> sections;
    std::string note; // a remark under the page (Audio: where music / effects volume live)
};

// ---- key bindings (read from the settings, edited through SettingsAction Bind / ClearBinding / ResetBindings) ----
struct BindingSlot {
    std::string source;                   // ini input name ("LCONTROL", "MOUSE1"); empty = unbound
    std::string display;                  // "Left Ctrl"
    std::vector<std::string> alsoBoundTo; // labels of the other actions on the same source (a conflict when any)
    std::string reserved;                 // why the port reserves this key (F1, F11, the capture key), else empty
};
struct BindingAction {
    std::string id; // TRIANGLE, LSTICK_UP, ACT:0x02 ...
    std::string label;
    std::string hint;
    std::vector<BindingSlot>
        slots; // the bound sources, in file order (the UI shows up to SettingsSchema::bindingSlots)
};
struct BindingGroup {
    std::string title;
    std::vector<BindingAction> actions;
};
struct BindingLayerView {
    std::string id; // "game" ([bindings]) or "menu" ([menu_bindings])
    std::string title;
    std::string description;
    std::vector<BindingGroup> groups;
    // Every source of the layer with the actions on it (action ids): conflict checks while capturing.
    struct SourceUse {
        std::string source;
        std::vector<std::string> actions;
    };
    std::vector<SourceUse> sources;
    std::vector<std::string> actionsOn(const std::string& source) const;
    std::string actionLabel(const std::string& actionId) const;
};

struct SettingsSchema {
    std::vector<SettingPage> pages;
    std::vector<BindingLayerView> bindings;
    int bindingSlots = 3;
    uint32_t revision = 0; // bumped by the producer whenever the values change (lets a UI drop its held values)

    const SettingRow* find(const std::string& key) const;
};

} // namespace gameui
