#include "lib/drakan_config.h"
#include "lib/drakan_log.h"
#include "lib/drakan_crash.h"
#include "lib/drakan_region.h"
#include "ps2_runtime.h"
#include "games_database.h"
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
#include "ps2_debug_panel.h"
#endif

#ifdef _DEBUG
#include "ps2_log.h"
#endif

#include <iostream>
#include <string>
#include <filesystem>
#include <exception>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#include <unistd.h>
#include <thread>
#include <cstdio>
#include <cstring>
#endif

std::string drakanExtractBootElfFromIso(); // drakan_overrides.cpp

namespace {
[[noreturn]] void finishProcess(int code) {
#if defined(__EMSCRIPTEN__)
    // _Exit on the application worker bypasses the browser's normal onExit.
    MAIN_THREAD_EM_ASM({ Module['onExit']($0); }, code);
#endif
    std::_Exit(code);
}
#if defined(__ANDROID__)
int g_logcatPipeFds[2]{-1, -1};
std::thread g_logcatThread;

void stopLogcatRedirect() {
    std::fflush(stdout);
    std::fflush(stderr);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if (g_logcatPipeFds[1] >= 0) {
        close(g_logcatPipeFds[1]);
        g_logcatPipeFds[1] = -1;
    }
    if (g_logcatThread.joinable()) {
        g_logcatThread.join();
    }
}

void redirectStdioToLogcat() {
    if (pipe(g_logcatPipeFds) != 0) {
        return;
    }

    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(g_logcatPipeFds[1], STDOUT_FILENO);
    dup2(g_logcatPipeFds[1], STDERR_FILENO);

    g_logcatThread = std::thread([]() {
        FILE* reader = fdopen(g_logcatPipeFds[0], "r");
        if (!reader) {
            return;
        }
        char line[1024];
        while (fgets(line, sizeof(line), reader)) {
            size_t len = std::strlen(line);
            if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
            }
            __android_log_write(ANDROID_LOG_INFO, "ps2x", line);
        }
        fclose(reader);
        g_logcatPipeFds[0] = -1;
    });
    if (std::atexit(stopLogcatRedirect) != 0) {
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        close(g_logcatPipeFds[1]);
        g_logcatPipeFds[1] = -1;
        if (g_logcatThread.joinable()) {
            g_logcatThread.join();
        }
    }
}
#endif

void setupTerminateLogger() // to help on release build crashs
{
    std::set_terminate([]() {
        std::cerr << "[terminate] unhandled exception" << std::endl;
        const std::exception_ptr ep = std::current_exception();
        if (ep) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::system_error& e) {
                std::cerr << "[terminate] std::system_error code=" << e.code().value()
                          << " category=" << e.code().category().name() << " message=" << e.what() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[terminate] std::exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[terminate] non-std exception" << std::endl;
            }
        }
        drakan_crash::fatal("std::terminate");
        std::abort();
    });
}

std::string normalizeGameId(const std::string& folderName) {
    std::string result = folderName;

    size_t underscore = result.find('_');
    if (underscore != std::string::npos)
        result[underscore] = '-';

    size_t dot = result.find('.');
    if (dot != std::string::npos)
        result.erase(dot, 1);

    std::ranges::transform(result, result.begin(),
                           [](unsigned char character) { return static_cast<char>(std::toupper(character)); });

    return result;
}

std::filesystem::path getExecutablePath(int argc, char* argv[]) {
    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
        std::cout << "Using argv boot path" << std::endl;
        return std::filesystem::path(argv[1]);
    }
#if defined(PS2X_DEFAULT_BOOT_ELF)
    std::cout << "Using default boot file" << std::endl;
    const std::filesystem::path configuredPath = std::filesystem::path(PS2X_DEFAULT_BOOT_ELF);
#if defined(PLATFORM_VITA)
    return configuredPath;
#endif
    if (configuredPath.is_absolute()) {
        return configuredPath;
    }
    return (std::filesystem::current_path() / configuredPath).lexically_normal();
#else
    // Drakan: no argument -> DRAKAN_ELF, drakan.ini [general] elf, or the extracted ELF next to / above the exe.
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        const std::string cfgElf = drakan_cfg::getString("general", "elf", "DRAKAN_ELF", "");
        if (!cfgElf.empty())
            candidates.emplace_back(cfgElf);
        const fs::path dir = fs::path(drakan_cfg::exeDir());
        const fs::path boot = drakan_region::build().bootElf; // SCES_500.06 / SCUS_971.28 (the build's region)
        for (const fs::path rel : {boot, "iso" / boot, ".." / fs::path("iso") / boot, "../.." / fs::path("iso") / boot,
                                   "../../.." / fs::path("iso") / boot}) {
            candidates.push_back(dir / rel);
            candidates.push_back(fs::current_path() / rel);
        }
        std::error_code ec;
        for (const fs::path& c : candidates)
            if (fs::exists(c, ec)) {
                std::cout << "Using boot file " << fs::weakly_canonical(c, ec).string() << std::endl;
                return fs::weakly_canonical(c, ec);
            }
    }
    {
        // no extracted ELF: read it from the disc image (drakan.ini [general] iso / DRAKAN_ISO)
        const std::string fromIso = drakanExtractBootElfFromIso();
        if (!fromIso.empty()) {
            std::cout << "Using boot file from the disc image: " << fromIso << std::endl;
            return std::filesystem::path(fromIso);
        }
    }
    throw std::runtime_error("Unable to determine executable path. Pass the guest ELF as argv[1], or set drakan.ini "
                             "[general] iso = <path to the disc image> (or elf = <path to the boot ELF>).");
#endif
}
} // namespace

int main(int argc, char* argv[]) {
#if defined(__ANDROID__)
    redirectStdioToLogcat();
#endif
    drakan_log::init(); // GUI-subsystem exe: stdout / stderr -> async log (never blocks the game threads)
    setupTerminateLogger();

    try {
        std::filesystem::path pathObj = getExecutablePath(argc, argv);

        std::string filePathStr = pathObj.string();
        std::string elfName = pathObj.filename().string();
        std::string normalizedId = normalizeGameId(elfName);

        std::string windowTitle = "PS2-Recomp | ";
        const char* gameName = getGameName(normalizedId);

#if !defined(PLATFORM_VITA)
        if (gameName) {
            windowTitle += std::string(gameName) + " | " + elfName;
        } else
#endif
        {
            windowTitle += elfName;
        }
#if defined(_WIN32) || defined(__linux__) || defined(__EMSCRIPTEN__)
        // Drakan port: the only game this build runs.
        windowTitle = "Drakan: The Ancients' Gates";
#endif

        PS2Runtime runtime;
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
        // This hook is to prevent leak rlimgui deps to recompiler etc
        PS2DebugPanel debugPanel;
        runtime.setDebugUiCallbacks(
            [](PS2Runtime& rt, void* userData) {
                (void)rt;
                static_cast<PS2DebugPanel*>(userData)->initialize();
            },
            [](PS2Runtime& rt, void* userData) { static_cast<PS2DebugPanel*>(userData)->draw(rt); },
            [](PS2Runtime& rt, void* userData) {
                (void)rt;
                static_cast<PS2DebugPanel*>(userData)->shutdown();
            },
            &debugPanel);
#endif
        if (!runtime.initialize(windowTitle.c_str())) {
            std::cerr << "Failed to initialize PS2 runtime" << std::endl;
            return 1;
        }

        if (!runtime.loadELF(filePathStr)) {
            std::cerr << "Failed to load ELF file: " << filePathStr << std::endl;
            return 1;
        }

        drakan_crash::install(&runtime);
        runtime.run();

#ifdef _DEBUG
        ps2_log::print_saved_location();
#endif
        std::cout.flush();
        std::cerr.flush();
#if defined(_WIN32)
        drakan_log::flush(); // drakan.log reaches the disk before the process ends
#endif
        finishProcess(0);
    } catch (const std::exception& e) {
        std::cerr << "[main] fatal exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[main] fatal exception: unknown" << std::endl;
    }

    std::cout.flush();
    std::cerr.flush();
    finishProcess(1);
}
