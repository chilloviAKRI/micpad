#include "audiodecoder.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <limits>
#include <utility>
#include <sys/mman.h>

PcmFile::PcmFile(const QString &directory)
    : m_file(QDir(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                      : directory).filePath("playback-XXXXXX.pcm")) {}

PcmFile::~PcmFile() {
    if (m_mapping) m_file.unmap(m_mapping);
}

bool PcmFile::open() {
    if (!QDir().mkpath(QFileInfo(m_file.fileTemplate()).absolutePath()) || !m_file.open()) {
        m_error = QCoreApplication::translate("PcmFile", "Cannot create a temporary audio file.");
        return false;
    }
    // Linux keeps an unlinked file alive through its open descriptor. It is
    // reclaimed even if Micpad crashes or is killed during decoding/playback.
    if (!QFile::remove(m_file.fileName())) { m_error = m_file.errorString(); return false; }
    m_file.setAutoRemove(false);
    return true;
}

bool PcmFile::append(const QByteArray &bytes) {
    if (m_mapping || !m_file.isOpen()) return false;
    if (m_file.write(bytes) != bytes.size()) { m_error = m_file.errorString(); return false; }
    return true;
}

bool PcmFile::seal() {
    if (m_mapping) return true;
    if (!m_file.flush()) { m_error = m_file.errorString(); return false; }
    const qint64 size = m_file.size();
    if (size <= 0 || size % sizeof(float) || size > std::numeric_limits<qsizetype>::max()) {
        m_error = QCoreApplication::translate("PcmFile", "Decoded audio is empty or incomplete.");
        return false;
    }
    m_mapping = m_file.map(0, size);
    if (!m_mapping) { m_error = m_file.errorString(); return false; }
    m_size = qsizetype(size);
    // Let the kernel read ahead and reclaim consumed pages on long tracks.
    madvise(m_mapping, size, MADV_SEQUENTIAL);
    return true;
}

AudioDecoder::AudioDecoder(const QString &cacheDirectory, QObject *parent)
    : QObject(parent), m_directory(cacheDirectory) {
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(45000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("Audio decoding timed out.")); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { drainOutput(); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_decodeError.append(m_process.readAllStandardError());
        if (m_decodeError.size() > 4096) m_decodeError = m_decodeError.right(4096);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_stopping && error == QProcess::FailedToStart) fail(tr("ffmpeg is missing."));
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop();
        if (m_stopping || !m_pcm || !drainOutput()) return;
        m_timeout.stop();
        if (code != 0 || status != QProcess::NormalExit) {
            fail(tr("Cannot decode audio: %1").arg(QString::fromUtf8(m_decodeError).trimmed()));
            return;
        }
        if (!m_pcm->seal()) { fail(tr("Cannot prepare audio: %1").arg(m_pcm->errorString())); return; }
        emit ready();
        emit changed();
    });
}

AudioDecoder::~AudioDecoder() { stop(); }

void AudioDecoder::start(const QString &path) {
    stop();
    m_pcm = std::make_shared<PcmFile>(m_directory);
    if (!m_pcm->open()) { fail(tr("Cannot prepare audio: %1").arg(m_pcm->errorString())); return; }
    m_decodeError.clear();
    m_process.start("ffmpeg", {"-nostdin", "-hide_banner", "-loglevel", "error", "-i", path,
        "-vn", "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "1", "-ar", "48000", "pipe:1"});
    m_timeout.start();
    emit changed();
}

bool AudioDecoder::drainOutput() {
    if (m_stopping || !m_pcm) return false;
    m_process.setReadChannel(QProcess::StandardOutput);
    while (m_process.bytesAvailable() > 0) {
        if (!m_pcm->append(m_process.read(65536))) {
            fail(tr("Cannot prepare audio: %1").arg(m_pcm->errorString())); return false;
        }
    }
    // This is a stalled-decoder timeout, never a limit on track duration or
    // total preparation time for a long recording.
    m_timeout.start();
    return true;
}

void AudioDecoder::stop() {
    m_stopping = true;
    m_timeout.stop();
    if (busy()) { m_process.kill(); m_process.waitForFinished(1000); }
    m_pcm.reset();
    m_stopping = false;
}

void AudioDecoder::fail(const QString &message) { stop(); emit error(message); emit changed(); }

std::shared_ptr<const PcmFile> AudioDecoder::takeSamples() {
    return std::exchange(m_pcm, {});
}
