#pragma once
#include <QHash>
#include <QLibrary>
#include <QList>
#include <QString>

// Reads NVIDIA GPU information through NVML, which is loaded at runtime (nvml.dll on
// Windows, libnvidia-ml.so.1 on Linux). Without NVML or an NVIDIA GPU the monitor reports
// itself as unavailable and every call stays harmless; nothing is linked at build time.
//
// Device values describe the whole GPU (all processes). Per-process GPU memory is reported
// separately and only when the driver provides it: Windows WDDM drivers usually do not,
// which is reported as -1 rather than guessed from the device total.
// Not thread-safe: use one instance from a single thread.
class GpuMonitor {
public:
    struct Device {
        int index = 0;
        QString name;
        int utilizationPercent = -1;
        qint64 memoryUsedBytes = -1;
        qint64 memoryTotalBytes = -1;
        int temperatureC = -1;
        double powerWatts = -1;
    };
    struct Sample {
        bool available = false;
        QString reason; // why the GPU information is unavailable
        QList<Device> devices;
        // GPU memory per process ID summed over all GPUs. -1: the process uses a GPU but the
        // driver does not report its memory. Processes not using any GPU are absent.
        QHash<qint64, qint64> processMemory;
    };
    // libraryPath overrides the NVML library location (tests); empty uses the system NVML.
    explicit GpuMonitor(const QString& libraryPath = { });
    ~GpuMonitor();
    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;
    bool available() const { return initialized; }
    QString unavailableReason() const { return reason; }
    Sample sample();

private:
    struct Api;
    bool load(const QString& libraryPath);
    QLibrary library;
    Api* api = nullptr;
    bool initialized = false;
    QString reason;
};
