#include "CliOptions.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

CliParseResult parse(std::initializer_list<const char *> arguments)
{
    std::vector<std::string> storage;
    storage.reserve(arguments.size());
    for (const char *argument : arguments) {
        storage.emplace_back(argument);
    }

    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (auto &argument : storage) {
        argv.push_back(argument.data());
    }

    return parseCliOptions(static_cast<int>(argv.size()), argv.data());
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
    bool passed = true;

    const auto defaults = parse({"obsbot-cli"});
    passed &= check(defaults.ok(), "default arguments parse");
    passed &= check(defaults.options.action == CliAction::ApplyConfig, "default action applies config");

    const auto preset = parse({"obsbot-cli", "--preset", "2", "--serial", "ABC123"});
    passed &= check(preset.ok(), "preset with serial parses");
    passed &= check(preset.options.action == CliAction::RecallPreset, "preset action selected");
    passed &= check(preset.options.presetNumber == 2, "preset number retained");
    passed &= check(preset.options.serial == "ABC123", "serial retained");

    const auto inlinePreset = parse({"obsbot-cli", "--preset=3"});
    passed &= check(inlinePreset.ok() && inlinePreset.options.presetNumber == 3,
                    "inline preset syntax parses");

    const auto list = parse({"obsbot-cli", "--list-presets"});
    passed &= check(list.ok() && list.options.action == CliAction::ListPresets,
                    "list-presets action parses");

    passed &= check(!parse({"obsbot-cli", "--preset", "0"}).ok(),
                    "out-of-range preset rejected");
    passed &= check(!parse({"obsbot-cli", "--preset"}).ok(),
                    "missing preset value rejected");
    passed &= check(!parse({"obsbot-cli", "--serial", "--help"}).ok(),
                    "option token rejected as missing serial value");
    passed &= check(!parse({"obsbot-cli", "--serial", "--list-presets"}).ok(),
                    "action token cannot be consumed as a serial value");
    passed &= check(!parse({"obsbot-cli", "--preset", "1", "--interactive"}).ok(),
                    "conflicting actions rejected");
    passed &= check(!parse({"obsbot-cli", "--list-presets", "--serial", "ABC123"}).ok(),
                    "unused serial rejected for list action");
    passed &= check(!parse({"obsbot-cli", "--definitely-unknown"}).ok(),
                    "unknown option rejected");

    const auto help = parse({"obsbot-cli", "--help"});
    passed &= check(help.ok() && help.options.showHelp, "help flag parses");
    std::ostringstream usage;
    printCliUsage(usage, "obsbot-cli");
    passed &= check(usage.str().find("--preset N") != std::string::npos,
                    "usage documents preset action");

    return passed ? 0 : 1;
}
