#include "Profile.h"
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>

QJsonObject Profile::toJson() const
{
    return { { "id", id }, { "name", name }, { "executable", executable }, { "model", model }, { "workingDirectory", workingDirectory },
        { "host", host }, { "port", port }, { "context", context }, { "gpuLayers", gpuLayers },
        { "threads", threads }, { "parallel", parallel }, { "metrics", metrics }, { "autoStart", autoStart },
        { "extraArguments", QJsonArray::fromStringList(extraArguments) }, { "environment", environment },
        { "watchdog", watchdog.toJson() } };
}
bool Profile::fromJson(const QJsonObject& o, Profile& p, QString& error)
{
    Profile candidate;
    auto str = [&](const char* key, QString& value) {
        if (!o.contains(key)) {
            return true;
        }
        if (!o[key].isString()) {
            error = QString("%1 must be a string").arg(key);
            return false;
        }
        value = o[key].toString();
        return true;
    };
    auto number = [&](const char* key, int& value) {
        if (!o.contains(key)) {
            return true;
        }
        const double n = o[key].toDouble(-1e20);
        if (!o[key].isDouble() || n != std::floor(n) || n < -1 || n > 2147483647.) {
            error = QString("%1 must be an integer").arg(key);
            return false;
        }
        value = int(n);
        return true;
    };
    if (!str("id", candidate.id) || !str("name", candidate.name)
        || !str("executable", candidate.executable) || !str("model", candidate.model)
        || !str("workingDirectory", candidate.workingDirectory) || !str("host", candidate.host)
        || !number("port", candidate.port) || !number("context", candidate.context)
        || !number("gpuLayers", candidate.gpuLayers) || !number("threads", candidate.threads)
        || !number("parallel", candidate.parallel)) {
        return false;
    }
    if (o.contains("metrics") && !o["metrics"].isBool()) {
        error = "metrics must be boolean";
        return false;
    }
    candidate.metrics = o["metrics"].toBool();
    if (o.contains("autoStart") && !o["autoStart"].isBool()) {
        error = "autoStart must be boolean";
        return false;
    }
    candidate.autoStart = o["autoStart"].toBool();
    if (o.contains("extraArguments") && !o["extraArguments"].isArray()) {
        error = "extraArguments must be an array";
        return false;
    }
    for (const auto& v : o["extraArguments"].toArray()) {
        if (!v.isString()) {
            error = "Arguments must be strings";
            return false;
        }
        candidate.extraArguments << v.toString();
    }
    if (o.contains("environment") && !o["environment"].isObject()) {
        error = "environment must be an object";
        return false;
    }
    candidate.environment = o["environment"].toObject();
    if (o.contains("watchdog")) {
        if (!o["watchdog"].isObject()) {
            error = "watchdog must be an object";
            return false;
        }
        if (!WatchdogSettings::fromJson(o["watchdog"].toObject(), candidate.watchdog, error)) {
            return false;
        }
    }
    error = candidate.validate(false);
    if (!error.isEmpty()) {
        return false;
    }
    p = candidate;
    return true;
}
QString Profile::validate(bool files) const
{
    if (port < 1 || port > 65535 || context < 1 || gpuLayers < -1 || threads < 1 || parallel < 1) {
        return "Invalid port/context/GPU layers/threads/parallel value";
    }
    if (!id.isEmpty() && !validId(id)) {
        return "Invalid profile ID";
    }
    if (const auto e = watchdog.validate(); !e.isEmpty()) {
        return e;
    }
    if (name.size() > 128 || name.contains(QChar::Null)) {
        return "Name must be at most 128 characters";
    }
    if (QHostAddress(host).isNull()) {
        return "Host must be a numeric IPv4 or IPv6 address";
    }
    if (executable.contains(QChar::Null) || model.contains(QChar::Null) || workingDirectory.contains(QChar::Null)) {
        return "Invalid path";
    }
    if (files) {
        if (!QFileInfo(executable).isAbsolute() || !QFileInfo(executable).isFile()) {
            return "Select an existing absolute executable path";
        }
        if (!QFileInfo(model).isAbsolute() || !QFileInfo(model).isFile() || !model.endsWith(".gguf", Qt::CaseInsensitive)) {
            return "Select an existing GGUF model";
        }
        if (!workingDirectory.isEmpty() && (!QFileInfo(workingDirectory).isAbsolute() || !QFileInfo(workingDirectory).isDir())) {
            return "Working directory does not exist";
        }
    }
    const QSet<QString> reserved = { "-m", "--model", "--model-url", "-mu", "--hf-repo", "-hf", "--host", "--port", "-c", "--ctx-size", "-ngl", "--gpu-layers", "--n-gpu-layers", "-t", "--threads", "--parallel", "-np", "--metrics", "--help", "-h", "--version" };
    for (const auto& arg : extraArguments) {
        if (arg.contains(QChar::Null) || reserved.contains(arg.section('=', 0, 0))) {
            return "Extra arguments must not override managed options or request help/version";
        }
    }
    for (auto i = environment.begin(); i != environment.end(); ++i) {
        if (i.key().isEmpty() || i.key().contains('=') || i.key().contains(QChar::Null) || !i.value().isString() || i.value().toString().contains(QChar::Null)) {
            return "Invalid environment variable";
        }
    }
    return { };
}
QStringList Profile::arguments() const
{
    QStringList a = { "-m", model, "-c", QString::number(context), "-ngl", QString::number(gpuLayers),
        "-t", QString::number(threads), "--host", host, "--port", QString::number(port), "--parallel", QString::number(parallel) };
    if (metrics) {
        a << "--metrics";
    }
    return a + extraArguments;
}
QString Profile::displayName() const
{
    if (!name.trimmed().isEmpty()) {
        return name.trimmed();
    }
    const auto base = QFileInfo(model).completeBaseName();
    return base.isEmpty() ? QStringLiteral("Model") : base;
}
QString Profile::newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool Profile::validId(const QString& id)
{
    static const QRegularExpression re("^[A-Za-z0-9_-]{1,64}$");
    return re.match(id).hasMatch();
}
QJsonObject WatchdogSettings::toJson() const
{
    return { { "autoRestart", autoRestart }, { "healthCheck", healthCheck }, { "intervalSeconds", intervalSeconds },
        { "timeoutSeconds", timeoutSeconds }, { "failureThreshold", failureThreshold }, { "startupGraceSeconds", startupGraceSeconds },
        { "cooldownSeconds", cooldownSeconds }, { "maxBackoffSeconds", maxBackoffSeconds }, { "maxRestarts", maxRestarts },
        { "windowSeconds", windowSeconds } };
}
bool WatchdogSettings::fromJson(const QJsonObject& o, WatchdogSettings& w, QString& error)
{
    WatchdogSettings candidate;
    auto flag = [&](const char* key, bool& value) {
        if (!o.contains(key)) {
            return true;
        }
        if (!o[key].isBool()) {
            error = QString("watchdog.%1 must be boolean").arg(key);
            return false;
        }
        value = o[key].toBool();
        return true;
    };
    auto number = [&](const char* key, int& value) {
        if (!o.contains(key)) {
            return true;
        }
        const double n = o[key].toDouble(-1e20);
        if (!o[key].isDouble() || n != std::floor(n) || n < 0 || n > 1e6) {
            error = QString("watchdog.%1 must be a non-negative integer").arg(key);
            return false;
        }
        value = int(n);
        return true;
    };
    if (!flag("autoRestart", candidate.autoRestart) || !flag("healthCheck", candidate.healthCheck)
        || !number("intervalSeconds", candidate.intervalSeconds) || !number("timeoutSeconds", candidate.timeoutSeconds)
        || !number("failureThreshold", candidate.failureThreshold) || !number("startupGraceSeconds", candidate.startupGraceSeconds)
        || !number("cooldownSeconds", candidate.cooldownSeconds) || !number("maxBackoffSeconds", candidate.maxBackoffSeconds)
        || !number("maxRestarts", candidate.maxRestarts) || !number("windowSeconds", candidate.windowSeconds)) {
        return false;
    }
    error = candidate.validate();
    if (!error.isEmpty()) {
        return false;
    }
    w = candidate;
    return true;
}
QString WatchdogSettings::validate() const
{
    if (intervalSeconds < 1 || intervalSeconds > 3600) {
        return "Health check interval must be 1-3600 seconds";
    }
    if (timeoutSeconds < 1 || timeoutSeconds > 300) {
        return "Health check timeout must be 1-300 seconds";
    }
    if (failureThreshold < 1 || failureThreshold > 100) {
        return "Failure threshold must be 1-100";
    }
    if (startupGraceSeconds < 1 || startupGraceSeconds > 86400) {
        return "Startup grace period must be 1-86400 seconds";
    }
    if (cooldownSeconds < 0 || cooldownSeconds > 3600) {
        return "Restart cooldown must be 0-3600 seconds";
    }
    if (maxBackoffSeconds < cooldownSeconds || maxBackoffSeconds > 86400) {
        return "Maximum backoff must be at least the cooldown and at most 86400 seconds";
    }
    if (maxRestarts < 0 || maxRestarts > 100) {
        return "Maximum restarts must be 0-100";
    }
    if (windowSeconds < 1 || windowSeconds > 86400) {
        return "Restart window must be 1-86400 seconds";
    }
    return { };
}
