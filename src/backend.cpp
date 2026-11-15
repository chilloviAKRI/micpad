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
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>

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
            m_monitorEnabled = o.value("monitorEnabled").toBool(true);
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
        if (m_audio.finished()) stop();
        else if (!m_playing.isEmpty() && !busy() && m_routeWait.isValid() && m_routeWait.elapsed() > 5000 && progress() == 0) {
            stop(); fail(tr("No active audio output. Check your headphones or enable microphone capture in the target app."));
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
    stop();
    m_dump.disconnect(this); m_dump.kill(); m_dump.waitForFinished(1000);
    if (m_hotkeys.state() != QProcess::NotRunning) m_hotkeys.waitForFinished(30000);
}

QVariantList Backend::applications() const {
    QVariantList result{QVariantMap{{"serial", ""}, {"label", tr("All apps using this microphone")}}};
    return result + m_graph.applications(m_microphone);
}

void Backend::fail(const QString &message) { m_error = message; emit changed(); }

void Backend::save() {
    if (m_offline) return;
    QDir().mkpath(m_dataPath);
    const QString path = m_dataPath + "/library.json";
    QFile previous(path);
    if (previous.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(previous.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            previous.close();
            if (!QFile::copy(path, path + ".invalid-" + QUuid::createUuid().toString(QUuid::Id128))) {
                fail(tr("Cannot back up the invalid library.")); return;
            }
        }
    }
    QSaveFile file(path);
    auto settings = m_levels.toJson();
    settings.insert("clips", QJsonArray::fromVariantList(m_clips));
    settings.insert("microphone", m_microphone);
    settings.insert("monitorEnabled", m_monitorEnabled);
    const QByteArray data = QJsonDocument(settings).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        fail(tr("Cannot save the sound library."));
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
    const double level = AudioLevels::bounded(value, m_levels.microphone);
    if (level == m_levels.microphone) return;
    m_levels.microphone = level;
    m_audio.setMicrophoneGain(float(level));
    save(); emit changed();
}
void Backend::setHeadphoneVolume(double value) {
    const double level = AudioLevels::bounded(value, m_levels.headphones);
    if (level == m_levels.headphones) return;
    m_levels.headphones = level;
    m_audio.setHeadphoneGain(float(level));
    if (m_output) m_output->setVolume(level);
    save(); emit changed();
}
void Backend::setRepeat(bool value) { m_repeat = value; m_audio.setLoop(value); emit changed(); }
void Backend::setMonitorEnabled(bool value) {
    if (m_monitorEnabled == value) return;
    m_monitorEnabled = value;
    updateRoutes(); save(); emit changed();
}

void Backend::refresh() {
    if (m_offline || m_dump.state() != QProcess::NotRunning) return;
    m_dump.start("pw-dump"); m_dumpTimeout.start();
}

void Backend::updateRoutes() {
    const int ownNode = m_audio.nodeId();
    const auto plan = m_graph.playbackPlan(m_microphone,m_target,ownNode,m_monitorEnabled);
    // Each destination has its own gate. Local playback can advance without
    // a call, while a muted or missing microphone always gets digital silence.
    m_audio.setAllowed(plan.captureReady,plan.monitorReady);
    m_audio.setRoutes(plan.links());
    m_sending = plan.captureReady;
    m_listening = plan.monitorReady;
    if (busy()) m_status = tr("Loading: %1").arg(m_playing);
    else if (m_playing.isEmpty()) m_status = m_monitorEnabled
        ? tr("Ready. You can listen without a call.")
        : tr("Local listening is off. Sounds will be sent to apps using your microphone.");
    else if (m_sending && m_listening) m_status = tr("Playing for you and your callers: %1").arg(m_playing);
    else if (m_sending) m_status = tr("Sending: %1").arg(m_playing);
    else if (m_listening) m_status = tr("Playing only for you: %1").arg(m_playing);
    else m_status = tr("Connecting audio outputs…");
    const auto monitorWarning = tr("Local listening unavailable: check your default sound output.");
    if (m_monitorEnabled && ownNode >= 0) {
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
    if (index < 0 || index >= m_clips.size()) return;
    stop(); clearError();
    if (!m_connected) { fail(tr("Cannot connect to PipeWire.")); return; }
    if (!m_monitorEnabled) {
        if (m_graph.applications(m_microphone).isEmpty()) {
            fail(tr("Enable 'Hear played sounds myself' to listen without a call.")); return;
        }
        for (const auto &n : m_graph.nodes)
            if (n.name == m_microphone && n.muted) { fail(tr("Unmute your microphone before sending sounds.")); return; }
    }
    const auto clip = m_clips[index].toMap();
    const auto path = clip.value("path").toString();
    if (!QFileInfo::exists(path)) { fail(tr("The audio file has moved or was deleted: %1").arg(path)); return; }
    m_playing = clip.value("name").toString(); m_status = tr("Loading: %1").arg(m_playing);
    m_decoder.start(path); emit changed();
}

void Backend::preview(int index) {
    if (index < 0 || index >= m_clips.size()) return;
    stop();
    if (!m_connected) { fail(tr("Cannot connect to PipeWire.")); return; }
    if (!m_player) {
        m_output = new QAudioOutput(this);
        m_output->setVolume(m_levels.headphones);
        m_player = new QMediaPlayer(this);
        m_player->setAudioOutput(m_output);
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
            if (!message.isEmpty()) fail(tr("Preview: %1").arg(message));
        });
    }
    m_player->setSource(QUrl::fromLocalFile(m_clips[index].toMap().value("path").toString()));
    m_player->play();
    m_status = tr("Preview plays through your speakers or headphones only."); emit changed();
}

void Backend::stop() {
    m_playing.clear(); m_routeWait.invalidate();
    m_sending = false; m_listening = false;
    m_decoder.stop(); m_audio.stop();
    if (m_player) m_player->stop();
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
