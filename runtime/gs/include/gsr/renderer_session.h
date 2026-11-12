// What the previous game session says about the renderer to start with. Standard C++ only.
//
// The host keeps a small text record beside its log: the renderer a session started on and how it ended. A session
// that ends because the graphics device was lost records that; a session that dies without a word (wgpu-native ends
// the process on some calls to a lost device, and a driver can take the process down too) leaves "running" behind.
// The next start reads the record and may begin on another renderer once, telling the player why.
#pragma once
#include "gsr.h"

#include <cstdio>
#include <string>

namespace gsr {
// True when this build contains that renderer (Windows builds have both; defined with the backend selection).
bool backendAvailable(Config::Api api);
} // namespace gsr

namespace gsr::session {

enum class End : uint8_t { None, Running, Clean, DeviceLost };

struct Record {
    Config::Api api = Config::Api::Auto;
    End end = End::None;
    unsigned abnormalInRow = 0; // sessions on this renderer in a row that ended as Running or DeviceLost
    std::string reason;         // the driver's text of a device loss
};

struct Decision {
    Config::Api api = Config::Api::Auto; // the renderer to start on
    bool changed = false;                // differs from what the player asked for
    std::string notice;                  // empty, or one sentence for the log and the player
};

// wanted: the player's choice. haveD3D11: this build can fall back to Direct3D 11.
// - The previous session lost its device on the shared (WebGPU) renderer: start on Direct3D 11 this once.
// - Two sessions in a row on the shared renderer ended without a clean exit: the same.
// - Anything else: the player's choice. One unexplained end alone changes nothing (a killed process is one too).
inline Decision decide(const Record& previous, Config::Api wanted, bool haveD3D11) {
    Decision d;
    d.api = wanted;
    if (wanted == Config::Api::D3D11 || previous.api != Config::Api::WebGpu)
        return d;
    const bool lost = previous.end == End::DeviceLost;
    const bool repeated = previous.end == End::Running && previous.abnormalInRow >= 2;
    if (!lost && !repeated)
        return d;
    const std::string what = lost ? "The graphics device was lost in the last session" +
                                        (previous.reason.empty() ? std::string() : " (" + previous.reason + ")")
                                  : std::string("The last two sessions on this renderer ended unexpectedly");
    if (haveD3D11) {
        d.api = Config::Api::D3D11;
        d.changed = true;
        d.notice = what + ". This session runs on Direct3D 11; the renderer setting is unchanged.";
    } else {
        d.notice = what + ". Lower the internal resolution or switch ray tracing off if it happens again.";
    }
    return d;
}

// A session that left "running" behind may have died of a lost device: wgpu-native ends the process when a lost
// device is asked for a window image, and its last words are in that session's log. `previousLog` is the end of the
// previous session's log; the record is returned with the loss filled in when the log shows one.
inline Record interpret(Record previous, const std::string& previousLog) {
    if (previous.end != End::Running)
        return previous;
    static const char* const marks[] = {"Parent device is lost", "[gpu] device lost"};
    for (const char* mark : marks)
        if (previousLog.find(mark) != std::string::npos) {
            previous.end = End::DeviceLost;
            previous.reason = "the driver gave the device up and the graphics library ended the process";
            break;
        }
    return previous;
}

// The last `bytes` of a text file, empty when it cannot be read.
inline std::string tail(const std::string& path, size_t bytes = 32768) {
    std::string text;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        if (std::fseek(f, 0, SEEK_END) == 0) {
            const long size = std::ftell(f);
            const long from = size > long(bytes) ? size - long(bytes) : 0;
            if (size > 0 && std::fseek(f, from, SEEK_SET) == 0) {
                text.resize(size_t(size - from));
                text.resize(std::fread(text.data(), 1, text.size(), f));
            }
        }
        std::fclose(f);
    }
    return text;
}

// The record a new session writes at its start, given the one it found.
inline Record begin(const Record& previous, Config::Api api) {
    Record r;
    r.api = api;
    r.end = End::Running;
    const bool sameRenderer = previous.api == api;
    const bool abnormal = previous.end == End::Running || previous.end == End::DeviceLost;
    r.abnormalInRow = sameRenderer && abnormal ? previous.abnormalInRow : 0;
    ++r.abnormalInRow; // this session counts until it ends cleanly
    return r;
}

inline std::string serialize(const Record& r) {
    const char* api = r.api == Config::Api::D3D11 ? "d3d11" : r.api == Config::Api::WebGpu ? "webgpu" : "auto";
    const char* end = r.end == End::Running      ? "running"
                      : r.end == End::Clean      ? "clean"
                      : r.end == End::DeviceLost ? "device-lost"
                                                 : "none";
    std::string reason = r.reason;
    for (char& c : reason)
        if (c == '\n' || c == '\r')
            c = ' ';
    return std::string("renderer=") + api + "\nend=" + end + "\nabnormal=" + std::to_string(r.abnormalInRow) +
           "\nreason=" + reason + "\n";
}

inline Record parse(const std::string& text) {
    Record r;
    size_t at = 0;
    while (at < text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos)
            eol = text.size();
        const std::string line = text.substr(at, eol - at);
        at = eol + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "renderer")
            r.api = value == "d3d11" ? Config::Api::D3D11 : value == "webgpu" ? Config::Api::WebGpu : Config::Api::Auto;
        else if (key == "end")
            r.end = value == "running"       ? End::Running
                    : value == "clean"       ? End::Clean
                    : value == "device-lost" ? End::DeviceLost
                                             : End::None;
        else if (key == "abnormal") {
            unsigned n = 0;
            for (const char c : value)
                if (c >= '0' && c <= '9' && n < 1000)
                    n = n * 10 + unsigned(c - '0');
            r.abnormalInRow = n;
        } else if (key == "reason")
            r.reason = value;
    }
    return r;
}

inline Record load(const std::string& path) {
    std::string text;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buffer[1024];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0 && text.size() < 8192)
            text.append(buffer, n);
        std::fclose(f);
    }
    return parse(text);
}

inline bool save(const std::string& path, const Record& r) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const std::string text = serialize(r);
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

} // namespace gsr::session
