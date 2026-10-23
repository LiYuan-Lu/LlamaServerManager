// Stand-in for nvml.dll / libnvidia-ml.so used by the GpuMonitor tests.
// FAKE_NVML_MODE: "ok" (default, two GPUs), "no-gpu", or "init-fail".
// FAKE_NVML_PID: process reported on both GPUs (GPU 0: 1 GiB in the compute and the
// graphics list, GPU 1: 512 MiB). PID 4242 is reported on GPU 0 without a memory value.
// Demo options (README screenshots), off by default:
//   FAKE_NVML_COUNT      number of GPUs (1 or 2)
//   FAKE_NVML_NAME       name of GPU 0
//   FAKE_NVML_DYNAMIC    GPU 0 utilization varies over time; its used memory includes the
//                        processes from FAKE_NVML_PIDS_FILE
//   FAKE_NVML_PIDS_FILE  text file with "<pid> <bytes>" lines, reported as compute processes on GPU 0
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS // getenv and strncpy are fine in this test double
#endif
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
bool dynamic() { return std::getenv("FAKE_NVML_DYNAMIC") != nullptr; }
double seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
struct Listed {
    unsigned int pid;
    unsigned long long bytes;
};
std::vector<Listed> listed()
{
    std::vector<Listed> result;
    const char* path = std::getenv("FAKE_NVML_PIDS_FILE");
    if (!path) {
        return result;
    }
    if (FILE* file = std::fopen(path, "r")) {
        unsigned int pid = 0;
        unsigned long long bytes = 0;
        while (result.size() < 32 && std::fscanf(file, "%u %llu", &pid, &bytes) == 2) {
            result.push_back({ pid, bytes });
        }
        std::fclose(file);
    }
    return result;
}
unsigned int trackedPid()
{
    const char* value = std::getenv("FAKE_NVML_PID");
    return value ? unsigned(std::strtoul(value, nullptr, 10)) : 0;
}
int processes(Device* device, unsigned int* count, ProcessInfo* infos, bool graphics)
{
    ProcessInfo list[40];
    unsigned int n = 0;
    if (device->index == 0) {
        list[n++] = { trackedPid(), GiB, 0, 0 };
        if (!graphics) {
            list[n++] = { 4242, NotAvailable, 0, 0 };
            for (const auto& p : listed()) {
                list[n++] = { p.pid, p.bytes, 0, 0 };
            }
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
    const char* wanted = std::getenv("FAKE_NVML_COUNT");
    *count = mode("no-gpu") ? 0 : (wanted && std::atoi(wanted) == 1 ? 1 : 2);
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
    const char* custom = std::getenv("FAKE_NVML_NAME");
    const char* value = device->index == 0 ? (custom && *custom ? custom : "Fake RTX 0") : "Fake RTX 1";
    std::strncpy(name, value, length - 1);
    name[length - 1] = '\0';
    return 0;
}
NVML_EXPORT int nvmlDeviceGetUtilizationRates(Device* device, Utilization* utilization)
{
    unsigned int gpu = device->index == 0 ? 75u : 5u;
    if (dynamic() && device->index == 0) {
        // Bursts of inference load on top of a low idle level.
        const double t = seconds();
        const double load = 18 + 30 * (1 + std::sin(t / 23.0)) * (0.6 + 0.4 * std::sin(t / 7.0));
        gpu = unsigned(std::fmin(99.0, std::fmax(2.0, load)));
    }
    *utilization = { gpu, 30u };
    return 0;
}
NVML_EXPORT int nvmlDeviceGetMemoryInfo(Device* device, Memory* memory)
{
    unsigned long long used = device->index == 0 ? 6 * GiB : GiB;
    if (dynamic() && device->index == 0) {
        used = GiB; // desktop and driver
        for (const auto& p : listed()) {
            used += p.bytes;
        }
    }
    *memory = { 24 * GiB, 24 * GiB - used, used };
    return 0;
}
NVML_EXPORT int nvmlDeviceGetTemperature(Device* device, unsigned int, unsigned int* temperature)
{
    *temperature = device->index == 0 ? 67 : 40;
    if (dynamic() && device->index == 0) {
        Utilization u { };
        nvmlDeviceGetUtilizationRates(device, &u);
        *temperature = 42 + u.gpu * 3 / 10;
    }
    return 0;
}
// GPU 1 does not report power, like many consumer cards.
NVML_EXPORT int nvmlDeviceGetPowerUsage(Device* device, unsigned int* milliwatts)
{
    if (device->index == 1) {
        return 3; // NVML_ERROR_NOT_SUPPORTED
    }
    *milliwatts = 215500;
    if (dynamic()) {
        Utilization u { };
        nvmlDeviceGetUtilizationRates(device, &u);
        *milliwatts = 35000 + u.gpu * 3200;
    }
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
