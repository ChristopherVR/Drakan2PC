#include "input/Script.h"
#include "input/Config.h"
#include <cstdlib>
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
        std::string header;
        if (ss >> header && header == "clock") {
            std::string units, extra;
            auto bad = [&](const char* m) {
                if (err)
                    *err += "line " + std::to_string(ln) + ": " + m + "\n";
            };
            if (!(ss >> units) || units != "milliseconds" || (ss >> extra)) {
                bad("clock milliseconds");
            } else if (milliseconds_ || !evs_.empty()) {
                bad("clock must appear once before events");
            } else {
                milliseconds_ = true;
            }
            continue;
        }
        Ev e;
        if (parseLine(line, ln, e, err))
            evs_.push_back(e);
    }
    loaded_ = true;
    return true;
}

bool InputScript::parseLine(const std::string& line, int ln, Ev& e, std::string* err) {
    std::istringstream ss(line);
    auto bad = [&](const char* m) {
        if (err)
            *err += "line " + std::to_string(ln) + ": " + m + "\n";
        return false;
    };
    std::string cmd, first;
    if (!(ss >> first))
        return false; // blank or comment-only line
    char* endp = nullptr;
    const long when = std::strtol(first.c_str(), &endp, 10);
    if (*endp != '\0')
        return bad("every line needs a leading time: '<time> <command> ...' (for example '0 sync play 600')");
    e.frame = static_cast<int>(when);
    if (!(ss >> cmd))
        return bad("missing command after the time");
    auto mode = [&](const std::string& m) { return m == "down" ? 1 : m == "up" ? 0 : m == "tap" ? 2 : -1; };
    std::string a, b;
    if (cmd == "key") {
        ss >> a >> b;
        e.kind = Ev::Key;
        e.mode = mode(a);
        if (e.mode < 0 || !keyFromName(b, &e.code)) {
            bad("key <down|up|tap> <NAME>");
            return false;
        }
    } else if (cmd == "button") {
        ss >> a >> e.code;
        e.kind = Ev::Button;
        e.mode = mode(a);
        if (e.mode < 0 || e.code < 1 || e.code > 5) {
            bad("button <down|up|tap> <1..5>");
            return false;
        }
        e.code = 1 << (e.code - 1);
    } else if (cmd == "mouse") {
        e.kind = Ev::Mouse;
        if (!(ss >> e.a >> e.b)) {
            bad("mouse <dx> <dy> [frames]");
            return false;
        }
        if (!(ss >> e.n))
            e.n = 1;
    } else if (cmd == "wheel") {
        e.kind = Ev::Wheel;
        if (!(ss >> e.a)) {
            bad("wheel <n>");
            return false;
        }
    } else if (cmd == "pad") {
        ss >> a >> b;
        uint16_t m = 0;
        e.kind = Ev::Pad;
        e.mode = mode(a);
        if (e.mode < 0 || !padButtonFromName(b, &m)) {
            bad("pad <down|up|tap> <BUTTON>");
            return false;
        }
        e.code = m;
    } else if (cmd == "cursor") {
        e.kind = Ev::Cursor;
        if (!(ss >> e.a >> e.b)) {
            bad("cursor <x> <y>");
            return false;
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
    } else if (cmd == "setting") {
        std::string value;
        ss >> e.text >> value;
        if (e.text.empty() || value.empty()) {
            bad("setting <key> <value>");
            return false;
        }
        e.kind = Ev::Setting;
        e.text += " " + value; // "<key> <value>"
    } else if (cmd == "dual_wield") {
        ss >> a;
        if (a != "on" && a != "off") {
            bad("dual_wield on|off");
            return false;
        }
        e.kind = Ev::DualWield;
        e.a = a == "on" ? 1 : 0;
    } else if (cmd == "sync") {
        e.kind = Ev::Sync;
        ss >> a;
        if (a == "play") {
            if (!(ss >> e.a)) {
                bad("sync play <n>");
                return false;
            }
        } else if (a == "screen" && ss >> e.code) {
            e.b = 1; // e.code = screen id, e.a = consecutive samples
            if (!(ss >> e.a) || e.a < 1)
                e.a = 1;
        } else {
            bad("sync play <n> | sync screen <id> [n]");
            return false;
        }
    } else if (cmd == "place") {
        e.kind = Ev::Place;
        if (!(ss >> e.place.pos[0] >> e.place.pos[1] >> e.place.pos[2])) {
            bad("place <x> <y> <z> [yaw]");
            return false;
        }
        e.place.hasYaw = static_cast<bool>(ss >> e.place.yaw);
        if (!e.place.hasYaw)
            e.place.yaw = 0;
    } else if (cmd == "quit") {
        e.kind = Ev::Quit;
    } else {
        return bad("unknown command");
    }
    return true;
}

int InputScript::appendLive(const std::string& text, std::string* err) {
    std::istringstream in(text);
    std::string line;
    int added = 0;
    while (std::getline(in, line)) {
        ++liveLines_;
        size_t h = line.find('#');
        if (h != std::string::npos)
            line.resize(h);
        Ev e;
        if (!parseLine(line, liveLines_, e, err))
            continue;
        if (e.kind == Ev::Sync) {
            if (err)
                *err += "live line " + std::to_string(liveLines_) + ": sync is not available in live input\n";
            continue;
        }
        // <frame> counts input samples from the sample the line arrived on (0 = the next advance)
        e.frame = (e.frame > 0 ? e.frame : 0) + lastFrame_ + 1;
        live_.push_back(e);
        ++added;
    }
    return added;
}

void InputScript::apply(const Ev& e, ScriptEvents& ev, int& wheel) {
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
        case Ev::Place:
            ev.places.push_back(e.place);
            break;
        case Ev::Setting: {
            const size_t sp = e.text.find(' ');
            ev.settings.emplace_back(e.text.substr(0, sp), e.text.substr(sp + 1));
            break;
        }
    }
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
        apply(e, ev, wheel);
    }
    lastFrame_ = frame;
    for (auto it = live_.begin(); it != live_.end();) {
        if (it->frame > frame) {
            ++it;
            continue;
        }
        apply(*it, ev, wheel);
        ++liveApplied_;
        it = live_.erase(it);
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
