#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QtGlobal>

// Samples system-wide and per-process CPU and RAM usage.
// CPU percentages are computed from the difference between two samples, so the first
// sample of the system or of a new PID reports -1 (not available yet).
// Process CPU is relative to the whole machine (100% = all logical processors busy),
// matching Windows Task Manager. Process RAM is the working set (resident memory).
// Not thread-safe: use one instance from a single thread.
class SystemMonitor {
public:
    struct SystemSample {
        double cpuPercent = -1;
        qint64 ramUsedBytes = -1;
        qint64 ramTotalBytes = -1;
    };
    struct ProcessSample {
        qint64 pid = 0;
        bool running = false;
        double cpuPercent = -1;
        qint64 ramBytes = -1;
    };
    SystemMonitor();
    SystemSample sampleSystem();
    // Forgets the CPU baselines of PIDs that are not in `pids`.
    QList<ProcessSample> sampleProcesses(const QList<qint64>& pids);
    static int logicalProcessors();

private:
    struct CpuTimes {
        quint64 idle = 0, total = 0;
        bool valid = false;
    };
    struct ProcessTimes {
        quint64 cpuNanoseconds = 0;
        qint64 wallNanoseconds = 0;
    };
    static CpuTimes readSystemTimes();
    static bool readProcess(qint64 pid, quint64& cpuNanoseconds, qint64& ramBytes);
    static bool readMemory(qint64& used, qint64& total);
    CpuTimes previousSystem;
    QHash<qint64, ProcessTimes> previousProcess;
    QElapsedTimer wall;
};
