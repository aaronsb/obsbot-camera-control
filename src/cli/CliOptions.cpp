#include "CliOptions.h"

#include <charconv>
#include <ostream>
#include <string_view>

namespace {

bool parsePresetNumber(std::string_view value, int &presetNumber)
{
    if (value.empty()) {
        return false;
    }

    int parsed = 0;
    const char *begin = value.data();
    const char *end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end || parsed < 1 || parsed > 3) {
        return false;
    }

    presetNumber = parsed;
    return true;
}

bool selectAction(CliParseResult &result, CliAction action, const char *option)
{
    if (result.options.action != CliAction::ApplyConfig) {
        result.error = std::string(option) + " cannot be combined with another action";
        return false;
    }

    result.options.action = action;
    return true;
}

} // namespace

CliParseResult parseCliOptions(int argc, char *const argv[])
{
    CliParseResult result;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);

        if (argument == "-h" || argument == "--help") {
            result.options.showHelp = true;
            continue;
        }

        if (argument == "-i" || argument == "--interactive") {
            if (!selectAction(result, CliAction::Interactive, argv[i])) {
                return result;
            }
            continue;
        }

        if (argument == "--list-presets") {
            if (!selectAction(result, CliAction::ListPresets, argv[i])) {
                return result;
            }
            continue;
        }

        if (argument == "-p" || argument == "--preset") {
            if (i + 1 >= argc || !parsePresetNumber(argv[++i], result.options.presetNumber)) {
                result.error = std::string(argument) + " requires a preset number from 1 to 3";
                return result;
            }
            if (!selectAction(result, CliAction::RecallPreset, argv[i - 1])) {
                return result;
            }
            continue;
        }

        constexpr std::string_view presetPrefix = "--preset=";
        if (argument.rfind(presetPrefix, 0) == 0) {
            if (!parsePresetNumber(argument.substr(presetPrefix.size()), result.options.presetNumber)) {
                result.error = "--preset requires a preset number from 1 to 3";
                return result;
            }
            if (!selectAction(result, CliAction::RecallPreset, "--preset")) {
                return result;
            }
            continue;
        }

        if (argument == "--serial") {
            if (i + 1 >= argc || argv[i + 1][0] == '\0' || argv[i + 1][0] == '-') {
                result.error = "--serial requires a camera serial number";
                return result;
            }
            if (!result.options.serial.empty()) {
                result.error = "--serial may only be specified once";
                return result;
            }
            result.options.serial = argv[++i];
            continue;
        }

        result.error = "unknown option: " + std::string(argument);
        return result;
    }

    if (result.options.action == CliAction::ListPresets && !result.options.serial.empty()) {
        result.error = "--serial cannot be combined with --list-presets";
    }

    return result;
}

void printCliUsage(std::ostream &out, const char *programName)
{
    out << "OBSBOT Control - CLI Tool\n"
        << "\nUsage: " << programName << " [options]\n"
        << "\nActions (choose at most one):\n"
        << "  -i, --interactive       Run in interactive menu mode\n"
        << "  -p, --preset N          Recall saved position preset N (1-3)\n"
        << "      --list-presets      Show configured position presets and exit\n"
        << "\nOptions:\n"
        << "      --serial SERIAL     Target an exact camera serial number\n"
        << "  -h, --help              Show this help message\n"
        << "\nDefault behavior:\n"
        << "  Loads ~/.config/obsbot-control/settings.conf, applies all settings,\n"
        << "  and exits. Bind 'obsbot-cli --preset N' to a desktop global shortcut\n"
        << "  for quick position changes.\n";
}
