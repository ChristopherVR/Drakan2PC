#pragma once
#include <utility>

// Raylib/GLFW host key IDs -> shared input module DirectInput scancodes.
// The browser publishes the same host IDs; bindings and prompt generation then
// consume the same drakan_input.ini on every desktop platform.
inline constexpr std::pair<int, int> kPortableHostKeys[] = {
    {65, 0x1e},  // A
    {66, 0x30},  // B
    {67, 0x2e},  // C
    {68, 0x20},  // D
    {69, 0x12},  // E
    {70, 0x21},  // F
    {71, 0x22},  // G
    {72, 0x23},  // H
    {73, 0x17},  // I
    {74, 0x24},  // J
    {75, 0x25},  // K
    {76, 0x26},  // L
    {77, 0x32},  // M
    {78, 0x31},  // N
    {79, 0x18},  // O
    {80, 0x19},  // P
    {81, 0x10},  // Q
    {82, 0x13},  // R
    {83, 0x1f},  // S
    {84, 0x14},  // T
    {85, 0x16},  // U
    {86, 0x2f},  // V
    {87, 0x11},  // W
    {88, 0x2d},  // X
    {89, 0x15},  // Y
    {90, 0x2c},  // Z
    {48, 0x0b},  // 0
    {49, 0x02},  // 1
    {50, 0x03},  // 2
    {51, 0x04},  // 3
    {52, 0x05},  // 4
    {53, 0x06},  // 5
    {54, 0x07},  // 6
    {55, 0x08},  // 7
    {56, 0x09},  // 8
    {57, 0x0a},  // 9
    {290, 0x3b}, // F1
    {291, 0x3c}, // F2
    {292, 0x3d}, // F3
    {293, 0x3e}, // F4
    {294, 0x3f}, // F5
    {295, 0x40}, // F6
    {296, 0x41}, // F7
    {297, 0x42}, // F8
    {298, 0x43}, // F9
    {299, 0x44}, // F10
    {300, 0x57}, // F11
    {301, 0x58}, // F12
    {320, 0x52}, // Numpad0
    {321, 0x4f}, // Numpad1
    {322, 0x50}, // Numpad2
    {323, 0x51}, // Numpad3
    {324, 0x4b}, // Numpad4
    {325, 0x4c}, // Numpad5
    {326, 0x4d}, // Numpad6
    {327, 0x47}, // Numpad7
    {328, 0x48}, // Numpad8
    {329, 0x49}, // Numpad9
    {32, 0x39},  // Space
    {39, 0x28},  // Apostrophe
    {44, 0x33},  // Comma
    {45, 0x0c},  // Minus
    {46, 0x34},  // Period
    {47, 0x35},  // Slash
    {59, 0x27},  // Semicolon
    {61, 0x0d},  // Equal
    {91, 0x1a},  // Left bracket
    {92, 0x2b},  // Backslash
    {93, 0x1b},  // Right bracket
    {96, 0x29},  // Grave
    {256, 0x01}, // Escape
    {257, 0x1c}, // Enter
    {258, 0x0f}, // Tab
    {259, 0x0e}, // Backspace
    {260, 0xd2}, // Insert
    {261, 0xd3}, // Delete
    {262, 0xcd}, // Right
    {263, 0xcb}, // Left
    {264, 0xd0}, // Down
    {265, 0xc8}, // Up
    {266, 0xc9}, // Page up
    {267, 0xd1}, // Page down
    {268, 0xc7}, // Home
    {269, 0xcf}, // End
    {280, 0x3a}, // Caps lock
    {281, 0x46}, // Scroll lock
    {282, 0x45}, // Num lock
    {283, 0xb7}, // Print screen
    {330, 0x53}, // Numpad decimal
    {331, 0xb5}, // Numpad divide
    {332, 0x37}, // Numpad multiply
    {333, 0x4a}, // Numpad subtract
    {334, 0x4e}, // Numpad add
    {335, 0x9c}, // Numpad enter
    {340, 0x2a}, // Left shift
    {341, 0x1d}, // Left control
    {342, 0x38}, // Left alt
    {343, 0xdb}, // Left super
    {344, 0x36}, // Right shift
    {345, 0x9d}, // Right control
    {346, 0xb8}, // Right alt
    {347, 0xdc}, // Right super
    {348, 0xdd}, // Menu
};
