#include "GpuMonitor.h"
#include <QDir>
#include <QStringList>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

// Minimal NVML declarations (ABI from nvml.h); the NVIDIA SDK is not needed to build.
namespace nvml {
using Return = int;
using Device = struct DeviceOpaque*;
constexpr Return Success = 0;
constexpr Return InsufficientSize = 7;
constexpr unsigned long long ValueNotAvailable = std::numeric_limits<unsigned long long>::max();
constexpr unsigned int TemperatureGpu = 0;
constexpr unsigned int NameBufferSize = 96;
struct Utilization {
    unsigned int gpu;
    unsigned int memory;
};
struct Memory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};
// nvmlProcessInfo_v1_t: used by the unversioned *RunningProcesses functions.
struct ProcessInfoV1 {
    unsigned int pid;
    unsigned long long usedGpuMemory;
};
// nvmlProcessInfo_t / nvmlProcessInfo_v2_t: used by the _v2 and _v3 functions.
struct ProcessInfoV2 {
    unsigned int pid;
    unsigned long long usedGpuMemory;
    unsigned int gpuInstanceId;
    unsigned int computeInstanceId;
};
using Init = Return (*)();
using Shutdown = Return (*)();
using ErrorString = const char* (*)(Return);
using GetCount = Return (*)(unsigned int*);
using GetHandle = Return (*)(unsigned int, Device*);
using GetName = Return (*)(Device, char*, unsigned int);
using GetUtilization = Return (*)(Device, Utilization*);
using GetMemory = Return (*)(Device, Memory*);
using GetTemperature = Return (*)(Device, unsigned int, unsigned int*);
using GetPower = Return (*)(Device, unsigned int*);
using GetProcesses = Return (*)(Device, unsigned int*, void*);
}

struct GpuMonitor::Api {
    nvml::Init init = nullptr;
    nvml::Shutdown shutdown = nullptr;
    nvml::ErrorString errorString = nullptr;
    nvml::GetCount count = nullptr;
    nvml::GetHandle handle = nullptr;
    nvml::GetName name = nullptr;
    nvml::GetUtilization utilization = nullptr;
    nvml::GetMemory memory = nullptr;
    nvml::GetTemperature temperature = nullptr;
    nvml::GetPower power = nullptr;
    // Compute (CUDA) and graphics process lists; stride is the size of one record.
    nvml::GetProcesses computeProcesses = nullptr, graphicsProcesses = nullptr;
    qsizetype processStride = 0;
    QString error(nvml::Return code) const
    {
        if (errorString) {
            if (const char* text = errorString(code)) {
                return QString::fromUtf8(text);
            }
        }
        return QString("NVML error %1").arg(code);
    }
};

namespace {
QStringList candidates()
{
#ifdef Q_OS_WIN
    QStringList list { "nvml.dll" };
    const auto programFiles = qEnvironmentVariable("ProgramW6432", qEnvironmentVariable("ProgramFiles"));
    if (!programFiles.isEmpty()) {
        list << QDir(programFiles).filePath("NVIDIA Corporation/NVSMI/nvml.dll");
    }
    return list;
#else
    return { "libnvidia-ml.so.1", "libnvidia-ml.so" };
#endif
}

// Reads one process list. Returns false when the function is missing or fails.
bool readProcesses(nvml::GetProcesses function, qsizetype stride, nvml::Device device, QHash<qint64, qint64>& memory)
{
    if (!function) {
        return false;
    }
    unsigned int count = 64;
    QByteArray buffer;
    nvml::Return code = nvml::InsufficientSize;
    for (int attempt = 0; attempt < 2 && code == nvml::InsufficientSize; ++attempt) {
        count += 16; // processes may start between the calls
        buffer.fill(0, qsizetype(count) * stride);
        code = function(device, &count, buffer.data());
    }
    if (code != nvml::Success) {
        return false;
    }
    for (unsigned int i = 0; i < count; ++i) {
        const char* record = buffer.constData() + qsizetype(i) * stride;
        unsigned int pid = 0;
        unsigned long long used = 0;
        // Both record layouts start with pid followed by usedGpuMemory (8-byte aligned).
        memcpy(&pid, record, sizeof(pid));
        memcpy(&used, record + offsetof(nvml::ProcessInfoV1, usedGpuMemory), sizeof(used));
        const qint64 value = used == nvml::ValueNotAvailable ? -1 : qint64(used);
        // A process can appear in both the compute and the graphics list of one GPU with
        // the same allocation, so values for one device are merged, not added.
        const auto existing = memory.find(qint64(pid));
        if (existing == memory.end()) {
            memory.insert(qint64(pid), value);
        } else {
            *existing = std::max(*existing, value);
        }
    }
    return true;
}
}

GpuMonitor::GpuMonitor(const QString& libraryPath)
{
    initialized = load(libraryPath);
}

GpuMonitor::~GpuMonitor()
{
    if (initialized && api->shutdown) {
        api->shutdown();
    }
    delete api;
    // The library stays loaded: NVML does not support being unloaded and reloaded reliably.
}

bool GpuMonitor::load(const QString& libraryPath)
{
    for (const auto& path : libraryPath.isEmpty() ? candidates() : QStringList { libraryPath }) {
        library.setFileName(path);
        if (library.load()) {
            break;
        }
    }
    if (!library.isLoaded()) {
        reason = "NVML not found (no NVIDIA driver installed)";
        return false;
    }
    api = new Api;
    auto resolve = [this](const char* name) { return library.resolve(name); };
    api->init = reinterpret_cast<nvml::Init>(resolve("nvmlInit_v2"));
    api->shutdown = reinterpret_cast<nvml::Shutdown>(resolve("nvmlShutdown"));
    api->errorString = reinterpret_cast<nvml::ErrorString>(resolve("nvmlErrorString"));
    api->count = reinterpret_cast<nvml::GetCount>(resolve("nvmlDeviceGetCount_v2"));
    api->handle = reinterpret_cast<nvml::GetHandle>(resolve("nvmlDeviceGetHandleByIndex_v2"));
    api->name = reinterpret_cast<nvml::GetName>(resolve("nvmlDeviceGetName"));
    api->utilization = reinterpret_cast<nvml::GetUtilization>(resolve("nvmlDeviceGetUtilizationRates"));
    api->memory = reinterpret_cast<nvml::GetMemory>(resolve("nvmlDeviceGetMemoryInfo"));
    api->temperature = reinterpret_cast<nvml::GetTemperature>(resolve("nvmlDeviceGetTemperature"));
    api->power = reinterpret_cast<nvml::GetPower>(resolve("nvmlDeviceGetPowerUsage"));
    // Prefer the newest process-list functions; v2 and v3 share one record layout.
    for (const char* suffix : { "_v3", "_v2" }) {
        api->computeProcesses = reinterpret_cast<nvml::GetProcesses>(resolve((QByteArray("nvmlDeviceGetComputeRunningProcesses") + suffix).constData()));
        if (api->computeProcesses) {
            api->graphicsProcesses = reinterpret_cast<nvml::GetProcesses>(resolve((QByteArray("nvmlDeviceGetGraphicsRunningProcesses") + suffix).constData()));
            api->processStride = sizeof(nvml::ProcessInfoV2);
            break;
        }
    }
    if (!api->computeProcesses) {
        api->computeProcesses = reinterpret_cast<nvml::GetProcesses>(resolve("nvmlDeviceGetComputeRunningProcesses"));
        api->graphicsProcesses = reinterpret_cast<nvml::GetProcesses>(resolve("nvmlDeviceGetGraphicsRunningProcesses"));
        api->processStride = sizeof(nvml::ProcessInfoV1);
    }
    if (!api->init || !api->count || !api->handle) {
        reason = "NVML library is missing required functions";
        return false;
    }
    if (const auto code = api->init(); code != nvml::Success) {
        reason = "NVML initialization failed: " + api->error(code);
        return false;
    }
    unsigned int count = 0;
    if (const auto code = api->count(&count); code != nvml::Success || count == 0) {
        if (api->shutdown) {
            api->shutdown();
        }
        reason = code != nvml::Success ? "Cannot enumerate NVIDIA GPUs: " + api->error(code) : QString("No NVIDIA GPU found");
        return false;
    }
    return true;
}

GpuMonitor::Sample GpuMonitor::sample()
{
    Sample result;
    if (!initialized) {
        result.reason = reason;
        return result;
    }
    unsigned int count = 0;
    if (const auto code = api->count(&count); code != nvml::Success) {
        result.reason = "Cannot enumerate NVIDIA GPUs: " + api->error(code);
        return result;
    }
    result.available = true;
    for (unsigned int i = 0; i < count && i < 64; ++i) {
        nvml::Device device = nullptr;
        if (api->handle(i, &device) != nvml::Success) {
            continue;
        }
        Device d;
        d.index = int(i);
        char name[nvml::NameBufferSize] = { };
        if (api->name && api->name(device, name, sizeof(name)) == nvml::Success) {
            d.name = QString::fromUtf8(name);
        }
        nvml::Utilization utilization { };
        if (api->utilization && api->utilization(device, &utilization) == nvml::Success) {
            d.utilizationPercent = int(utilization.gpu);
        }
        nvml::Memory memory { };
        if (api->memory && api->memory(device, &memory) == nvml::Success) {
            d.memoryUsedBytes = qint64(memory.used);
            d.memoryTotalBytes = qint64(memory.total);
        }
        unsigned int temperature = 0;
        if (api->temperature && api->temperature(device, nvml::TemperatureGpu, &temperature) == nvml::Success) {
            d.temperatureC = int(temperature);
        }
        unsigned int milliwatts = 0;
        if (api->power && api->power(device, &milliwatts) == nvml::Success) {
            d.powerWatts = milliwatts / 1000.0;
        }
        result.devices << d;
        QHash<qint64, qint64> perDevice;
        readProcesses(api->computeProcesses, api->processStride, device, perDevice);
        readProcesses(api->graphicsProcesses, api->processStride, device, perDevice);
        // Across GPUs the allocations add up; one unknown value makes the total unknown.
        for (auto p = perDevice.cbegin(); p != perDevice.cend(); ++p) {
            const auto existing = result.processMemory.find(p.key());
            if (existing == result.processMemory.end()) {
                result.processMemory.insert(p.key(), p.value());
            } else if (*existing < 0 || p.value() < 0) {
                *existing = -1;
            } else {
                *existing += p.value();
            }
        }
    }
    return result;
}
