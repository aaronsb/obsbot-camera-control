#include "Config.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string validConfigWithPreset(const std::string &pan,
                                  const std::string &tilt,
                                  const std::string &zoom)
{
    return
        "face_tracking=disabled\n"
        "hdr=disabled\n"
        "fov=wide\n"
        "face_ae=disabled\n"
        "face_focus=disabled\n"
        "zoom=1.0\n"
        "pan=0.0\n"
        "tilt=0.0\n"
        "brightness_auto=enabled\n"
        "brightness=128\n"
        "contrast_auto=enabled\n"
        "contrast=128\n"
        "saturation_auto=enabled\n"
        "saturation=128\n"
        "white_balance=auto\n"
        "start_minimized=disabled\n"
        "preset1_defined=enabled\n"
        "preset1_pan=" + pan + "\n"
        "preset1_tilt=" + tilt + "\n"
        "preset1_zoom=" + zoom + "\n";
}

bool loadConfig(const std::filesystem::path &configPath,
                const std::string &pan,
                const std::string &tilt,
                const std::string &zoom)
{
    std::ofstream file(configPath);
    file << validConfigWithPreset(pan, tilt, zoom);
    file.close();

    Config config;
    std::vector<Config::ValidationError> errors;
    return config.load(errors);
}

bool check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    const char *tmpDir = std::getenv("TMPDIR");
    if (!tmpDir || tmpDir[0] == '\0') {
        std::cerr << "FAIL: TMPDIR must be set for scratch-backed tests\n";
        return 1;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::path(tmpDir) / ("obsbot-config-test-" + std::to_string(nonce));
    const std::filesystem::path configDir = root / "obsbot-control";
    const std::filesystem::path configPath = configDir / "settings.conf";
    std::filesystem::create_directories(configDir);
    setenv("XDG_CONFIG_HOME", root.c_str(), 1);

    bool passed = true;
    passed &= check(loadConfig(configPath, "0.25", "-0.5", "1.75"),
                    "finite preset values are accepted");
    passed &= check(!loadConfig(configPath, "nan", "-0.5", "1.75"),
                    "NaN preset pan is rejected");
    passed &= check(!loadConfig(configPath, "0.25", "inf", "1.75"),
                    "infinite preset tilt is rejected");
    passed &= check(!loadConfig(configPath, "0.25junk", "-0.5", "1.75"),
                    "partially numeric preset pan is rejected");
    passed &= check(!loadConfig(configPath, "0.25", "-0.5", "1.75garbage"),
                    "partially numeric preset zoom is rejected");

    std::filesystem::remove_all(root);
    return passed ? 0 : 1;
}
