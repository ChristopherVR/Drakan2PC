// Drakan: key code tables shared by the GLFW (Linux) and browser hosts, which both deliver GLFW key codes.
#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace drakan_keys {

// GLFW key (US layout position) -> DirectInput scancode (E0 keys = 0x80 | code); 0 = none.
inline int dikFromGlfw(int key) {
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
        return 0x02 + (key - GLFW_KEY_1);
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F10)
        return 0x3B + (key - GLFW_KEY_F1);
    static const unsigned char kLetters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                               0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                               0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return kLetters[key - GLFW_KEY_A];
    switch (key) {
        case GLFW_KEY_ESCAPE:
            return 0x01;
        case GLFW_KEY_0:
            return 0x0B;
        case GLFW_KEY_MINUS:
            return 0x0C;
        case GLFW_KEY_EQUAL:
            return 0x0D;
        case GLFW_KEY_BACKSPACE:
            return 0x0E;
        case GLFW_KEY_TAB:
            return 0x0F;
        case GLFW_KEY_LEFT_BRACKET:
            return 0x1A;
        case GLFW_KEY_RIGHT_BRACKET:
            return 0x1B;
        case GLFW_KEY_ENTER:
            return 0x1C;
        case GLFW_KEY_LEFT_CONTROL:
            return 0x1D;
        case GLFW_KEY_SEMICOLON:
            return 0x27;
        case GLFW_KEY_APOSTROPHE:
            return 0x28;
        case GLFW_KEY_GRAVE_ACCENT:
            return 0x29;
        case GLFW_KEY_LEFT_SHIFT:
            return 0x2A;
        case GLFW_KEY_BACKSLASH:
            return 0x2B;
        case GLFW_KEY_COMMA:
            return 0x33;
        case GLFW_KEY_PERIOD:
            return 0x34;
        case GLFW_KEY_SLASH:
            return 0x35;
        case GLFW_KEY_RIGHT_SHIFT:
            return 0x36;
        case GLFW_KEY_KP_MULTIPLY:
            return 0x37;
        case GLFW_KEY_LEFT_ALT:
            return 0x38;
        case GLFW_KEY_SPACE:
            return 0x39;
        case GLFW_KEY_CAPS_LOCK:
            return 0x3A;
        case GLFW_KEY_NUM_LOCK:
            return 0x45;
        case GLFW_KEY_SCROLL_LOCK:
            return 0x46;
        case GLFW_KEY_KP_7:
            return 0x47;
        case GLFW_KEY_KP_8:
            return 0x48;
        case GLFW_KEY_KP_9:
            return 0x49;
        case GLFW_KEY_KP_SUBTRACT:
            return 0x4A;
        case GLFW_KEY_KP_4:
            return 0x4B;
        case GLFW_KEY_KP_5:
            return 0x4C;
        case GLFW_KEY_KP_6:
            return 0x4D;
        case GLFW_KEY_KP_ADD:
            return 0x4E;
        case GLFW_KEY_KP_1:
            return 0x4F;
        case GLFW_KEY_KP_2:
            return 0x50;
        case GLFW_KEY_KP_3:
            return 0x51;
        case GLFW_KEY_KP_0:
            return 0x52;
        case GLFW_KEY_KP_DECIMAL:
            return 0x53;
        case GLFW_KEY_F11:
            return 0x57;
        case GLFW_KEY_F12:
            return 0x58;
        case GLFW_KEY_KP_ENTER:
            return 0x9C;
        case GLFW_KEY_RIGHT_CONTROL:
            return 0x9D;
        case GLFW_KEY_KP_DIVIDE:
            return 0xB5;
        case GLFW_KEY_RIGHT_ALT:
            return 0xB8;
        case GLFW_KEY_PAUSE:
            return 0xC5;
        case GLFW_KEY_HOME:
            return 0xC7;
        case GLFW_KEY_UP:
            return 0xC8;
        case GLFW_KEY_PAGE_UP:
            return 0xC9;
        case GLFW_KEY_LEFT:
            return 0xCB;
        case GLFW_KEY_RIGHT:
            return 0xCD;
        case GLFW_KEY_END:
            return 0xCF;
        case GLFW_KEY_DOWN:
            return 0xD0;
        case GLFW_KEY_PAGE_DOWN:
            return 0xD1;
        case GLFW_KEY_INSERT:
            return 0xD2;
        case GLFW_KEY_DELETE:
            return 0xD3;
        case GLFW_KEY_LEFT_SUPER:
            return 0xDB;
        case GLFW_KEY_RIGHT_SUPER:
            return 0xDC;
        case GLFW_KEY_MENU:
            return 0xDD;
        default:
            return 0;
    }
}

// Windows virtual-key code (drakan_win::keyDown's identifier) -> GLFW key; -1 = none. Letters and digits are their
// ASCII codes in both.
inline int glfwFromVk(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return vk;
    if (vk >= 0x70 && vk <= 0x87)
        return GLFW_KEY_F1 + (vk - 0x70);
    switch (vk) {
        case 0x08:
            return GLFW_KEY_BACKSPACE;
        case 0x09:
            return GLFW_KEY_TAB;
        case 0x0D:
            return GLFW_KEY_ENTER;
        case 0x10:
            return GLFW_KEY_LEFT_SHIFT;
        case 0x11:
            return GLFW_KEY_LEFT_CONTROL;
        case 0x12:
            return GLFW_KEY_LEFT_ALT;
        case 0x1B:
            return GLFW_KEY_ESCAPE;
        case 0x20:
            return GLFW_KEY_SPACE;
        case 0x25:
            return GLFW_KEY_LEFT;
        case 0x26:
            return GLFW_KEY_UP;
        case 0x27:
            return GLFW_KEY_RIGHT;
        case 0x28:
            return GLFW_KEY_DOWN;
        case 0x2E:
            return GLFW_KEY_DELETE;
        default:
            return -1;
    }
}

} // namespace drakan_keys
