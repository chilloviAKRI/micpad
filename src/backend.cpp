#include "backend.h"
#include <QDBusConnection>
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
const QRegularExpression soundCloudFile(R"(^soundcloud-([0-9]{1,20})\.[a-zA-Z0-9]+$)");

bool soundCloudShortHost(const QString &host) {
    return host == "on.soundcloud.com" || host == "snd.sc" || host == "soundcloud.app.goo.gl";
}

QUrl soundCloudUrl(QString link, bool allowWrapper = true) {
    link = link.trimmed();
    static const QRegularExpression urn(R"(^soundcloud:(?://)?(tracks|sounds|playlists)[:/]([0-9]+)/?$)",
                                        QRegularExpression::CaseInsensitiveOption);
    const auto match = urn.match(link);
    if (match.hasMatch()) {
        const auto resource = match.captured(1).toLower() == "playlists" ? "playlists/" : "tracks/";
        link = QStringLiteral("https://api.soundcloud.com/") + resource + match.captured(2);
    }
    if (link.startsWith("//")) link.prepend("https:");
    else if (!link.contains("://")) link.prepend("https://");
    QUrl url(link, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || !url.userInfo().isEmpty() || url.port() != -1) return {};

    const auto host = url.host();
    const auto parts = url.path(QUrl::FullyDecoded).split('/', Qt::SkipEmptyParts);
    url.setScheme("https");
    url.setFragment({});
    QUrlQuery query(url);

    if ((host == "w.soundcloud.com" || host == "player.soundcloud.com" || host == "p.soundcloud.com")
        && parts == QStringList{"player"}) {
        if (!allowWrapper) return {};
        auto target = soundCloudUrl(query.queryItemValue("url", QUrl::FullyDecoded), false);
        if (target.isEmpty()) return {};
        const auto token = query.queryItemValue("secret_token");
        if (!token.isEmpty()) {
            QUrlQuery targetQuery(target);
            targetQuery.removeAllQueryItems("secret_token");
            targetQuery.addQueryItem("secret_token", token);
            target.setQuery(targetQuery);
            target = soundCloudUrl(target.toString(QUrl::FullyEncoded), false);
        }
        return target;
    }
    if (soundCloudShortHost(host)) {
        if (host == "soundcloud.app.goo.gl" && allowWrapper && query.hasQueryItem("link"))
            return soundCloudUrl(query.queryItemValue("link", QUrl::FullyDecoded), false);
        return parts.isEmpty() ? QUrl{} : url;
    }
    if (host == "api.soundcloud.com" || host == "api-v2.soundcloud.com") {
        static const QRegularExpression id(R"(^[0-9]+$)");
        if (parts.size() != 2 || (parts.first() != "tracks" && parts.first() != "playlists")) return {};
        auto number = parts.at(1);
        const auto prefix = "soundcloud:" + parts.first() + ':';
        if (number.startsWith(prefix)) number.remove(0, prefix.size());
        if (!id.match(number).hasMatch()) return {};
        url.setPath('/' + parts.first() + '/' + number);
        QUrlQuery apiQuery;
        if (query.hasQueryItem("secret_token")) apiQuery.addQueryItem("secret_token", query.queryItemValue("secret_token"));
        if (apiQuery.isEmpty()) url.setQuery(QString());
        else url.setQuery(apiQuery);
        return url;
    }
    if (host != "soundcloud.com" && host != "www.soundcloud.com" && host != "m.soundcloud.com") return {};
    if (parts.size() < 2) return {};
    const bool playlist = parts.at(1) == "sets";
    const int baseLength = playlist ? 3 : 2;
    if (parts.size() < baseLength || parts.size() > baseLength+1
        || (parts.size() > baseLength && !parts.last().startsWith("s-"))) return {};
    url.setHost("soundcloud.com");
    auto path = '/' + parts.join('/');
    const auto token = query.queryItemValue("secret_token");
    static const QRegularExpression secret(R"(^s-[A-Za-z0-9_-]+$)");
    if (parts.size() == baseLength && secret.match(token).hasMatch()) path += '/' + token;
    url.setPath(path);
    // Share parameters (including ?in=...) should not turn a track into its playlist.
    url.setQuery(QString());
    return url;
}

bool usableDownload(const QFileInfo &file) {
    return file.isFile() && !file.isSymLink() && file.isReadable() && file.size() > 0;
}
}

Backend::Backend(bool offline, QObject *parent) : QObject(parent), m_offline(offline) {
    m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QFile file(m_dataPath + "/library.json");
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            m_error = tr("The library file is invalid. A backup is required before saving.");
        else {
            const auto o = document.object();
            m_clips = o.value("clips").toArray().toVariantList();
            m_microphone = o.value("microphone").toString();
            m_levels = AudioLevels::fromJson(o);
            m_autoAdvance = o.value("autoAdvance").toBool(false);
        }
    }
    if (offline && QCoreApplication::instance()->property("demo").toBool()) {
        m_clips = {QVariantMap{{"name", "Applause"}, {"path", "/sounds/applause.wav"}},
                   QVariantMap{{"name", "Air horn"}, {"path", "/sounds/air-horn.mp3"}},
                   QVariantMap{{"name", "Drum roll"}, {"path", "/sounds/drum-roll.ogg"}},
                   QVariantMap{{"name", "Mission complete"}, {"path", "/sounds/mission-complete.flac"}}};
        m_graph = Graph::parse(QJsonDocument::fromJson(R"json([
          {"id":10,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":100,"node.name":"alsa_input.demo","node.description":"Analog microphone","media.class":"Audio/Source"}}},
          {"id":20,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":200,"node.name":"voice-chat","application.name":"Voice chat","media.class":"Stream/Input/Audio"}}},
          {"id":60,"type":"PipeWire:Interface:Link","info":{"output-node-id":10,"input-node-id":20,"output-port-id":70,"input-port-id":50,"state":"active"}}
        ])json").array());
        m_microphone = "alsa_input.demo"; m_connected = true;
    }
    m_audio.setMicrophoneGain(float(m_levels.microphone));
    m_audio.setHeadphoneGain(float(m_levels.headphones));
    m_download.setProcessChannelMode(QProcess::MergedChannels);
    m_downloadTimeout.setSingleShot(true);
    m_downloadTimeout.setInterval(90000);
    connect(&m_download, &QProcess::readyRead, this, &Backend::readDownloadOutput);
    connect(&m_download, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finishDownload(status == QProcess::NormalExit && code == 0);
    });
    connect(&m_download, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_downloadError = tr("Could not start yt-dlp.");
        finishDownload(false);
    });
    connect(&m_downloadTimeout, &QTimer::timeout, this, [this] {
        m_downloadError = tr("SoundCloud download timed out.");
        m_download.kill();
    });
    connect(&m_audio, &AudioEngine::error, this, [this](const QString &message) { stop(); fail(message); });
    m_poll.setInterval(350);
    connect(&m_poll, &QTimer::timeout, this, &Backend::refresh);
    m_dumpTimeout.setSingleShot(true); m_dumpTimeout.setInterval(2500);
    connect(&m_dumpTimeout, &QTimer::timeout, this, [this] { m_dump.kill(); });
    connect(&m_dump, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { m_dumpTimeout.stop(); m_connected = false; fail(tr("pw-dump is missing.")); }
    });
    connect(&m_dump, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        m_dumpTimeout.stop();
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(m_dump.readAllStandardOutput(), &parseError);
        const auto errors = QString::fromUtf8(m_dump.readAllStandardError()).trimmed();
        if (code != 0 || exit != QProcess::NormalExit || !doc.isArray() || parseError.error != QJsonParseError::NoError) {
            m_connected = false; m_audio.setAllowed(false,false); m_audio.setRoutes({});
            if (!m_playing.isEmpty()) stop();
            m_status = tr("Cannot connect to PipeWire.");
            if (!errors.isEmpty()) m_error = errors.right(900);
            emit changed(); return;
        }
        m_connected = true;
        m_graph = Graph::parse(doc.array());
        const auto mics = microphones();
        if (m_microphone.isEmpty() && !mics.isEmpty()) {
            for (const auto &mic : mics)
                if (mic.toMap().value("name") == m_graph.defaultSource) m_microphone = m_graph.defaultSource;
            if (m_microphone.isEmpty()) m_microphone = mics.first().toMap().value("name").toString();
        }
        // A missing microphone must not stop ordinary local music playback.
        updateRoutes();
        emit changed();
    });
    connect(&m_decoder, &AudioDecoder::changed, this, &Backend::changed);
    connect(&m_decoder, &AudioDecoder::error, this, [this](const QString &message) { stop(); fail(message); });
    connect(&m_decoder, &AudioDecoder::ready, this, [this] {
        if (m_playing.isEmpty()) return;
        if (!m_audio.start(m_decoder.takeSamples())) { stop(); fail(tr("Cannot open a PipeWire playback stream.")); return; }
        m_routeWait.start(); refresh(); emit changed();
    });
    m_tick.setInterval(50);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_audio.finished()) finishPlayback();
        else if (!m_playing.isEmpty() && !busy() && !paused() && m_routeWait.isValid() && m_routeWait.elapsed() > 5000 && progress() == 0) {
            const QString message = m_outputMode == OutputMode::Microphone
                ? tr("No microphone capture connection is active. Check the calling app.")
                : m_outputMode == OutputMode::Headphones ? tr("No headphone output is active. Check your default audio output.")
                : tr("No active audio output. Check your headphones or enable microphone capture in the target app.");
            stop(); fail(message);
        }
        emit changed();
    });
    connect(&m_hotkeys, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(m_hotkeys.readAllStandardOutput() + m_hotkeys.readAllStandardError()).trimmed();
        if (code != 0) fail(out); else { m_status = tr("Global shortcuts enabled: Ctrl+Alt+1–9; Ctrl+Alt+0 stops playback."); emit changed(); }
    });
    connect(&m_hotkeys, &QProcess::errorOccurred, this, [this] { fail(tr("Cannot start the shortcut installer.")); });
    m_status = tr("Ready. You can listen without a call.");
    if (!offline) { m_poll.start(); m_tick.start(); QTimer::singleShot(0, this, &Backend::refresh); }
}

Backend::~Backend() {
    if (downloading()) {
        m_download.disconnect(this);
        m_download.kill();
        m_download.waitForFinished(1000);
    }
    stop();
    m_dump.disconnect(this); m_dump.kill(); m_dump.waitForFinished(1000);
    if (m_hotkeys.state() != QProcess::NotRunning) m_hotkeys.waitForFinished(30000);
}

QVariantList Backend::applications() const {
    QVariantList result{QVariantMap{{"serial", ""}, {"label", tr("All apps using this microphone")}}};
    return result + m_graph.applications(m_microphone);
}

void Backend::fail(const QString &message) { m_error = message; emit changed(); }

bool Backend::save() {
    if (m_offline) return true;
    QDir().mkpath(m_dataPath);
    const QString path = m_dataPath + "/library.json";
    QFile previous(path);
    if (previous.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(previous.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            previous.close();
            if (!QFile::copy(path, path + ".invalid-" + QUuid::createUuid().toString(QUuid::Id128))) {
                fail(tr("Cannot back up the invalid library.")); return false;
            }
        }
    }
    QSaveFile file(path);
    auto settings = m_levels.toJson();
    settings.insert("clips", QJsonArray::fromVariantList(m_clips));
    settings.insert("microphone", m_microphone);
    settings.insert("autoAdvance", m_autoAdvance);
    const QByteArray data = QJsonDocument(settings).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        fail(tr("Cannot save the sound library."));
        return false;
    }
    return true;
}

void Backend::setMicrophone(const QString &name) {
    if (name == m_microphone) return;
    stop(); m_microphone = name; m_target.clear(); save(); updateRoutes(); emit changed();
}
void Backend::setTarget(const QString &serial) {
    if (serial == m_target) return;
    stop(); m_target = serial; updateRoutes(); emit changed();
}
void Backend::setMicrophoneVolume(double value) {
    const double level = AudioLevels::stepped(value, m_levels.microphone);
    if (level == m_levels.microphone) return;
    m_levels.microphone = level;
    m_audio.setMicrophoneGain(float(level));
    save(); emit changed();
}
void Backend::setHeadphoneVolume(double value) {
    const double level = AudioLevels::stepped(value, m_levels.headphones);
    if (level == m_levels.headphones) return;
    m_levels.headphones = level;
    m_audio.setHeadphoneGain(float(level));
    save(); emit changed();
}
void Backend::setRepeat(bool value) {
    if (m_repeat == value) return;
    m_repeat = value;
    m_audio.setLoop(value);
    if (value && m_autoAdvance) { m_autoAdvance = false; save(); }
    emit changed();
}
void Backend::setAutoAdvance(bool value) {
    if (m_autoAdvance == value) return;
    m_autoAdvance = value;
    if (value && m_repeat) { m_repeat = false; m_audio.setLoop(false); }
    save(); emit changed();
}
QString Backend::outputMode() const {
    return m_outputMode == OutputMode::Microphone ? "microphone" : m_outputMode == OutputMode::Headphones ? "headphones" : "normal";
}

bool Backend::wantsHeadphones() const {
    return m_outputMode != OutputMode::Microphone;
}

void Backend::setPaused(bool value) {
    if (m_playing.isEmpty() || busy() || duration() <= 0 || value == paused()) return;
    m_audio.setPaused(value);
    if (!value) m_routeWait.restart();
    updateRoutes(); emit changed();
}

bool Backend::seek(double progress) {
    if (m_playing.isEmpty() || busy() || !m_audio.seek(progress)) return false;
    m_routeWait.restart(); emit changed(); return true;
}

void Backend::seekBy(double seconds) {
    if (duration() > 0 && std::isfinite(seconds)) seek((position()+seconds)/duration());
}

void Backend::refresh() {
    if (m_offline || m_dump.state() != QProcess::NotRunning) return;
    m_dump.start("pw-dump"); m_dumpTimeout.start();
}

void Backend::updateRoutes() {
    const int ownNode = m_audio.nodeId();
    const auto plan = m_graph.playbackPlan(m_microphone,m_target,ownNode,wantsHeadphones(),m_outputMode != OutputMode::Headphones);
    // Each destination has its own gate. Local playback can advance without
    // a call, while a muted or missing microphone always gets digital silence.
    m_audio.setAllowed(plan.captureReady,plan.monitorReady);
    m_audio.setRoutes(plan.links());
    m_sending = plan.captureReady && !paused();
    m_listening = plan.monitorReady && !paused();
    if (busy()) m_status = tr("Loading: %1").arg(m_playing);
    else if (m_playing.isEmpty()) m_status = tr("Ready. You can listen without a call.");
    else if (paused()) m_status = tr("Paused: %1").arg(m_playing);
    else if (m_sending && m_listening) m_status = tr("Playing for you and your callers: %1").arg(m_playing);
    else if (m_sending) m_status = tr("Sending: %1").arg(m_playing);
    else if (m_listening) m_status = tr("Playing only for you: %1").arg(m_playing);
    else m_status = tr("Connecting audio outputs…");
    const auto monitorWarning = tr("Local listening unavailable: check your default sound output.");
    if (wantsHeadphones() && ownNode >= 0) {
        if (plan.monitor.isEmpty() && m_routeWait.isValid() && m_routeWait.elapsed() > 1000) {
            m_status = monitorWarning;
            if (m_error.isEmpty()) m_error = monitorWarning;
        } else if (!plan.monitor.isEmpty() && m_error == monitorWarning) m_error.clear();
    } else if (m_error == monitorWarning) m_error.clear();
}

void Backend::chooseFiles() {
    if (!m_pickerPath.isEmpty()) return;
    auto bus = QDBusConnection::sessionBus();
    const QString token = "micpad_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1); sender.replace('.', '_');
    m_pickerPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    bus.connect("org.freedesktop.portal.Desktop", m_pickerPath, "org.freedesktop.portal.Request", "Response",
                this, SLOT(fileResponse(uint,QVariantMap)));
    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                          "org.freedesktop.portal.FileChooser", bus);
    auto watcher = new QDBusPendingCallWatcher(portal.asyncCall("OpenFile", "", tr("Add sounds"),
            QVariantMap{{"handle_token", token}, {"multiple", true}, {"modal", true}}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_pickerPath,
                "org.freedesktop.portal.Request", "Response", this, SLOT(fileResponse(uint,QVariantMap)));
            m_pickerPath.clear(); fail(tr("File chooser unavailable. Drag audio files into the window. %1").arg(reply.error().message()));
        }
        watcher->deleteLater();
    });
}

void Backend::fileResponse(uint response, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_pickerPath,
        "org.freedesktop.portal.Request", "Response", this, SLOT(fileResponse(uint,QVariantMap)));
    m_pickerPath.clear();
    if (response == 0) addUrls(qdbus_cast<QStringList>(results.value("uris")));
}

void Backend::downloadSoundCloud(const QString &link) {
    if (downloading()) return;
    const QUrl url = soundCloudUrl(link);
    if (url.isEmpty()) {
        m_downloadError = tr("Paste a SoundCloud track or playlist link.");
    } else if (const auto downloader = QStandardPaths::findExecutable("yt-dlp"); downloader.isEmpty()) {
        m_downloadError = tr("Install yt-dlp to add tracks from SoundCloud.");
    } else {
        const QDir directory(m_dataPath + "/downloads");
        if (QDir().mkpath(directory.absolutePath())) {
            auto staging = std::make_unique<QTemporaryDir>(directory.filePath(".import-XXXXXX"));
            if (staging->isValid()) {
                // Seed an import-local archive from playable entries already in the library.
                QFile archive(staging->filePath("archive.txt"));
                if (!archive.open(QIODevice::WriteOnly)) {
                    m_downloadError = tr("Cannot create the download folder.");
                    emit downloadChanged();
                    return;
                }
                QByteArray entries;
                for (const auto &clip : std::as_const(m_clips)) {
                    const QFileInfo file(clip.toMap().value("path").toString());
                    const auto match = soundCloudFile.match(file.fileName());
                    if (match.hasMatch() && file.canonicalPath() == directory.canonicalPath() && usableDownload(file))
                        entries += "soundcloud " + match.captured(1).toLatin1() + '\n';
                }
                if (archive.write(entries) != entries.size() || !archive.flush()) {
                    m_downloadError = tr("Cannot create the download folder.");
                    emit downloadChanged();
                    return;
                }
                archive.close();
                m_downloadDir = std::move(staging);
                m_downloadError.clear();
                m_downloadOutput.clear();
                m_downloadResults.clear();
                m_downloadProgress = -1;
                m_downloadTrack = m_downloadTotal = 0;
                m_downloadCanceled = m_downloadHadErrors = false;
                QString extractors = "soundcloud,soundcloud:set,soundcloud:playlist";
                if (soundCloudShortHost(url.host())) extractors += ",generic";
                m_download.setWorkingDirectory(m_downloadDir->path());
                m_downloadTimeout.start();
                m_download.start(downloader, {
                    "--ignore-config", "--no-plugin-dirs", "--no-cache-dir", "--yes-playlist",
                    "--ignore-errors", "--no-skip-unavailable-fragments",
                    "--download-archive", "archive.txt", "--use-extractors", extractors,
                    "--match-filters", "extractor = soundcloud", "--socket-timeout", "20",
                    "--no-colors", "--newline", "--progress", "--progress-delta", "0.25",
                    "--progress-template", "download:progress:%(progress._percent_str)s",
                    "--print", "before_dl:item:%(playlist_index)s/%(n_entries)s",
                    "--print", "after_move:track:%(.{id,title,filepath})j",
                    "-f", "bestaudio", "-o", "track-%(id)s.%(ext)s", "--", url.toString(QUrl::FullyEncoded)
                });
                emit downloadChanged();
                return;
            }
        }
        m_downloadError = tr("Cannot create the download folder.");
    }
    emit downloadChanged();
}

void Backend::cancelDownload() {
    if (!downloading()) return;
    m_downloadCanceled = true;
    m_download.kill();
}

void Backend::clearDownloadError() {
    if (downloading() || m_downloadError.isEmpty()) return;
    m_downloadError.clear();
    emit downloadChanged();
}

void Backend::readDownloadOutput() {
    m_downloadOutput += m_download.readAll();
    if (m_downloadTimeout.isActive()) m_downloadTimeout.start();
    const auto previousProgress = m_downloadProgress;
    const auto previousCount = m_downloadResults.size();
    qsizetype end;
    while ((end = m_downloadOutput.indexOf('\n')) >= 0) {
        const auto line = m_downloadOutput.left(end).trimmed();
        m_downloadOutput.remove(0, end+1);
        if (line.startsWith("ERROR:")) m_downloadHadErrors = true;
        if (line.startsWith("track:")) m_downloadResults.append(line.mid(6));
        if (line.startsWith("item:")) {
            const auto position = line.mid(5).split('/');
            m_downloadTrack = position.value(0).toInt();
            m_downloadTotal = position.value(1).toInt();
            m_downloadProgress = -1;
        }
        if (line.startsWith("progress:") && line.endsWith('%')) {
            bool valid;
            const double percent = line.mid(9, line.size()-10).trimmed().toDouble(&valid);
            if (valid && std::isfinite(percent)) {
                const auto fraction = std::clamp(percent/100., 0., 1.);
                m_downloadProgress = m_downloadTrack > 0 && m_downloadTotal >= m_downloadTrack
                    ? (m_downloadTrack - 1 + fraction)/m_downloadTotal : fraction;
            }
        }
    }
    if (m_downloadOutput.size() > 64*1024) m_downloadOutput.clear();
    if (m_downloadProgress != previousProgress || m_downloadResults.size() != previousCount) emit downloadChanged();
}

void Backend::finishDownload(bool success) {
    if (!downloading()) return;
    m_downloadTimeout.stop();
    readDownloadOutput();
    const int index = addDownloadedTracks();
    if (m_downloadError.isEmpty() && !m_downloadCanceled) {
        if (index >= 0 && (!success || m_downloadHadErrors))
            m_downloadError = tr("Added available tracks. Some tracks could not be downloaded.");
        else if (index < 0 && (!success || m_downloadHadErrors))
            m_downloadError = tr("Could not import audio from this SoundCloud link.");
        else if (index < 0)
            m_downloadError = tr("No new SoundCloud tracks to add.");
    }
    m_downloadDir.reset();
    emit downloadChanged();
    if (index >= 0) emit downloadAdded(index);
}

int Backend::addDownloadedTracks() {
    if (m_downloadResults.isEmpty()) return -1;
    const QDir staging(m_downloadDir->path());
    const QString stagedPath = staging.canonicalPath();
    const QDir directory(QFileInfo(stagedPath).absolutePath());
    QSet<QString> importedIds;
    QHash<QString, qsizetype> libraryRows;
    for (qsizetype i = 0; i < m_clips.size(); ++i) {
        const QFileInfo file(m_clips.at(i).toMap().value("path").toString());
        libraryRows.insert(file.absoluteFilePath(), i);
        const auto match = soundCloudFile.match(file.fileName());
        if (match.hasMatch() && file.canonicalPath() == directory.path() && usableDownload(file))
            importedIds.insert(match.captured(1));
    }

    const auto previous = m_clips;
    QStringList addedFiles;
    int first = -1;
    static const QRegularExpression validId("^[0-9]{1,20}$");
    static const QRegularExpression extension("^[a-zA-Z0-9]+$");
    for (const auto &result : std::as_const(m_downloadResults)) {
        const auto track = QJsonDocument::fromJson(result).object();
        const auto id = track.value("id").toString();
        const auto title = track.value("title").toString().simplified();
        const QFileInfo audio(staging.absoluteFilePath(track.value("filepath").toString()));
        if (!validId.match(id).hasMatch() || title.isEmpty() || !usableDownload(audio)
            || audio.canonicalPath() != stagedPath || audio.completeBaseName() != "track-" + id
            || !extension.match(audio.suffix()).hasMatch()) {
            m_downloadHadErrors = true;
            continue;
        }
        if (importedIds.contains(id)) continue;

        const auto path = directory.filePath("soundcloud-" + id + "." + audio.suffix());
        const QFileInfo destination(path);
        if (!usableDownload(destination)) {
            if (destination.exists() || destination.isSymLink() || !QFile::rename(audio.absoluteFilePath(), path)) {
                m_downloadHadErrors = true;
                continue;
            }
            addedFiles.append(path);
        }
        const auto row = libraryRows.value(path, m_clips.size());
        if (row == m_clips.size()) m_clips.append(QVariantMap{{"path", path}, {"name", title}});
        if (first < 0) first = int(row);
        importedIds.insert(id);
    }
    if (first < 0) return -1;

    if (!save()) {
        m_clips = previous;
        for (const auto &path : addedFiles) QFile::remove(path);
        m_downloadError = tr("Cannot save the sound library.");
        emit changed();
        return -1;
    }
    if (previous != m_clips) {
        m_undo.append(previous);
        m_redo.clear();
        if (m_undo.size() > 50) m_undo.removeFirst();
    }
    emit changed();
    return first;
}

void Backend::addUrls(const QStringList &urls) {
    const auto before = m_clips;
    for (const auto &url : urls) {
        const QUrl u(url);
        const QString path = u.isLocalFile() ? u.toLocalFile() : u.scheme().isEmpty() ? url : QString();
        const QFileInfo info(path);
        if (!info.isFile() || !info.isReadable()) { fail(tr("Cannot read file: %1").arg(url)); continue; }
        bool duplicate = false;
        for (const auto &clip : std::as_const(m_clips)) duplicate |= clip.toMap().value("path").toString() == info.canonicalFilePath();
        if (duplicate) continue;
        m_clips.append(QVariantMap{{"path", info.canonicalFilePath()}, {"name", info.completeBaseName()}});
    }
    if (before != m_clips) {
        m_undo.append(before); m_redo.clear();
        if (m_undo.size() > 50) m_undo.removeFirst();
    }
    save(); emit changed();
}

void Backend::play(int index) {
    playClip(index,OutputMode::Normal);
}

void Backend::preview(int index) {
    playClip(index,OutputMode::Headphones);
}

void Backend::playMicrophoneOnly(int index) {
    playClip(index,OutputMode::Microphone);
}

void Backend::setPlaybackOrder(const QVariantList &indices) {
    QStringList paths;
    for (const auto &entry : indices) {
        bool valid = false;
        const int index = entry.toInt(&valid);
        if (valid && index >= 0 && index < m_clips.size()) {
            const auto path = m_clips.at(index).toMap().value("path").toString();
            if (!paths.contains(path)) paths.append(path);
        }
    }
    m_queue.setOrder(paths);
}

void Backend::finishPlayback() {
    if (m_playing.isEmpty()) return;
    const int next = m_autoAdvance ? m_queue.next(m_clips) : -1;
    if (next < 0) stop();
    else playClip(next,m_outputMode);
}

void Backend::playClip(int index, OutputMode mode) {
    if (index < 0 || index >= m_clips.size()) return;
    stop(); clearError();
    m_outputMode = mode;
    if (!m_connected) { fail(tr("Cannot connect to PipeWire.")); return; }
    if (!wantsHeadphones()) {
        if (m_graph.applications(m_microphone).isEmpty()) {
            fail(tr("No app is recording this microphone. Start a call or microphone test.")); return;
        }
        for (const auto &n : m_graph.nodes)
            if (n.name == m_microphone && n.muted) { fail(tr("Unmute your microphone before sending sounds.")); return; }
    }
    const auto clip = m_clips[index].toMap();
    const auto path = clip.value("path").toString();
    if (!QFileInfo::exists(path)) { fail(tr("The audio file has moved or was deleted: %1").arg(path)); return; }
    m_playing = clip.value("name").toString(); m_status = tr("Loading: %1").arg(m_playing);
    m_queue.started(path);
    m_decoder.start(path); emit clipStarted(index); emit changed();
}

void Backend::stop() {
    m_playing.clear(); m_routeWait.invalidate();
    m_queue.clear();
    m_sending = false; m_listening = false;
    m_decoder.stop(); m_audio.stop();
    m_outputMode = OutputMode::Normal;
    m_status = tr("Stopped. Microphone routing is unchanged."); emit changed();
}
void Backend::remove(int index) {
    if (index < 0 || index >= m_clips.size()) return;
    stop(); remember(); m_clips.removeAt(index); save(); emit changed();
}
void Backend::rename(int index, const QString &name) {
    if (index < 0 || index >= m_clips.size() || name.trimmed().isEmpty()) return;
    remember(); auto clip = m_clips[index].toMap(); clip["name"] = name.trimmed(); m_clips[index] = clip; save(); emit changed();
}
void Backend::remember() {
    m_undo.append(m_clips); m_redo.clear();
    if (m_undo.size() > 50) m_undo.removeFirst();
}
void Backend::undo() {
    if (m_undo.isEmpty()) return;
    stop(); m_redo.append(m_clips); m_clips = m_undo.takeLast(); save(); emit changed();
}
void Backend::redo() {
    if (m_redo.isEmpty()) return;
    stop(); m_undo.append(m_clips); m_clips = m_redo.takeLast(); save(); emit changed();
}
void Backend::enableHotkeys() {
    if (m_hotkeys.state() != QProcess::NotRunning) return;
    const QString script = QStandardPaths::findExecutable("micpad-hotkeys");
    if (script.isEmpty()) { fail(tr("micpad-hotkeys is not installed.")); return; }
    m_hotkeys.start(script);
}

QString Backend::command(const QStringList &args) {
    if (args.isEmpty() || args.first() == "open") { emit openRequested(); return "ok"; }
    if (args.first() == "stop") { stop(); return "ok"; }
    if (args == QStringList{"pause"}) { setPaused(true); return "ok"; }
    if (args == QStringList{"resume"}) { setPaused(false); return "ok"; }
    if (args.first() == "seek" && args.size() == 2) {
        bool valid; const double seconds = args[1].toDouble(&valid);
        return valid && duration() > 0 && seek(seconds/duration()) ? "ok" : "error: invalid position or no sound is playing";
    }
    if (args.first() == "add" && args.size() > 1) {
        clearError(); addUrls(args.mid(1)); return m_error.isEmpty() ? "ok" : "error: " + m_error;
    }
    if (args.first() == "play" && args.size() == 2) {
        bool valid; const int index = args[1].toInt(&valid);
        if (!valid || index < 1 || index > m_clips.size()) return "error: invalid clip number";
        play(index - 1); return m_error.isEmpty() ? "ok" : "error: " + m_error;
    }
    return "error: unknown command";
}
