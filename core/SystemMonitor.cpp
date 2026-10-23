#include "SystemMonitor.h"
#include <QFile>
#include <QSet>
#include <QThread>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
// windows.h must come first.
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace {
double percent(double part, double whole)
{
    if (whole <= 0) {
        return -1;
    }
    return std::clamp(part / whole * 100.0, 0.0, 100.0);
}
#ifdef Q_OS_WIN
quint64 fileTime(const FILETIME& t)
{
    return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
#endif
}

SystemMonitor::SystemMonitor()
{
    wall.start();
}

int SystemMonitor::logicalProcessors()
{
    return std::max(1, QThread::idealThreadCount());
}

SystemMonitor::CpuTimes SystemMonitor::readSystemTimes()
{
    CpuTimes times;
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        // Kernel time includes idle time.
        times.idle = fileTime(idle);
        times.total = fileTime(kernel) + fileTime(user);
        times.valid = true;
    }
#elif defined(Q_OS_LINUX)
    QFile file("/proc/stat");
    if (file.open(QIODevice::ReadOnly)) {
        const auto fields = file.readAll().split('\n').value(0).simplified().split(' ');
        // cpu user nice system idle iowait irq softirq steal [guest guest_nice]
        if (fields.size() >= 9 && fields[0] == "cpu") {
            quint64 total = 0;
            for (int i = 1; i <= 8; ++i) {
                total += fields[i].toULongLong();
            }
            times.idle = fields[4].toULongLong() + fields[5].toULongLong();
            times.total = total;
            times.valid = true;
        }
    }
#endif
    return times;
}

bool SystemMonitor::readMemory(qint64& used, qint64& total)
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status { };
    status.dwLength = DWORD(sizeof(status));
    if (!GlobalMemoryStatusEx(&status)) {
        return false;
    }
    total = qint64(status.ullTotalPhys);
    used = qint64(status.ullTotalPhys - status.ullAvailPhys);
    return true;
#elif defined(Q_OS_LINUX)
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    qint64 totalKb = -1, availableKb = -1;
    // /proc files report size 0, so read everything instead of testing atEnd().
    for (const auto& line : file.readAll().split('\n')) {
        const auto fields = line.simplified().split(' ');
        if (fields.size() < 2) {
            continue;
        }
        if (fields[0] == "MemTotal:") {
            totalKb = fields[1].toLongLong();
        } else if (fields[0] == "MemAvailable:") {
            availableKb = fields[1].toLongLong();
        }
    }
    if (totalKb <= 0 || availableKb < 0) {
        return false;
    }
    total = totalKb * 1024;
    used = (totalKb - availableKb) * 1024;
    return true;
#else
    Q_UNUSED(used);
    Q_UNUSED(total);
    return false;
#endif
}

bool SystemMonitor::readProcess(qint64 pid, quint64& cpuNanoseconds, qint64& ramBytes)
{
    if (pid <= 0) {
        return false;
    }
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!handle) {
        return false;
    }
    bool ok = false;
    DWORD exitCode = 0;
    FILETIME created, exited, kernel, user;
    PROCESS_MEMORY_COUNTERS memory { };
    memory.cb = DWORD(sizeof(memory));
    if (GetExitCodeProcess(handle, &exitCode) && exitCode == STILL_ACTIVE
        && GetProcessTimes(handle, &created, &exited, &kernel, &user)
        && GetProcessMemoryInfo(handle, &memory, DWORD(sizeof(memory)))) {
        // FILETIME counts 100 ns units.
        cpuNanoseconds = (fileTime(kernel) + fileTime(user)) * 100;
        ramBytes = qint64(memory.WorkingSetSize);
        ok = true;
    }
    CloseHandle(handle);
    return ok;
#elif defined(Q_OS_LINUX)
    QFile stat(QString("/proc/%1/stat").arg(pid));
    QFile statm(QString("/proc/%1/statm").arg(pid));
    if (!stat.open(QIODevice::ReadOnly) || !statm.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray line = stat.readAll();
    // The command name may contain spaces and parentheses; fields follow the last ')'.
    const auto close = line.lastIndexOf(')');
    if (close < 0) {
        return false;
    }
    const auto fields = line.mid(close + 2).simplified().split(' ');
    // fields[0] is field 3 (state); utime and stime are fields 14 and 15.
    if (fields.size() < 13 || fields[0] == "Z") {
        return false;
    }
    const long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0) {
        return false;
    }
    cpuNanoseconds = (fields[11].toULongLong() + fields[12].toULongLong()) * (1000000000ULL / quint64(ticks));
    const auto pages = statm.readAll().simplified().split(' ');
    if (pages.size() < 2) {
        return false;
    }
    ramBytes = pages[1].toLongLong() * sysconf(_SC_PAGESIZE);
    return true;
#else
    Q_UNUSED(cpuNanoseconds);
    Q_UNUSED(ramBytes);
    return false;
#endif
}

SystemMonitor::SystemSample SystemMonitor::sampleSystem()
{
    SystemSample sample;
    const auto now = readSystemTimes();
    if (now.valid && previousSystem.valid && now.total > previousSystem.total) {
        const double total = double(now.total - previousSystem.total);
        const double idle = double(now.idle >= previousSystem.idle ? now.idle - previousSystem.idle : 0);
        sample.cpuPercent = percent(total - std::min(idle, total), total);
    }
    previousSystem = now;
    readMemory(sample.ramUsedBytes, sample.ramTotalBytes);
    return sample;
}

QList<SystemMonitor::ProcessSample> SystemMonitor::sampleProcesses(const QList<qint64>& pids)
{
    QList<ProcessSample> samples;
    const qint64 wallNow = wall.nsecsElapsed();
    const QSet<qint64> wanted(pids.begin(), pids.end());
    for (auto i = previousProcess.begin(); i != previousProcess.end();) {
        if (wanted.contains(i.key())) {
            ++i;
        } else {
            i = previousProcess.erase(i);
        }
    }
    for (const auto pid : pids) {
        ProcessSample sample;
        sample.pid = pid;
        quint64 cpu = 0;
        qint64 ram = -1;
        if (!readProcess(pid, cpu, ram)) {
            previousProcess.remove(pid);
            samples << sample;
            continue;
        }
        sample.running = true;
        sample.ramBytes = ram;
        const auto previous = previousProcess.constFind(pid);
        if (previous != previousProcess.cend() && wallNow > previous->wallNanoseconds && cpu >= previous->cpuNanoseconds) {
            const double capacity = double(wallNow - previous->wallNanoseconds) * logicalProcessors();
            sample.cpuPercent = percent(double(cpu - previous->cpuNanoseconds), capacity);
        }
        previousProcess.insert(pid, { cpu, wallNow });
        samples << sample;
    }
    return samples;
}
