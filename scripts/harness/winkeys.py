"""Real keyboard input for a game window on Windows (SendInput), for what the game's input script cannot do: the
function keys the multiplayer HUD reads from the OS (F4 scoreboard, F6 chat) and typed characters (the ImGui char queue
fills from WM_CHAR, the input script only has DirectInput-style key names).

    from winkeys import find_window, focus, tap, hold, type_text
    hwnd = find_window(pid)
    focus(hwnd); tap(VK_F6); type_text("hello"); tap(VK_RETURN)

Keys go to whatever window has the foreground, so `focus` first (it uses the AttachThreadInput trick that lets a
background console process take the foreground). Characters are sent as KEYEVENTF_UNICODE events (one WM_CHAR each),
no keyboard layout involved. Windows only.
"""

from __future__ import annotations

import ctypes
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

VK_RETURN, VK_ESCAPE, VK_F4, VK_F6, VK_BACK, VK_MENU = 0x0D, 0x1B, 0x73, 0x75, 0x08, 0x12

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP, KEYEVENTF_UNICODE, KEYEVENTF_SCANCODE = 0x2, 0x4, 0x8
ULONG_PTR = ctypes.c_size_t


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class _U(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def _send(vk: int = 0, scan: int = 0, flags: int = 0) -> None:
    inp = INPUT(type=INPUT_KEYBOARD, ki=KEYBDINPUT(vk, scan, flags, 0, 0))
    if user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT)) != 1:
        raise OSError(ctypes.get_last_error(), "SendInput failed")


def _scan(vk: int) -> int:
    return user32.MapVirtualKeyW(vk, 0)  # MAPVK_VK_TO_VSC


def key_down(vk: int) -> None:
    _send(vk, _scan(vk), 0)


def key_up(vk: int) -> None:
    _send(vk, _scan(vk), KEYEVENTF_KEYUP)


def tap(vk: int, ms: int = 60) -> None:
    key_down(vk)
    time.sleep(ms / 1000)
    key_up(vk)


def hold(vk: int, seconds: float) -> None:
    key_down(vk)
    time.sleep(seconds)
    key_up(vk)


def type_text(text: str, gap: float = 0.1) -> None:
    """One WM_CHAR per character (KEYEVENTF_UNICODE), like a keyboard would deliver."""
    for ch in text:
        code = ord(ch)
        _send(0, code, KEYEVENTF_UNICODE)
        _send(0, code, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP)
        time.sleep(gap)


def find_window(pid: int, timeout: float = 30.0) -> int:
    """The first visible top-level window of the process (the game window), waiting for it to appear."""
    found: list[int] = []
    enum_proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _lparam):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd) and user32.GetWindow(hwnd, 4) == 0:  # GW_OWNER
            found.append(hwnd)
            return False
        return True

    end = time.time() + timeout
    while time.time() < end:
        found.clear()
        user32.EnumWindows(enum_proc(cb), 0)
        if found:
            return found[0]
        time.sleep(0.25)
    raise TimeoutError(f"no window for pid {pid}")


def focus(hwnd: int, tries: int = 6) -> bool:
    """Bring the window to the foreground; True when it got it (retried: another process may hold the foreground lock)."""
    for _ in range(tries):
        if user32.GetForegroundWindow() == hwnd:
            return True
        fg = user32.GetForegroundWindow()
        cur = kernel32.GetCurrentThreadId()
        fg_thread = user32.GetWindowThreadProcessId(fg, None)
        user32.AttachThreadInput(cur, fg_thread, True)
        try:
            key_down(VK_MENU)  # an Alt tap lifts the foreground lock
            key_up(VK_MENU)
            user32.ShowWindow(hwnd, 9)  # SW_RESTORE
            user32.SetForegroundWindow(hwnd)
            user32.BringWindowToTop(hwnd)
            user32.SwitchToThisWindow(hwnd, True)
        finally:
            user32.AttachThreadInput(cur, fg_thread, False)
        time.sleep(0.4)
    return user32.GetForegroundWindow() == hwnd


def capture(hwnd: int, path) -> bool:
    """Save the desktop pixels under the window to a PNG (needs Pillow; the window must be on top, see `focus`).
    The game's own `shot` lines capture the renderer, this also shows what the OS shows (and works from the thread
    that knows when something happened)."""
    try:
        from PIL import ImageGrab
    except ImportError:
        return False
    try:  # window rectangles must be in the same (physical) pixels as the grab
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except (OSError, AttributeError):
        pass
    rect = wintypes.RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
        return False
    ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True).save(str(path))
    return True
