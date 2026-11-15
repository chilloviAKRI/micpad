#pragma once

#include <QFileSystemWatcher>
#include <QFont>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QFont uiFont READ uiFont NOTIFY fontChanged)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_colors.value("accent").toString(); }
    QString accentForeground() const;
    QVariantMap colors() const { return m_colors; }
    bool dark() const { return m_colors.value("mode") == "dark"; }
    int radius() const { return m_radius; }
    QFont uiFont() const;

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

    QString m_directory;
    QVariantMap m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
