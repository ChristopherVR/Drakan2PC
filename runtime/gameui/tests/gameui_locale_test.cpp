#include "gameui/MenuText.h"
#include "gameui/PauseScreen.h"
#include "gameui/SaveLoadScreen.h"
#include "gameui/TitleScreen.h"
#include "gameui/UiSkin.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace gameui;
namespace {
bool hasText(const DrawList& draw, const std::string& text) {
    for (const DrawCmd& command : draw.commands())
        if (command.kind == DrawKind::Text && command.text == text)
            return true;
    return false;
}
void validDraw(const DrawList& draw) {
    for (const DrawCmd& command : draw.commands()) {
        if (command.kind != DrawKind::Text)
            continue;
        assert(command.text.find("[Missing menu text]") == std::string::npos);
        assert(command.text.find("{0}") == std::string::npos);
        assert(command.rect.w >= 0 && command.rect.h >= 0);
    }
}
UiInput click(const std::vector<HitTarget>& targets, TargetId id) {
    for (const HitTarget& target : targets) {
        if (target.id == id) {
            UiInput input;
            input.pointerValid = input.pointerMoved = input.leftClick = input.leftDown = true;
            input.pointerX = target.rect.cx();
            input.pointerY = target.rect.cy();
            return input;
        }
    }
    assert(false);
    return {};
}
UiInput confirm() {
    UiInput input;
    input.keys = {UiKey::Confirm};
    return input;
}
} // namespace

int main() {
    struct Expected {
        const char* language;
        const char* load;
        const char* save;
        const char* resume;
        const char* quit;
    };
    const Expected locales[] = {
        {"english", "Load Game", "Save Game", "Resume", "Quit the game?"},
        {"german", "Spiel laden", "Spiel speichern", "Fortsetzen", "Spiel beenden?"},
        {"french", "Charger une partie", "Sauvegarder", "Reprendre", "Quitter le jeu ?"},
        {"italian", "Carica partita", "Salva partita", "Riprendi", "Uscire dal gioco?"},
        {"spanish", "Cargar partida", "Guardar partida", "Reanudar", "¿Salir del juego?"},
    };
    for (const auto& expected : locales) {
        setMenuLanguage(expected.language);
        for (const Viewport viewport :
             {Viewport{0, 0, 1024, 768}, Viewport{0, 0, 1920, 1080}, Viewport{0, 0, 3440, 1440}}) {
            TitleScreen title;
            title.open();
            TitleState titleState;
            auto titleOut = title.frame(titleState, {}, viewport);
            assert(hasText(titleOut.draw, expected.load));
            validDraw(titleOut.draw);
            titleOut = title.frame(
                titleState, click(title.targets(), {TargetKind::TitleRow, static_cast<int>(TitleRow::Quit)}), viewport);
            assert(title.page() == TitlePage::QuitConfirm && hasText(titleOut.draw, expected.quit));
            assert(hasText(titleOut.draw, menuText("Cancel")));
            validDraw(titleOut.draw);

            { // the new mod packs card and the game choice note are translated like the rest of the title
                TitleScreen notice;
                notice.open();
                TitleState noticeState;
                noticeState.newMods = {"Pack A", "Pack B"};
                noticeState.newModCampaigns = 1;
                const auto noticeOut = notice.frame(noticeState, {}, viewport);
                assert(notice.page() == TitlePage::ModsNotice);
                assert(hasText(noticeOut.draw, menuText("New mod packs found")));
                assert(hasText(noticeOut.draw, menuText("Enable and restart")));
                assert(hasText(noticeOut.draw, menuText("Keep them off")));
                validDraw(noticeOut.draw);
                if (std::string(expected.language) == "german") {
                    assert(hasText(noticeOut.draw, "Neue Mod-Pakete gefunden"));
                    assert(hasText(noticeOut.draw, "Aktivieren und neu starten"));
                    assert(hasText(noticeOut.draw, "Mod-Menü öffnen"));
                    assert(
                        menuFormat("{0} is enabled: restart the game to choose it on the title screen.", {"Pack A"}) ==
                        "Pack A ist aktiviert: Starte das Spiel neu, um es im Titelbildschirm zu wählen.");
                }
                for (const char* key :
                     {"In the mods folder: {0}.", "{0} new packs in the mods folder: {1}.", "Open Mods menu", "Later",
                      "New mod pack found",
                      "New packs stay off until you enable them. They take effect after a restart.",
                      "A campaign pack adds its game to the Choose your game screen.",
                      "Choose your game is not shown: drakan.ini sets game = {1}. Set game = ask to choose {0}.",
                      "Choose your game is not shown: DRAKAN_GAME is set to {1}. Clear it to choose {0}.",
                      "{0} is enabled: restart the game to choose it on the title screen.",
                      "{0} is enabled but was not loaded, so its campaign is not offered.",
                      "{0} is loaded, but its campaign could not be offered."})
                    assert(std::string(menuText(key)) != "[Missing menu text]");
            }

            PauseConfig pauseConfig;
            pauseConfig.hostPages = true;
            pauseConfig.quitCard = true;
            PauseScreen pause(pauseConfig);
            pause.open();
            PauseState pauseState;
            pauseState.location = "Back"; // Guest/user text must never be used as a translation key.
            auto pauseOut = pause.frame({}, pauseState, {}, viewport);
            assert(hasText(pauseOut.draw, expected.resume));
            assert(hasText(pauseOut.draw, expected.save));
            assert(hasText(pauseOut.draw, "Back"));
            validDraw(pauseOut.draw);

            pauseOut =
                pause.frame({}, pauseState,
                            click(pause.targets(), {TargetKind::MenuRow, static_cast<int>(PauseRow::Quit)}), viewport);
            assert(pause.page() == PausePage::QuitConfirm && hasText(pauseOut.draw, expected.quit));
            assert(hasText(pauseOut.draw, menuText("Quit to Title")));
            validDraw(pauseOut.draw);
            pause.open();
            pause.frame({}, pauseState, {}, viewport);
            pause.frame({}, pauseState,
                        click(pause.targets(), {TargetKind::MenuRow, static_cast<int>(PauseRow::Options)}), viewport);
            pauseOut = pause.frame({}, pauseState,
                                   click(pause.targets(), {TargetKind::MenuRow, static_cast<int>(PauseRow::Defaults)}),
                                   viewport);
            assert(pause.page() == PausePage::DefaultsConfirm);
            assert(hasText(pauseOut.draw, menuText("Restore default settings?")));
            validDraw(pauseOut.draw);

            SaveSlotsState slots;
            slots.pageCount = 2;
            slots.slots.resize(4);
            for (int i = 0; i < 4; ++i) {
                slots.slots[i].number = i + 1;
                slots.slots[i].state = SlotState::Empty;
            }
            slots.slots[0].state = SlotState::Used;
            slots.slots[0].location = "Back";
            slots.nativeRow = 0;
            slots.inGame = true;
            SaveLoadScreen saves;
            saves.open(SaveLoadMode::Save, SaveLoadContext::Pause);
            auto savesOut = saves.frame(slots, {}, viewport);
            assert(hasText(savesOut.draw, expected.save));
            assert(hasText(savesOut.draw, "Back"));
            assert(hasText(savesOut.draw, menuFormat("Page {0} of {1}", {"1", "2"})));
            validDraw(savesOut.draw);
            savesOut = saves.frame(slots, confirm(), viewport);
            assert(saves.card() == SaveCard::Overwrite);
            assert(hasText(savesOut.draw, menuText("Overwrite this save?")));
            validDraw(savesOut.draw);
            saves.open(SaveLoadMode::Load, SaveLoadContext::Pause);
            saves.frame(slots, {}, viewport);
            savesOut = saves.frame(slots, confirm(), viewport);
            assert(saves.card() == SaveCard::Load);
            assert(hasText(savesOut.draw, menuText("Load this game?")));
            validDraw(savesOut.draw);
            slots.card = CardState::NoCard;
            saves.reactivate();
            savesOut = saves.frame(slots, {}, viewport);
            assert(hasText(savesOut.draw, menuText("No memory card")));
            validDraw(savesOut.draw);
            slots.card = CardState::Unformatted;
            savesOut = saves.frame(slots, {}, viewport);
            assert(hasText(savesOut.draw, menuText("Memory card not formatted")));
            validDraw(savesOut.draw);
            slots.card = CardState::Reading;
            savesOut = saves.frame(slots, {}, viewport);
            assert(hasText(savesOut.draw, menuText("Reading the memory card...")));
            validDraw(savesOut.draw);
        }
        assert(menuFormat("Save in slot {0}?", {"7"}).find("7") != std::string::npos);
        const std::string arbitrary = "Back {0} Optionen";
        assert(menuFormat("Slot {0} ({1}) is replaced by your current progress.", {"7", arbitrary}).find(arbitrary) !=
               std::string::npos);
    }
    setMenuLanguage("GERMAN");
    assert(menuLanguage() == MenuLanguage::German);
    assert(std::string(menuText("Overwrite")) == "Überschreiben");
    // Explicit skin strings remain verbatim, even when they equal an English catalog key.
    UiSkin skin;
    parseUiSkin("[strings]\ntitle.row.new_game = Back\n", skin);
    setUiSkin(skin);
    assert(std::string(titleRowLabel(TitleRow::NewGame)) == "Back");
    setUiSkin({});
    for (const auto* invalid : {"", "japanese", "de"}) {
        setMenuLanguage(invalid);
        assert(menuLanguage() == MenuLanguage::English);
        assert(std::string(menuText("Load Game")) == "Load Game");
    }
    std::cout << "Modern menu locale tests passed\n";
}
