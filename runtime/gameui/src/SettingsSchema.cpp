#include "gameui/SettingsSchema.h"

#include <cmath>
#include <cstdio>

namespace gameui {

std::string SettingRow::formatValue(float v) const {
    if (!zeroLabel.empty() && v <= 0.0f)
        return zeroLabel;
    char b[96];
    const char* f = format.empty() ? (integer ? "%d" : "%g") : format.c_str();
    if (integer)
        std::snprintf(b, sizeof b, f, static_cast<int>(std::lround(v)));
    else
        std::snprintf(b, sizeof b, f, static_cast<double>(v));
    return b;
}

std::string SettingRow::valueText() const {
    switch (kind) {
        case SettingKind::Toggle:
            return on ? "On" : "Off";
        case SettingKind::Choice:
            if (!currentLabel.empty())
                return currentLabel;
            if (current >= 0 && current < static_cast<int>(choices.size()))
                return choices[static_cast<size_t>(current)].label;
            return "--";
        case SettingKind::Slider:
            return formatValue(value);
        case SettingKind::Info:
            return text;
        case SettingKind::Size:
            return std::to_string(width) + " x " + std::to_string(height);
    }
    return {};
}

std::vector<std::string> BindingLayerView::actionsOn(const std::string& source) const {
    for (const SourceUse& u : sources)
        if (u.source == source)
            return u.actions;
    return {};
}

std::string BindingLayerView::actionLabel(const std::string& actionId) const {
    for (const BindingGroup& g : groups)
        for (const BindingAction& a : g.actions)
            if (a.id == actionId)
                return a.label;
    return actionId;
}

const SettingRow* SettingsSchema::find(const std::string& key) const {
    for (const SettingPage& p : pages)
        for (const SettingSection& s : p.sections)
            for (const SettingRow& r : s.rows)
                if (r.key == key)
                    return &r;
    return nullptr;
}

} // namespace gameui
