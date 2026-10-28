#include "input/Script.h"
#include "input/Config.h"
#include <fstream>
#include <sstream>

namespace drakan_input {

bool InputScript::load(const std::string& path, std::string* err) {
    std::ifstream f(path);
    if (!f) {
        if (err)
            *err += "cannot open " + path + "\n";
        return false;
    }
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        size_t h = line.find('#');
        if (h != std::string::npos)
            line.resize(h);
        std::istringstream ss(line);
        auto bad = [&](const char* m) {
            if (err)
                *err += "line " + std::to_string(ln) + ": " + m + "\n";
        };
        std::string header;
        if (ss >> header && header == "clock") {
            std::string units, extra;
            if (!(ss >> units) || units != "milliseconds" || (ss >> extra)) {
                bad("clock milliseconds");
            } else if (milliseconds_ || !evs_.empty()) {
                bad("clock must appear once before events");
            } else {
                milliseconds_ = true;
            }
            continue;
        }
        ss.clear();
        ss.str(line);
        Ev e;
        std::string cmd;
        if (!(ss >> e.frame >> cmd))
            continue;
        auto mode = [&](const std::string& m) { return m == "down" ? 1 : m == "up" ? 0 : m == "tap" ? 2 : -1; };
        std::string a, b;
        if (cmd == "key") {
            ss >> a >> b;
            e.kind = Ev::Key;
            e.mode = mode(a);
            if (e.mode < 0 || !keyFromName(b, &e.code)) {
                bad("key <down|up|tap> <NAME>");
                continue;
            }
        } else if (cmd == "button") {
            ss >> a >> e.code;
            e.kind = Ev::Button;
            e.mode = mode(a);
            if (e.mode < 0 || e.code < 1 || e.code > 5) {
                bad("button <down|up|tap> <1..5>");
                continue;
            }
            e.code = 1 << (e.code - 1);
        } else if (cmd == "mouse") {
            e.kind = Ev::Mouse;
            if (!(ss >> e.a >> e.b)) {
                bad("mouse <dx> <dy> [frames]");
                continue;
            }
            if (!(ss >> e.n))
                e.n = 1;
        } else if (cmd == "wheel") {
            e.kind = Ev::Wheel;
            if (!(ss >> e.a)) {
                bad("wheel <n>");
                continue;
            }
        } else if (cmd == "pad") {
            ss >> a >> b;
            uint16_t m = 0;
            e.kind = Ev::Pad;
            e.mode = mode(a);
            if (e.mode < 0 || !padButtonFromName(b, &m)) {
                bad("pad <down|up|tap> <BUTTON>");
                continue;
            }
            e.code = m;
        } else if (cmd == "cursor") {
            e.kind = Ev::Cursor;
            if (!(ss >> e.a >> e.b)) {
                bad("cursor <x> <y>");
                continue;
            }
        } else if (cmd == "dump") {
            e.kind = Ev::Dump;
            ss >> e.text;
            if (e.text.empty())
                e.text = "dump";
        } else if (cmd == "shot") {
            e.kind = Ev::Shot;
            ss >> e.text;
            if (e.text.empty())
                e.text = "shot";
        } else if (cmd == "log") {
            e.kind = Ev::Log;
            ss >> a;
            e.a = a == "on" ? 1 : 0;
            int every = 1;
            if (ss >> every && every > 0 && e.a)
                e.a = every;
        } else if (cmd == "menu") {
            e.kind = Ev::Menu;
            ss >> a;
            e.a = a == "on" ? 1 : a == "off" ? 0 : -1;
        } else if (cmd == "dual_wield") {
            ss >> a;
            if (a != "on" && a != "off") {
                bad("dual_wield on|off");
                continue;
            }
            e.kind = Ev::DualWield;
            e.a = a == "on" ? 1 : 0;
        } else if (cmd == "sync") {
            e.kind = Ev::Sync;
            ss >> a;
            if (a == "play") {
                if (!(ss >> e.a)) {
                    bad("sync play <n>");
                    continue;
                }
            } else if (a == "screen" && ss >> e.code) {
                e.b = 1; // e.code = screen id, e.a = consecutive samples
                if (!(ss >> e.a) || e.a < 1)
                    e.a = 1;
            } else {
                bad("sync play <n> | sync screen <id> [n]");
                continue;
            }
        } else if (cmd == "quit") {
            e.kind = Ev::Quit;
        } else {
            bad("unknown command");
            continue;
        }
        evs_.push_back(e);
    }
    loaded_ = true;
    return true;
}

void InputScript::advance(int frame, int playRun, FrameInput& out, uint16_t& padButtons, ScriptEvents& ev,
                          std::optional<uint64_t> monotonicMs) {
    if (milliseconds_ && monotonicMs) {
        if (!timeBase_) {
            timeBase_ = *monotonicMs;
            lastMs_ = *monotonicMs;
        }
        if (*monotonicMs > lastMs_)
            lastMs_ = *monotonicMs;
    }
    uint64_t elapsedMs = timeBase_ ? lastMs_ - *timeBase_ : 0;
    // one-frame taps of the previous frame expire
    keys_ &= ~tapKeys_;
    mouse_ = static_cast<uint8_t>(mouse_ & ~tapMouse_);
    pad_ = static_cast<uint16_t>(pad_ & ~tapPad_);
    tapKeys_.reset();
    tapMouse_ = 0;
    tapPad_ = 0;

    int wheel = 0;
    auto due = [&](const Ev& e) {
        return milliseconds_ ? e.frame <= 0 || static_cast<uint64_t>(e.frame) <= elapsedMs
                             : e.frame <= frame - segBase_;
    };
    while ((!milliseconds_ || monotonicMs) && next_ < evs_.size() && (waiting_ || due(evs_[next_]))) {
        if (waiting_) {
            const Ev& w = evs_[next_];
            if (w.b == 1 ? screen_ != w.code || screenRun_ < w.a : playRun < w.a)
                break;
            waiting_ = false;
            segBase_ = frame;
            if (milliseconds_) {
                timeBase_ = lastMs_;
                elapsedMs = 0;
            }
            ++next_;
            continue;
        }
        const Ev& e = evs_[next_];
        if (e.kind == Ev::Sync) {
            waiting_ = true;
            continue;
        }
        ++next_;
        switch (e.kind) {
            case Ev::Sync:
                break;
            case Ev::Key:
                if (e.mode == 0)
                    keys_[static_cast<size_t>(e.code) & 255] = false;
                else
                    keys_[static_cast<size_t>(e.code) & 255] = true;
                if (e.mode == 2)
                    tapKeys_[static_cast<size_t>(e.code) & 255] = true;
                break;
            case Ev::Button:
                if (e.mode == 0)
                    mouse_ = static_cast<uint8_t>(mouse_ & ~e.code);
                else
                    mouse_ = static_cast<uint8_t>(mouse_ | e.code);
                if (e.mode == 2)
                    tapMouse_ = static_cast<uint8_t>(tapMouse_ | e.code);
                break;
            case Ev::Mouse:
                reps_.push_back({e.a, e.b, e.n});
                break;
            case Ev::Wheel:
                wheel += e.a;
                break;
            case Ev::Pad:
                if (e.mode == 0)
                    pad_ = static_cast<uint16_t>(pad_ & ~e.code);
                else
                    pad_ = static_cast<uint16_t>(pad_ | e.code);
                if (e.mode == 2)
                    tapPad_ = static_cast<uint16_t>(tapPad_ | e.code);
                break;
            case Ev::Cursor:
                curX_ = e.a;
                curY_ = e.b;
                break;
            case Ev::Dump:
                ev.dumps.push_back(e.text);
                break;
            case Ev::Shot:
                ev.shots.push_back(e.text);
                break;
            case Ev::Log:
                ev.logOn = e.a;
                break;
            case Ev::Menu:
                ev.menu = e.a;
                break;
            case Ev::Quit:
                ev.quit = true;
                break;
            case Ev::DualWield:
                ev.dualWield = e.a;
                break;
        }
    }
    int dx = 0, dy = 0;
    for (auto it = reps_.begin(); it != reps_.end();) {
        dx += it->dx;
        dy += it->dy;
        if (--it->left <= 0)
            it = reps_.erase(it);
        else
            ++it;
    }
    out.keyDown = keys_;
    out.keyPressed = keys_ & ~prevKeys_;
    out.keyReleased = prevKeys_ & ~keys_;
    out.mouseDown = mouse_;
    out.mousePressed = static_cast<uint8_t>(mouse_ & ~prevMouse_);
    out.mouseReleased = static_cast<uint8_t>(prevMouse_ & ~mouse_);
    out.dx = dx;
    out.dy = dy;
    out.wheel = wheel;
    out.focused = true;
    out.cursorX = curX_;
    out.cursorY = curY_;
    out.clientW = 640;
    out.clientH = 512;
    padButtons = pad_;
    prevKeys_ = keys_;
    prevMouse_ = mouse_;
}

} // namespace drakan_input
