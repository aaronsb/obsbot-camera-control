#include "GuiRemoteClient.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QString>

GuiRemoteRecallResult recallPresetViaRunningGui(int presetNumber)
{
    QDBusInterface remote(
        QStringLiteral("com.obsbot.CameraControl"),
        QStringLiteral("/CameraControl"),
        QStringLiteral("com.obsbot.CameraControl"),
        QDBusConnection::sessionBus());

    if (!remote.isValid()) {
        return {GuiRemoteRecallResult::Status::GuiNotRunning, {}};
    }

    const QDBusReply<bool> reply = remote.call(QStringLiteral("recallPreset"), presetNumber);
    if (!reply.isValid()) {
        return {
            GuiRemoteRecallResult::Status::Error,
            reply.error().message().toStdString()
        };
    }

    return reply.value()
        ? GuiRemoteRecallResult{GuiRemoteRecallResult::Status::Accepted, {}}
        : GuiRemoteRecallResult{GuiRemoteRecallResult::Status::Rejected,
                                "The running GUI rejected the preset recall."};
}
