#pragma once
#include <QAbstractListModel>
#include <QJsonObject>
#include <QUrl>
#include <QVector>

// Console lines of one model for the GUI log viewer: level classification, search,
// level filters, pausing, copy, and export. Holds at most MaxLines lines.
class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool showErrors READ showErrors WRITE setShowErrors NOTIFY filterChanged)
    Q_PROPERTY(bool showWarnings READ showWarnings WRITE setShowWarnings NOTIFY filterChanged)
    Q_PROPERTY(bool showInfo READ showInfo WRITE setShowInfo NOTIFY filterChanged)
    // While paused, new lines are held back and shown on resume.
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int heldCount READ heldCount NOTIFY countsChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY countsChanged)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY countsChanged)
    Q_PROPERTY(int infoCount READ infoCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
public:
    static constexpr int MaxLines = 5000;
    enum class Level { Info,
        Warning,
        Error };
    struct Line {
        qint64 sequence = 0;
        QString time; // ISO 8601 UTC from the Agent
        QString channel; // stdout, stderr, or agent
        QString text;
        Level level = Level::Info;
    };
    enum Role { TimeRole = Qt::UserRole + 1,
        ChannelRole,
        LevelRole,
        TextRole };
    explicit LogModel(QObject* parent = nullptr);
    // Splits one Agent log record into lines and classifies each.
    static QVector<Line> parse(const QJsonObject& record);
    static Level classify(const QString& channel, const QString& text);
    void reset(const QVector<Line>& lines);
    void append(const QVector<Line>& lines);
    int rowCount(const QModelIndex& parent = { }) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString filterText() const { return filter; }
    void setFilterText(const QString& text);
    bool showErrors() const { return errors; }
    void setShowErrors(bool value);
    bool showWarnings() const { return warnings; }
    void setShowWarnings(bool value);
    bool showInfo() const { return info; }
    void setShowInfo(bool value);
    bool paused() const { return pause; }
    void setPaused(bool value);
    int heldCount() const { return int(held.size()); }
    int errorCount() const { return counts[int(Level::Error)]; }
    int warningCount() const { return counts[int(Level::Warning)]; }
    int infoCount() const { return counts[int(Level::Info)]; }
    int totalCount() const { return int(lines.size()); }
    int visibleCount() const { return int(visible.size()); }
    // Plain text, one "time [channel] text" line each; visibleOnly applies search and filters.
    Q_INVOKABLE QString text(bool visibleOnly) const;
    // Writes text(visibleOnly) as UTF-8. Returns an error message, or an empty string.
    Q_INVOKABLE QString exportTo(const QUrl& file, bool visibleOnly) const;
    // Local time of an Agent timestamp, e.g. "14:03:07.120".
    static QString localTime(const QString& iso);
signals:
    void filterChanged();
    void pausedChanged();
    void countsChanged();

private:
    bool accepts(const Line& line) const;
    void refilter();
    void store(const QVector<Line>& incoming);
    QString format(const Line& line) const;
    QVector<Line> lines, held;
    QVector<qint64> visible; // ids of visible lines; a line's id is firstId + its index
    qint64 firstId = 0;
    int counts[3] = { 0, 0, 0 };
    QString filter;
    bool errors = true, warnings = true, info = true, pause = false;
};
