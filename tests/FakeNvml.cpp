// Stand-in for nvml.dll / libnvidia-ml.so used by the GpuMonitor tests.
// FAKE_NVML_MODE: "ok" (default, two GPUs), "no-gpu", or "init-fail".
// FAKE_NVML_PID: process reported on both GPUs (GPU 0: 1 GiB in the compute and the
// graphics list, GPU 1: 512 MiB). PID 4242 is reported on GPU 0 without a memory value.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS // getenv and strncpy are fine in this test double
#endif
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define NVML_EXPORT extern "C" __declspec(dllexport)
#else
#define NVML_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
struct Device {
    int index;
};
Device devices[2] = { { 0 }, { 1 } };
struct Utilization {
    unsigned int gpu, memory;
};
struct Memory {
    unsigned long long total, free, used;
};
struct ProcessInfo {
    unsigned int pid;
    unsigned long long usedGpuMemory;
    unsigned int gpuInstanceId, computeInstanceId;
};
constexpr unsigned long long GiB = 1024ULL * 1024 * 1024;
constexpr unsigned long long NotAvailable = ~0ULL;
bool mode(const char* name)
{
    const char* value = std::getenv("FAKE_NVML_MODE");
    return std::strcmp(value && *value ? value : "ok", name) == 0;
}
unsigned int trackedPid()
{
    const char* value = std::getenv("FAKE_NVML_PID");
    return value ? unsigned(std::strtoul(value, nullptr, 10)) : 0;
}
int processes(Device* device, unsigned int* count, ProcessInfo* infos, bool graphics)
{
    ProcessInfo list[2];
    unsigned int n = 0;
    if (device->index == 0) {
        list[n++] = { trackedPid(), GiB, 0, 0 };
        if (!graphics) {
            list[n++] = { 4242, NotAvailable, 0, 0 };
        }
    } else if (!graphics) {
        list[n++] = { trackedPid(), GiB / 2, 0, 0 };
    }
    if (*count < n) {
        *count = n;
        return 7; // NVML_ERROR_INSUFFICIENT_SIZE
    }
    std::memcpy(infos, list, sizeof(ProcessInfo) * n);
    *count = n;
    return 0;
}
}

NVML_EXPORT int nvmlInit_v2() { return mode("init-fail") ? 9 : 0; }
NVML_EXPORT int nvmlShutdown() { return 0; }
NVML_EXPORT const char* nvmlErrorString(int code) { return code == 9 ? "Driver Not Loaded" : "Unknown Error"; }
NVML_EXPORT int nvmlDeviceGetCount_v2(unsigned int* count)
{
    *count = mode("no-gpu") ? 0 : 2;
    return 0;
}
NVML_EXPORT int nvmlDeviceGetHandleByIndex_v2(unsigned int index, Device** device)
{
    if (index > 1) {
        return 2; // NVML_ERROR_INVALID_ARGUMENT
    }
    *device = &devices[index];
    return 0;
}
NVML_EXPORT int nvmlDeviceGetName(Device* device, char* name, unsigned int length)
{
    const char* value = device->index == 0 ? "Fake RTX 0" : "Fake RTX 1";
    std::strncpy(name, value, length - 1);
    name[length - 1] = '\0';
    return 0;
}
NVML_EXPORT int nvmlDeviceGetUtilizationRates(Device* device, Utilization* utilization)
{
    *utilization = { device->index == 0 ? 75u : 5u, 30u };
    return 0;
}
NVML_EXPORT int nvmlDeviceGetMemoryInfo(Device* device, Memory* memory)
{
    const unsigned long long used = device->index == 0 ? 6 * GiB : GiB;
    *memory = { 24 * GiB, 24 * GiB - used, used };
    return 0;
}
NVML_EXPORT int nvmlDeviceGetTemperature(Device* device, unsigned int, unsigned int* temperature)
{
    *temperature = device->index == 0 ? 67 : 40;
    return 0;
}
// GPU 1 does not report power, like many consumer cards.
NVML_EXPORT int nvmlDeviceGetPowerUsage(Device* device, unsigned int* milliwatts)
{
    if (device->index == 1) {
        return 3; // NVML_ERROR_NOT_SUPPORTED
    }
    *milliwatts = 215500;
    return 0;
}
NVML_EXPORT int nvmlDeviceGetComputeRunningProcesses_v3(Device* device, unsigned int* count, ProcessInfo* infos)
{
    return processes(device, count, infos, false);
}
NVML_EXPORT int nvmlDeviceGetGraphicsRunningProcesses_v3(Device* device, unsigned int* count, ProcessInfo* infos)
{
    return processes(device, count, infos, true);
}
