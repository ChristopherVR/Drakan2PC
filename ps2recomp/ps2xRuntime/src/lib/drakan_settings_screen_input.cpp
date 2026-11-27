// Drakan: the input of the host-drawn screens (Tab screen, pause, title, Save / Load, results): the real keyboard,
// mouse and pad through ImGui and the OS, or a scripted run's events (DRAKAN_INPUT_SCRIPT).
#if defined(PS2X_HAVE_DRAKAN_SETTINGS)
#include "drakan_settings_host_internal.h"

namespace drakan_settings_host::detail {

namespace {
// The modern PC Settings pages' key capture (SettingsPages::capturing()): keyboard keys (and the middle / side mouse
// buttons) are not navigation then but the key to bind, reported in g_capturedKey as its drakan_input.ini name; Esc
// and the pad stay navigation (Esc / B cancel). Set by the screen's draw before it reads the input.
bool g_capture = false;
bool g_textEdit = false; // a multiplayer text field is being edited: typed characters in, letter keys out of navigation
std::string g_capturedKey;

// The real devices: ImGui's keyboard / mouse / pad state, plus the OS key state (see `vk` below).
gameui::UiInput deviceInput(bool take) {
    using gameui::UiKey;
    gameui::UiInput in;
    g_capturedKey.clear();
    if (!take)
        return in;
    ImGuiIO& io = ImGui::GetIO();
    gameui::KeyGate& gate = g_tabUi.gate;
    bool pad = false, kbm = false;
    if (g_capture) {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart && g_capturedKey.empty(); ++key)
            if (key != ImGuiKey_Escape && ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false))
                if (const char* name = dsettings::iniNameOfImGuiKey(key))
                    g_capturedKey = name;
        static const char* const kButtons[] = {"MOUSE3", "MOUSE4", "MOUSE5"};
        for (int b = 2; b <= 4 && g_capturedKey.empty(); ++b)
            if (ImGui::IsMouseClicked(b))
                g_capturedKey = kButtons[b - 2];
        if (!g_capturedKey.empty())
            kbm = true;
    }
    // `vk`: the key's physical state comes from the OS (ImGui's is cleared when the screen takes the input, and the
    // key-down message of the opening press can arrive after that: raw input, which the game reads, runs on its own
    // thread). Pad buttons: ImGui's state is the backend's XInput poll.
    if (g_textEdit) { // typed characters and Backspace for the multiplayer text fields
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const ImWchar ch = io.InputQueueCharacters[i];
            if (ch >= 0x20 && ch < 0x7f)
                in.text.push_back(static_cast<char>(ch));
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true))
            ++in.backspaces;
        if (!in.text.empty() || in.backspaces)
            kbm = true;
    }
    const auto k = [&](ImGuiKey key, int vk, UiKey u, bool repeat) {
        if (g_capture && key < ImGuiKey_GamepadStart && key != ImGuiKey_Escape)
            return; // capturing: the keyboard binds, it does not navigate
        if (g_textEdit && key < ImGuiKey_GamepadStart &&
            ((key >= ImGuiKey_A && key <= ImGuiKey_Z) || key == ImGuiKey_Space || key == ImGuiKey_Backspace ||
             key == ImGuiKey_Delete))
            return; // typing: letters, Space and Backspace belong to the text field
        const bool down = ImGui::IsKeyDown(key) || (vk && drakan_win::keyDown(vk));
        if (gate.accept(static_cast<int>(key), down, ImGui::IsKeyPressed(key, repeat))) {
            in.keys.push_back(u);
            (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown ? pad : kbm) = true;
        }
    };
    k(ImGuiKey_UpArrow, drakan_win::vk::Up, UiKey::Up, true);
    k(ImGuiKey_W, 'W', UiKey::Up, true);
    k(ImGuiKey_GamepadDpadUp, 0, UiKey::Up, true);
    k(ImGuiKey_GamepadLStickUp, 0, UiKey::Up, true);
    k(ImGuiKey_DownArrow, drakan_win::vk::Down, UiKey::Down, true);
    k(ImGuiKey_S, 'S', UiKey::Down, true);
    k(ImGuiKey_GamepadDpadDown, 0, UiKey::Down, true);
    k(ImGuiKey_GamepadLStickDown, 0, UiKey::Down, true);
    k(ImGuiKey_LeftArrow, drakan_win::vk::Left, UiKey::Left, true);
    k(ImGuiKey_A, 'A', UiKey::Left, true);
    k(ImGuiKey_GamepadDpadLeft, 0, UiKey::Left, true);
    k(ImGuiKey_GamepadLStickLeft, 0, UiKey::Left, true);
    k(ImGuiKey_RightArrow, drakan_win::vk::Right, UiKey::Right, true);
    k(ImGuiKey_D, 'D', UiKey::Right, true);
    k(ImGuiKey_GamepadDpadRight, 0, UiKey::Right, true);
    k(ImGuiKey_GamepadLStickRight, 0, UiKey::Right, true);
    k(ImGuiKey_Enter, drakan_win::vk::Enter, UiKey::Confirm, false);
    k(ImGuiKey_KeypadEnter, drakan_win::vk::Enter, UiKey::Confirm, false);
    k(ImGuiKey_Space, drakan_win::vk::Space, UiKey::Confirm, false);
    k(ImGuiKey_GamepadFaceDown, 0, UiKey::Confirm, false);
    k(ImGuiKey_Escape, drakan_win::vk::Escape, UiKey::Back, false);
    k(ImGuiKey_Backspace, drakan_win::vk::Backspace, UiKey::Back, false);
    k(ImGuiKey_GamepadFaceRight, 0, UiKey::Back, false);
    k(ImGuiKey_Tab, drakan_win::vk::Tab, UiKey::Close, false);
    k(ImGuiKey_I, 'I', UiKey::Close, false);
    k(ImGuiKey_M, 'M', UiKey::Close, false);
    k(ImGuiKey_GamepadStart, 0, UiKey::Back, false);
    k(ImGuiKey_GamepadBack, 0, UiKey::Close, false);
    k(ImGuiKey_X, 'X', UiKey::Secondary, false);
    k(ImGuiKey_Delete, drakan_win::vk::Delete, UiKey::Secondary, false);
    k(ImGuiKey_GamepadFaceUp, 0, UiKey::Secondary, false);
    k(ImGuiKey_F, 'F', UiKey::Tertiary, false);
    k(ImGuiKey_GamepadFaceLeft, 0, UiKey::Tertiary, false);
    k(ImGuiKey_Q, 'Q', UiKey::PagePrev, false);
    k(ImGuiKey_GamepadL1, 0, UiKey::PagePrev, false);
    k(ImGuiKey_E, 'E', UiKey::PageNext, false);
    k(ImGuiKey_GamepadR1, 0, UiKey::PageNext, false);
    k(ImGuiKey_Z, 'Z', UiKey::FilterPrev, false);
    k(ImGuiKey_GamepadL2, 0, UiKey::FilterPrev, false);
    k(ImGuiKey_C, 'C', UiKey::FilterNext, false);
    k(ImGuiKey_GamepadR2, 0, UiKey::FilterNext, false);
    const ImVec2 mp = io.MousePos;
    in.pointerValid =
        ImGui::IsMousePosValid(&mp) && mp.x >= 0 && mp.y >= 0 && mp.x < io.DisplaySize.x && mp.y < io.DisplaySize.y;
    in.pointerX = mp.x;
    in.pointerY = mp.y;
    in.pointerMoved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    constexpr int kGateLeft = gameui::KeyGate::kKeys - 2, kGateRight = gameui::KeyGate::kKeys - 1;
    const auto button = [](ImGuiMouseButton b, int vk) { return ImGui::IsMouseDown(b) || drakan_win::keyDown(vk); };
    in.leftDown =
        button(ImGuiMouseButton_Left, drakan_win::vk::LeftButton); // slider drags (a drag starts only on a click)
    in.leftClick = gate.accept(kGateLeft, in.leftDown, ImGui::IsMouseClicked(ImGuiMouseButton_Left));
    in.doubleClick = in.leftClick && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    in.rightClick = gate.accept(kGateRight, button(ImGuiMouseButton_Right, drakan_win::vk::RightButton),
                                ImGui::IsMouseClicked(ImGuiMouseButton_Right));
    in.wheel = io.MouseWheel;
    if (in.pointerMoved || in.leftClick || in.rightClick || in.wheel != 0)
        kbm = true;
    if (pad)
        g_tabUi.device = gameui::InputDevice::Gamepad;
    else if (kbm)
        g_tabUi.device = gameui::InputDevice::KeyboardMouse;
    in.device = g_tabUi.device;
    if (in.device == gameui::InputDevice::Gamepad)
        in.pointerMoved = false; // a resting mouse must not steal the pad's focus
    return in;
}

// Scripted runs (DRAKAN_INPUT_SCRIPT): the script's edges, queued by the input module while the host screen is up.
gameui::UiInput scriptInput(bool take, float bbW, float bbH) {
    using drakan_input::InputModule;
    using gameui::UiKey;
    static float px = -1, py = -1, clickX = 0, clickY = 0;
    static double clickT = -10;
    gameui::UiInput in;
    g_capturedKey.clear();
    const auto events = drakan_input::module().drainHostScreenEvents();
    // The same KeyGate rule as for the real input, on the script's own up / down state (ids: keys 0..255, mouse
    // buttons 256 + code, pad 300 + bit).
    static std::bitset<gameui::KeyGate::kKeys> held;
    gameui::KeyGate& gate = g_tabUi.gate;
    const auto gateId = [](const InputModule::HostScreenEvent& e) {
        switch (e.kind) {
            case InputModule::HostScreenEvent::Key:
                return e.code & 255;
            case InputModule::HostScreenEvent::MouseButton:
                return 256 + (e.code & 31);
            case InputModule::HostScreenEvent::Pad: {
                int bit = 0;
                while (bit < 16 && !((e.code >> bit) & 1))
                    ++bit;
                return 300 + bit;
            }
            default:
                return -1;
        }
    };
    const auto counts = [&](const InputModule::HostScreenEvent& e) {
        const int id = gateId(e);
        if (id < 0)
            return true;
        const bool was = held[size_t(id)];
        held[size_t(id)] = e.down;
        if (!e.down) {
            gate.accept(id, false, false);
            return false;
        }
        gate.accept(id, was, false); // the state before this press: held over or released
        return take && gate.accept(id, true, true);
    };
    if (!take) {
        for (const InputModule::HostScreenEvent& e : events)
            (void)counts(e);
        return in;
    }
    bool pad = false, kbm = false;
    for (const InputModule::HostScreenEvent& e : events) {
        if (e.kind != InputModule::HostScreenEvent::Cursor && !counts(e))
            continue;
        switch (e.kind) {
            case InputModule::HostScreenEvent::Cursor:
                if (e.clientW > 0 && e.clientH > 0) {
                    px = float(e.x) * bbW / float(e.clientW);
                    py = float(e.y) * bbH / float(e.clientH);
                    in.pointerMoved = kbm = true;
                }
                break;
            case InputModule::HostScreenEvent::Key: {
                if (!e.down)
                    break;
                if (g_capture && e.code != 0x01) { // capturing: the key to bind (Esc still cancels)
                    if (const char* name = drakan_input::nameOfKey(e.code); name && g_capturedKey.empty())
                        g_capturedKey = name;
                    kbm = true;
                    break;
                }
                struct M {
                    int dik;
                    UiKey key;
                };
                static const M map[] = {{0xC8, UiKey::Up},         {0x11, UiKey::Up},        {0xD0, UiKey::Down},
                                        {0x1F, UiKey::Down},       {0xCB, UiKey::Left},      {0x1E, UiKey::Left},
                                        {0xCD, UiKey::Right},      {0x20, UiKey::Right},     {0x1C, UiKey::Confirm},
                                        {0x9C, UiKey::Confirm},    {0x39, UiKey::Confirm},   {0x01, UiKey::Back},
                                        {0x0E, UiKey::Back},       {0x0F, UiKey::Close},     {0x17, UiKey::Close},
                                        {0x32, UiKey::Close},      {0x2D, UiKey::Secondary}, {0xD3, UiKey::Secondary},
                                        {0x21, UiKey::Tertiary},   {0x10, UiKey::PagePrev},  {0x12, UiKey::PageNext},
                                        {0x2C, UiKey::FilterPrev}, {0x2E, UiKey::FilterNext}};
                for (const M& m : map)
                    if (m.dik == e.code) {
                        in.keys.push_back(m.key);
                        kbm = true;
                    }
                break;
            }
            case InputModule::HostScreenEvent::MouseButton:
                if (!e.down)
                    break;
                kbm = true;
                if (e.code == drakan_input::MBTN_LEFT) {
                    in.leftClick = true;
                    const double t = ImGui::GetTime();
                    in.doubleClick = t - clickT < 0.5 && std::fabs(px - clickX) < 8 && std::fabs(py - clickY) < 8;
                    clickT = in.doubleClick ? -10 : t;
                    clickX = px;
                    clickY = py;
                } else if (e.code == drakan_input::MBTN_RIGHT) {
                    in.rightClick = true;
                } else if (g_capture && g_capturedKey.empty()) {
                    g_capturedKey = e.code == drakan_input::MBTN_MIDDLE ? "MOUSE3"
                                    : e.code == drakan_input::MBTN_X1   ? "MOUSE4"
                                                                        : "MOUSE5";
                }
                break;
            case InputModule::HostScreenEvent::Pad: {
                if (!e.down)
                    break;
                namespace P = drakan_input::Pad;
                struct M {
                    int bit;
                    UiKey key;
                };
                static const M map[] = {{P::UP, UiKey::Up},
                                        {P::DOWN, UiKey::Down},
                                        {P::LEFT, UiKey::Left},
                                        {P::RIGHT, UiKey::Right},
                                        {P::CROSS, UiKey::Confirm},
                                        {P::CIRCLE, UiKey::Back},
                                        {P::TRIANGLE, UiKey::Secondary},
                                        {P::SQUARE, UiKey::Tertiary},
                                        {P::L1, UiKey::PagePrev},
                                        {P::R1, UiKey::PageNext},
                                        {P::L2, UiKey::FilterPrev},
                                        {P::R2, UiKey::FilterNext},
                                        {P::SELECT, UiKey::Close},
                                        {P::START, UiKey::Back}};
                for (const M& m : map)
                    if (m.bit == e.code) {
                        in.keys.push_back(m.key);
                        pad = true;
                    }
                break;
            }
        }
    }
    in.leftDown = held[size_t(256 + (drakan_input::MBTN_LEFT & 31))]; // slider drags
    in.pointerValid = px >= 0 && py >= 0;
    in.pointerX = px;
    in.pointerY = py;
    if (pad)
        g_tabUi.device = gameui::InputDevice::Gamepad;
    else if (kbm)
        g_tabUi.device = gameui::InputDevice::KeyboardMouse;
    in.device = g_tabUi.device;
    if (in.device == gameui::InputDevice::Gamepad)
        in.pointerMoved = false;
    return in;
}

} // namespace

gameui::UiInput hostScreenInput(bool take, bool capture, float w, float h, bool textEdit) {
    g_capture = capture;
    g_textEdit = textEdit;
    gameui::UiInput in = drakan_input::module().scripted() ? scriptInput(take, w, h) : deviceInput(take);
    g_capture = false;
    g_textEdit = false;
    return in;
}

const std::string& capturedKey() {
    return g_capturedKey;
}
} // namespace drakan_settings_host::detail
#endif // PS2X_HAVE_DRAKAN_SETTINGS
