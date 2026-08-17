#ifndef CAMERASELECTION_H
#define CAMERASELECTION_H

#include <iosfwd>
#include <memory>
#include <string>

class Device;

std::shared_ptr<Device> waitForSelectedCamera(const std::string &serial,
                                              bool requireExplicitSelection,
                                              int timeoutSeconds,
                                              std::ostream &progress,
                                              std::ostream &errors);

#endif // CAMERASELECTION_H
