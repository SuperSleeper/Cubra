#include "ai/GpuProbe.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

namespace td {
namespace {

constexpr qint64 kMiB = 1024 * 1024;

// Integrated GPUs report a small carve-out that is no use for running models.
constexpr int kMinDiscreteMiB = 1024;

std::vector<GpuInfo> queryNvidia() {
    QProcess process;
    process.start(QStringLiteral("nvidia-smi"), {QStringLiteral("--query-gpu=name,memory.total,memory.free"),
                                                 QStringLiteral("--format=csv,noheader,nounits")});
    if (!process.waitForFinished(3000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        process.waitForFinished(500);
        return {};
    }
    return parseNvidiaSmi(process.readAllStandardOutput());
}

qint64 readSysfsNumber(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    bool ok = false;
    const qint64 value = file.readAll().trimmed().toLongLong(&ok);
    return ok ? value : -1;
}

std::vector<GpuInfo> queryAmd() {
    std::vector<GpuInfo> gpus;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString& card : drm.entryList({QStringLiteral("card?"), QStringLiteral("card??")}, QDir::Dirs)) {
        const QString device = drm.filePath(card) + QStringLiteral("/device/");
        const qint64 total = readSysfsNumber(device + QStringLiteral("mem_info_vram_total"));
        const qint64 used = readSysfsNumber(device + QStringLiteral("mem_info_vram_used"));
        if (total <= 0 || used < 0) continue;
        GpuInfo gpu;
        gpu.name = QStringLiteral("AMD GPU (%1)").arg(card);
        gpu.totalMiB = static_cast<int>(total / kMiB);
        gpu.freeMiB = static_cast<int>((total - used) / kMiB);
        if (gpu.totalMiB >= kMinDiscreteMiB) gpus.push_back(gpu);
    }
    return gpus;
}

int availableRamMiB() {
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    const QRegularExpression pattern(QStringLiteral("^MemAvailable:\\s+(\\d+) kB"), QRegularExpression::MultilineOption);
    const auto match = pattern.match(QString::fromLatin1(file.readAll()));
    return match.hasMatch() ? static_cast<int>(match.captured(1).toLongLong() / 1024) : 0;
}

}  // namespace

const GpuInfo* HardwareInfo::best() const {
    const GpuInfo* result = nullptr;
    for (const GpuInfo& gpu : gpus)
        if (!result || gpu.totalMiB > result->totalMiB) result = &gpu;
    return result;
}

std::vector<GpuInfo> parseNvidiaSmi(const QByteArray& csv) {
    std::vector<GpuInfo> gpus;
    for (const QByteArray& rawLine : csv.split('\n')) {
        // Read the numbers from the right, because a name may contain commas.
        const QString line = QString::fromUtf8(rawLine).trimmed();
        const int freeComma = line.lastIndexOf(',');
        if (freeComma <= 0) continue;
        const int totalComma = line.lastIndexOf(',', freeComma - 1);
        if (totalComma <= 0) continue;
        bool totalOk = false, freeOk = false;
        GpuInfo gpu;
        gpu.name = line.left(totalComma).trimmed();
        gpu.totalMiB = line.mid(totalComma + 1, freeComma - totalComma - 1).trimmed().toInt(&totalOk);
        gpu.freeMiB = line.mid(freeComma + 1).trimmed().toInt(&freeOk);
        if (totalOk && freeOk && gpu.totalMiB >= kMinDiscreteMiB) gpus.push_back(gpu);
    }
    return gpus;
}

HardwareInfo probeHardware() {
    HardwareInfo info;
    info.gpus = queryNvidia();
    for (GpuInfo& gpu : queryAmd()) info.gpus.push_back(std::move(gpu));
    info.ramAvailableMiB = availableRamMiB();
    return info;
}

}  // namespace td
