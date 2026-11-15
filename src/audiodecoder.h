#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>

// A file-backed PCM recording avoids a growing private RAM buffer. Once
// sealed, its mapping stays immutable for the lifetime of the audio stream.
class PcmFile {
public:
    explicit PcmFile(const QString &directory = {});
    ~PcmFile();
    bool open();
    bool append(const QByteArray &bytes);
    bool seal();
    const float *samples() const { return reinterpret_cast<const float *>(m_mapping); }
    qsizetype count() const { return m_size / qsizetype(sizeof(float)); }
    QString errorString() const { return m_error; }
private:
    QTemporaryFile m_file;
    uchar *m_mapping = nullptr;
    qsizetype m_size = 0;
    QString m_error;
};

class AudioDecoder : public QObject {
    Q_OBJECT
public:
    explicit AudioDecoder(const QString &cacheDirectory = {}, QObject *parent = nullptr);
    ~AudioDecoder() override;
    bool busy() const { return m_process.state() != QProcess::NotRunning; }
    void start(const QString &path);
    void stop();
    std::shared_ptr<const PcmFile> takeSamples();
signals:
    void ready();
    void changed();
    void error(const QString &message);
private:
    bool drainOutput();
    void fail(const QString &message);
    QString m_directory;
    QProcess m_process;
    QTimer m_timeout;
    QByteArray m_decodeError;
    std::shared_ptr<PcmFile> m_pcm;
    bool m_stopping = false;
};
