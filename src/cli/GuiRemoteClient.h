#ifndef GUIREMOTECLIENT_H
#define GUIREMOTECLIENT_H

#include <string>

struct GuiRemoteRecallResult {
    enum class Status {
        GuiNotRunning,
        Accepted,
        Rejected,
        Error
    };

    Status status = Status::GuiNotRunning;
    std::string message;
};

GuiRemoteRecallResult recallPresetViaRunningGui(int presetNumber);

#endif // GUIREMOTECLIENT_H
