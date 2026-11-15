#pragma once
#include "graph.h"
#include "levels.h"
#include "audiodecoder.h"
#include <QObject>
#include <atomic>
#include <pipewire/pipewire.h>

class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;
    bool start(std::shared_ptr<const PcmFile> samples);
    void stop();
    void setPaused(bool paused);
    bool paused() const { return m_paused.load(); }
    bool seek(double progress);
    double duration() const;
    double position() const;
    void setRoutes(const QList<Route> &routes);
    void setMicrophoneGain(float gain) { m_microphoneGain.store(gain); }
    void setHeadphoneGain(float gain) { m_headphoneGain.store(gain); }
    void setLoop(bool loop) { m_repeat.store(loop); }
    void setAllowed(bool microphone, bool headphones) {
        m_microphoneAllowed.store(microphone); m_headphonesAllowed.store(headphones);
    }
    int nodeId() const;
    double progress() const;
    bool finished() const { return m_finished.load(); }
signals:
    void error(const QString &message);
private:
    static void process(void *data);
    static void stateChanged(void *data, pw_stream_state oldState, pw_stream_state state, const char *error);
    static void coreError(void *data, uint32_t id, int seq, int res, const char *message);
    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_stream *m_stream = nullptr;
    spa_hook m_coreListener{};
    spa_hook m_streamListener{};
    QMap<QString, pw_proxy *> m_links;
    std::shared_ptr<const PcmFile> m_samples;
    std::atomic<qsizetype> m_cursor{0};
    std::atomic<float> m_microphoneGain{float(AudioLevels::DefaultMicrophone)};
    std::atomic<float> m_headphoneGain{float(AudioLevels::DefaultHeadphones)};
    std::atomic<bool> m_repeat{false}, m_finished{false}, m_paused{false};
    std::atomic<bool> m_microphoneAllowed{false}, m_headphonesAllowed{false};
};
