// The "Mods" page of the PC settings schema (id "mods"), built from a ModsModel. Rows (all keys are routed to
// ModsModel::apply by the UIs, see ModsModel.h): per pack a section with Enabled (toggle), Load position (choice
// 1..N), status / author / target disc / conflict lines (info), plus Open mods folder and Restart now. Platform free.
#pragma once
#include "gameui/SettingsSchema.h"
#include "settings/ModsModel.h"

namespace dsettings {

gameui::SettingPage buildModsPage(const ModsModel& model);

} // namespace dsettings
