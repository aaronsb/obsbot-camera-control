#ifndef CLIOPTIONS_H
#define CLIOPTIONS_H

#include <iosfwd>
#include <string>

enum class CliAction {
    ApplyConfig,
    Interactive,
    RecallPreset,
    ListPresets
};

struct CliOptions {
    CliAction action = CliAction::ApplyConfig;
    int presetNumber = 0;
    std::string serial;
    bool showHelp = false;
};

struct CliParseResult {
    CliOptions options;
    std::string error;

    bool ok() const { return error.empty(); }
};

CliParseResult parseCliOptions(int argc, char *const argv[]);
void printCliUsage(std::ostream &out, const char *programName);

#endif // CLIOPTIONS_H
