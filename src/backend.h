#pragma once
#include "audioengine.h"
#include "graph.h"
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList clips READ clips NOTIFY changed)
    Q_PROPERTY(QVariantList microphones READ microphones NOTIFY changed)
    Q_PROPERTY(QVariantList applications READ applications NOTIFY changed)
    Q_PROPERTY(QString microphone READ microphone WRITE setMicrophone NOTIFY changed)
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString playing READ playing NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(double microphoneVolume READ microphoneVolume WRITE setMicrophoneVolume NOTIFY changed)
    Q_PROPERTY(double headphoneVolume READ headphoneVolume WRITE setHeadphoneVolume NOTIFY changed)
    Q_PROPERTY(bool repeat READ repeat WRITE setRepeat NOTIFY changed)
    Q_PROPERTY(bool monitorEnabled READ monitorEnabled WRITE setMonitorEnabled NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool sending READ sending NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
public:
    explicit Backend(bool offline = false, QObject *parent = nullptr);
    ~Backend() override;
    QVariantList clips() const { return m_clips; }
    QVariantList microphones() const { return m_graph.microphones(); }
    QVariantList applications() const;
    QString microphone() const { return m_microphone; }
    QString target() const { return m_target; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString playing() const { return m_playing; }
    bool busy() const { return m_decoder.busy(); }
    double progress() const { return m_audio.progress(); }
    double microphoneVolume() const { return m_levels.microphone; }
    double headphoneVolume() const { return m_levels.headphones; }
    bool repeat() const { return m_repeat; }
    bool monitorEnabled() const { return m_monitorEnabled; }
    bool connected() const { return m_connected; }
    bool sending() const { return m_sending; }
    bool listening() const { return m_listening; }
    void setMicrophone(const QString &name);
    void setTarget(const QString &serial);
    void setMicrophoneVolume(double value);
    void setHeadphoneVolume(double value);
    void setRepeat(bool value);
    void setMonitorEnabled(bool value);
    Q_INVOKABLE void chooseFiles();
    Q_INVOKABLE void addUrls(const QStringList &urls);
    Q_INVOKABLE void play(int index);
    Q_INVOKABLE void preview(int index);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void rename(int index, const QString &name);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearError() { m_error.clear(); emit changed(); }
    Q_INVOKABLE void enableHotkeys();
    Q_INVOKABLE void refresh();
    QString command(const QStringList &arguments);
signals:
    void changed();
    void openRequested();
private slots:
    void fileResponse(uint response, const QVariantMap &results);
private:
    void save();
    void fail(const QString &message);
    void updateRoutes();
    void remember();
    AudioEngine m_audio;
    Graph m_graph;
    QVariantList m_clips;
    QList<QVariantList> m_undo, m_redo;
    QString m_microphone, m_target, m_status, m_error, m_playing, m_dataPath;
    QString m_pickerPath;
    QProcess m_dump, m_hotkeys;
    AudioDecoder m_decoder;
    QTimer m_poll, m_tick, m_dumpTimeout;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    QElapsedTimer m_routeWait;
    AudioLevels m_levels;
    bool m_repeat = false, m_connected = false, m_offline = false;
    bool m_monitorEnabled = true;
    bool m_sending = false, m_listening = false;
};
