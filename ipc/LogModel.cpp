#include "LogModel.h"
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

LogModel::LogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

LogModel::Level LogModel::classify(const QString& channel, const QString& text)
{
    // llama.cpp has no structured levels on stdout/stderr (it logs almost everything to
    // stderr), so the level is inferred from the text, including "E "/"W " prefixes.
    static const QRegularExpression error(
        R"((^\s*E\s)|\b(error|errors|fatal|failed|failure|exception|panic|aborted|out of memory)\b)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression warning(
        R"((^\s*W\s)|\b(warn|warning|warnings|deprecated|unhealthy|backoff)\b)",
        QRegularExpression::CaseInsensitiveOption);
    Q_UNUSED(channel);
    if (error.match(text).hasMatch()) {
        return Level::Error;
    }
    if (warning.match(text).hasMatch()) {
        return Level::Warning;
    }
    return Level::Info;
}

QVector<LogModel::Line> LogModel::parse(const QJsonObject& record)
{
    QVector<Line> result;
    const auto channel = record["channel"].toString();
    const auto time = record["time"].toString();
    const auto sequence = record["sequence"].toInteger();
    auto parts = record["text"].toString().split('\n');
    if (!parts.isEmpty() && parts.last().isEmpty()) {
        parts.removeLast();
    }
    for (auto& part : parts) {
        if (part.endsWith('\r')) {
            part.chop(1);
        }
        result.append({ sequence, time, channel, part, classify(channel, part) });
    }
    return result;
}

QString LogModel::localTime(const QString& iso)
{
    const auto time = QDateTime::fromString(iso, Qt::ISODateWithMs);
    return time.isValid() ? time.toLocalTime().toString("HH:mm:ss.zzz") : iso;
}

bool LogModel::accepts(const Line& line) const
{
    const bool level = line.level == Level::Error ? errors : line.level == Level::Warning ? warnings
                                                                                          : info;
    return level && (filter.isEmpty() || line.text.contains(filter, Qt::CaseInsensitive));
}

void LogModel::refilter()
{
    beginResetModel();
    visible.clear();
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (accepts(lines[i])) {
            visible.append(firstId + i);
        }
    }
    endResetModel();
    emit countsChanged();
}

void LogModel::reset(const QVector<Line>& incoming)
{
    held.clear();
    beginResetModel();
    firstId += lines.size();
    lines.clear();
    visible.clear();
    counts[0] = counts[1] = counts[2] = 0;
    endResetModel();
    store(incoming);
}

void LogModel::append(const QVector<Line>& incoming)
{
    if (incoming.isEmpty()) {
        return;
    }
    if (pause) {
        held += incoming;
        if (held.size() > MaxLines) {
            held.remove(0, held.size() - MaxLines);
        }
        emit countsChanged();
        return;
    }
    store(incoming);
}

void LogModel::store(const QVector<Line>& incoming)
{
    if (incoming.isEmpty()) {
        emit countsChanged();
        return;
    }
    // Drop the oldest lines first so the model never exceeds MaxLines.
    const qsizetype overflow = lines.size() + incoming.size() - MaxLines;
    if (overflow > 0) {
        const qsizetype drop = std::min(overflow, lines.size());
        const qint64 newFirst = firstId + drop;
        qsizetype hidden = 0;
        while (hidden < visible.size() && visible[hidden] < newFirst) {
            ++hidden;
        }
        if (hidden > 0) {
            beginRemoveRows({ }, 0, int(hidden - 1));
            visible.remove(0, hidden);
            endRemoveRows();
        }
        for (qsizetype i = 0; i < drop; ++i) {
            --counts[int(lines[i].level)];
        }
        lines.remove(0, drop);
        firstId = newFirst;
    }
    const auto start = incoming.size() > MaxLines ? incoming.size() - MaxLines : 0;
    QVector<qint64> added;
    for (auto i = start; i < incoming.size(); ++i) {
        const auto& line = incoming[i];
        ++counts[int(line.level)];
        if (accepts(line)) {
            added.append(firstId + lines.size());
        }
        lines.append(line);
    }
    if (!added.isEmpty()) {
        beginInsertRows({ }, int(visible.size()), int(visible.size() + added.size() - 1));
        visible += added;
        endInsertRows();
    }
    emit countsChanged();
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(visible.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= visible.size()) {
        return { };
    }
    const auto& line = lines[visible[index.row()] - firstId];
    switch (role) {
    case TimeRole:
        return localTime(line.time);
    case ChannelRole:
        return line.channel;
    case LevelRole:
        return line.level == Level::Error ? QStringLiteral("error") : line.level == Level::Warning ? QStringLiteral("warning")
                                                                                                   : QStringLiteral("info");
    case Qt::DisplayRole:
    case TextRole:
        return line.text;
    }
    return { };
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return { { TimeRole, "time" }, { ChannelRole, "channel" }, { LevelRole, "level" }, { TextRole, "text" } };
}

void LogModel::setFilterText(const QString& text)
{
    if (filter == text) {
        return;
    }
    filter = text;
    emit filterChanged();
    refilter();
}

void LogModel::setShowErrors(bool value)
{
    if (errors == value) {
        return;
    }
    errors = value;
    emit filterChanged();
    refilter();
}

void LogModel::setShowWarnings(bool value)
{
    if (warnings == value) {
        return;
    }
    warnings = value;
    emit filterChanged();
    refilter();
}

void LogModel::setShowInfo(bool value)
{
    if (info == value) {
        return;
    }
    info = value;
    emit filterChanged();
    refilter();
}

void LogModel::setPaused(bool value)
{
    if (pause == value) {
        return;
    }
    pause = value;
    emit pausedChanged();
    if (!pause) {
        const auto pending = held;
        held.clear();
        store(pending);
    }
}

QString LogModel::format(const Line& line) const
{
    return QString("%1 [%2] %3").arg(line.time, line.channel, line.text);
}

QString LogModel::text(bool visibleOnly) const
{
    QString result;
    if (visibleOnly) {
        for (const auto id : visible) {
            result += format(lines[id - firstId]) + '\n';
        }
    } else {
        for (const auto& line : lines) {
            result += format(line) + '\n';
        }
    }
    return result;
}

QString LogModel::exportTo(const QUrl& url, bool visibleOnly) const
{
    const auto path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty()) {
        return "No file selected";
    }
    QSaveFile file(path);
    const auto bytes = text(visibleOnly).toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return "Cannot export log: " + file.errorString();
    }
    return { };
}
