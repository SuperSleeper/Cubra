#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

namespace td {

struct GpuInfo {
    QString name;
    int totalMiB = 0;
    int freeMiB = 0;
};

struct HardwareInfo {
    std::vector<GpuInfo> gpus;
    int ramAvailableMiB = 0;
    // The GPU with the most memory, or nullptr when none can run models.
    const GpuInfo* best() const;
};

std::vector<GpuInfo> parseNvidiaSmi(const QByteArray& csv);

// Blocks for up to a few seconds, so call it off the UI thread.
HardwareInfo probeHardware();

}  // namespace td
