#pragma once
#include "audioengine.h"
#include "graph.h"
#include "playbackqueue.h"
#include <QElapsedTimer>
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
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY changed)
    Q_PROPERTY(QString outputMode READ outputMode NOTIFY changed)
    Q_PROPERTY(double microphoneVolume READ microphoneVolume WRITE setMicrophoneVolume NOTIFY changed)
    Q_PROPERTY(double headphoneVolume READ headphoneVolume WRITE setHeadphoneVolume NOTIFY changed)
    Q_PROPERTY(int volumeSteps READ volumeSteps CONSTANT)
    Q_PROPERTY(bool repeat READ repeat WRITE setRepeat NOTIFY changed)
    Q_PROPERTY(bool autoAdvance READ autoAdvance WRITE setAutoAdvance NOTIFY changed)
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
    double duration() const { return m_audio.duration(); }
    double position() const { return m_audio.position(); }
    bool paused() const { return m_audio.paused(); }
    QString outputMode() const;
    double microphoneVolume() const { return m_levels.microphone; }
    double headphoneVolume() const { return m_levels.headphones; }
    int volumeSteps() const { return AudioLevels::Steps; }
    bool repeat() const { return m_repeat; }
    bool autoAdvance() const { return m_autoAdvance; }
    bool connected() const { return m_connected; }
    bool sending() const { return m_sending; }
    bool listening() const { return m_listening; }
    void setMicrophone(const QString &name);
    void setTarget(const QString &serial);
    void setMicrophoneVolume(double value);
    void setHeadphoneVolume(double value);
    void setRepeat(bool value);
    void setAutoAdvance(bool value);
    void setPaused(bool paused);
    Q_INVOKABLE bool seek(double progress);
    Q_INVOKABLE void seekBy(double seconds);
    Q_INVOKABLE void chooseFiles();
    Q_INVOKABLE void addUrls(const QStringList &urls);
    Q_INVOKABLE void play(int index);
    Q_INVOKABLE void preview(int index);
    Q_INVOKABLE void playMicrophoneOnly(int index);
    Q_INVOKABLE void setPlaybackOrder(const QVariantList &indices);
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
    void clipStarted(int index);
private slots:
    void fileResponse(uint response, const QVariantMap &results);
private:
    void save();
    void fail(const QString &message);
    void updateRoutes();
    void remember();
    enum class OutputMode { Normal, Microphone, Headphones };
    void playClip(int index, OutputMode mode);
    void finishPlayback();
    bool wantsHeadphones() const;
    AudioEngine m_audio;
    Graph m_graph;
    QVariantList m_clips;
    QList<QVariantList> m_undo, m_redo;
    QString m_microphone, m_target, m_status, m_error, m_playing, m_dataPath;
    QString m_pickerPath;
    QProcess m_dump, m_hotkeys;
    AudioDecoder m_decoder;
    QTimer m_poll, m_tick, m_dumpTimeout;
    OutputMode m_outputMode = OutputMode::Normal;
    QElapsedTimer m_routeWait;
    AudioLevels m_levels;
    PlaybackQueue m_queue;
    bool m_autoAdvance = false;
    bool m_repeat = false, m_connected = false, m_offline = false;
    bool m_sending = false, m_listening = false;
};
