#include "drakan_config.h"
#include "drakan_host.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    assert(argc == 3);
    namespace fs = std::filesystem;
    const fs::path expectedDir = fs::canonical(argv[1]);
    const fs::path scratchIni = fs::absolute(argv[2]);
    // CTest launches us outside the executable directory. Resolving /proc/self/exe
    // must not silently turn configuration lookup into a current-directory lookup.
    assert(fs::equivalent(fs::path(drakan_cfg::exeDir()), expectedDir));
    const std::string dir = drakan_cfg::exeDir();
    assert(!dir.empty() && (dir.back() == '/' || dir.back() == '\\'));
    drakan_cfg::enableDpiAwareness();

    assert(drakan_host::asciiEqual("UnCaPpEd", "uncapped"));
    assert(!drakan_host::asciiEqual("uncapped ", "uncapped"));
    assert(!drakan_host::asciiEqual("window", "native"));

    const std::string ini = scratchIni.string();
#if defined(_WIN32)
    assert(_putenv_s("DRAKAN_INI", ini.c_str()) == 0);
    assert(_putenv_s("DRAKAN_HOST_TEST_VALUE", "73") == 0);
#else
    assert(setenv("DRAKAN_INI", ini.c_str(), 1) == 0);
    assert(setenv("DRAKAN_HOST_TEST_VALUE", "73", 1) == 0);
#endif
    {
        std::ofstream out(scratchIni);
        out << "; preserve user configuration\n[video]\nfov = 91\n";
        assert(out.good());
    }
    assert(drakan_cfg::iniPath() == ini);
    assert(drakan_cfg::getInt("video", "fov", nullptr, 80) == 91);
    assert(drakan_cfg::getInt("video", "fov", "DRAKAN_HOST_TEST_VALUE", 80) == 73);
    std::ifstream in(scratchIni);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    assert(text.find("preserve user configuration") != std::string::npos);
    assert(text.find("fov = 91") != std::string::npos);
    in.close();
    fs::remove(scratchIni);
}
