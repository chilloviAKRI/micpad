#pragma once

#include <QFileSystemWatcher>
#include <QFont>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QFont uiFont READ uiFont NOTIFY fontChanged)
    Q_PROPERTY(QFont consoleFont READ consoleFont NOTIFY fontChanged)
    Q_PROPERTY(bool syncOmarchy READ syncOmarchy WRITE setSyncOmarchy NOTIFY changed)
    Q_PROPERTY(bool omarchyAvailable READ omarchyAvailable NOTIFY changed)
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {},
                   const QString &fontconfigDirectory = {}, const QString &fontMatcher = {},
                   const QString &preferenceFile = {});
    ~Theme() override;

    QString accent() const { return m_colors.value("accent").toString(); }
    QString accentForeground() const;
    QVariantMap colors() const { return m_colors; }
    bool dark() const { return m_colors.value("mode") == "dark"; }
    int radius() const { return m_radius; }
    QFont uiFont() const;
    QFont consoleFont() const;
    bool syncOmarchy() const { return m_syncOmarchy; }
    bool omarchyAvailable() const { return m_omarchyAvailable; }
    QString preset() const { return m_preset; }
    QVariantList presets() const;
    QString error() const { return m_error; }
    void setSyncOmarchy(bool enabled);
    Q_INVOKABLE bool selectPreset(const QString &preset);
    Q_INVOKABLE void clearError();

    static QString readAccent(const QString &path);
    static QVariantMap readColors(const QString &path);

signals:
    void changed();
    void fontChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reload();
    void refreshRounding();
    void refreshFont();
    void applyPalette(bool selectionChanged = false);
    bool savePreferences(bool sync, const QString &preset);

    QString m_directory;
    QVariantMap m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
    QString m_fontDirectory, m_fontMatcher, m_fontFamily;
    bool m_systemFontConfig = false;
    QFileSystemWatcher m_fontWatcher;
    QTimer m_fontDebounce, m_fontTimeout;
    QProcess m_fontProcess;
    QString m_preferenceFile, m_preset = "micpad-dark", m_error;
    bool m_syncOmarchy = false, m_omarchyAvailable = false;
};
