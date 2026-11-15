#include "backend.h"
#include "theme.h"
#include "translator.h"
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <memory>
#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <csignal>

static volatile std::sig_atomic_t terminateRequested = 0;
static void onSignal(int) { terminateRequested = 1; }

int main(int argc, char **argv) {
    const QStringList raw = [&] { QStringList v; for (int i = 1; i < argc; ++i) v << QString::fromLocal8Bit(argv[i]); return v; }();
    const bool headless = !raw.isEmpty() && raw.first() != "open" && raw.first() != "--screenshot";
    if (headless || raw.contains("--screenshot")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
    }
    const bool screenshot = raw.contains("--screenshot");
    if (screenshot) qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    app.setApplicationName("micpad"); app.setOrganizationName(""); app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("micpad"); app.setWindowIcon(QIcon(":/micpad.svg"));
    auto args = raw; const bool json = args.removeAll("--json") > 0;
    if (args.value(0) == "add")
        for (qsizetype i = 1; i < args.size(); ++i)
            if (QUrl(args[i]).scheme().isEmpty()) args[i] = QFileInfo(args[i]).absoluteFilePath();
    auto output = [json](const QJsonObject &o, int code) {
        QTextStream stream(code == 0 ? stdout : stderr);
        stream << (json ? QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact))
                        : o.value("message").toString()) << '\n';
        return code;
    };
    if (args.contains("--help") || args.contains("-h")) {
        QTextStream(stdout) << "Micpad 0.1.0\nmicpad [open]\nmicpad play NUMBER [--json]\nmicpad stop [--json]\n"
                              "micpad pause [--json]\nmicpad resume [--json]\nmicpad seek SECONDS [--json]\n"
                              "micpad add FILE... [--json]\nmicpad list [--json]\nmicpad check [--json]\nmicpad skill install [--json]\n";
        return 0;
    }
    if (args.value(0) == "check") {
        QProcess dump; dump.start("pw-dump");
        if (!dump.waitForFinished(3000) || dump.exitCode() != 0) {
            dump.kill(); dump.waitForFinished(1000);
            return output({{"ok", false}, {"message", "Cannot connect to PipeWire"},
                           {"detail", QString::fromUtf8(dump.readAllStandardError())}}, 1);
        }
        const auto graph = Graph::parse(QJsonDocument::fromJson(dump.readAllStandardOutput()).array());
        bool dependencies = true;
        for (const QString &tool : {QString("ffmpeg"), QString("pw-dump")}) dependencies &= !QStandardPaths::findExecutable(tool).isEmpty();
        return output({{"ok", dependencies && !graph.microphones().isEmpty()}, {"microphones", QJsonArray::fromVariantList(graph.microphones())},
                       {"message", graph.microphones().isEmpty() ? "No physical ALSA microphone found" : "PipeWire is available"}},
                       dependencies && !graph.microphones().isEmpty() ? 0 : 1);
    }
    if (args.value(0) == "list") {
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/library.json");
        if (!file.exists()) return output({{"ok", true}, {"clips", QJsonArray()}, {"message", "No clips yet"}}, 0);
        if (!file.open(QIODevice::ReadOnly)) return output({{"ok", false}, {"message", file.errorString()}}, 1);
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return output({{"ok", false}, {"message", "Invalid library JSON"}}, 1);
        const auto clips = doc.object().value("clips").toArray();
        QStringList names;
        for (qsizetype i = 0; i < clips.size(); ++i) names << QString::number(i + 1) + ". " + clips[i].toObject().value("name").toString();
        return output({{"ok", true}, {"clips", clips}, {"message", names.join('\n')}}, 0);
    }
    if (args == QStringList{"skill", "install"}) {
        const QString root = qEnvironmentVariable("CODEX_HOME", QDir::homePath() + "/.codex");
        const QString directory = root + "/skills/micpad";
        QFile source(":/SKILL.md");
        if (!source.open(QIODevice::ReadOnly)) return output({{"ok", false}, {"message", source.errorString()}}, 1);
        const QByteArray data = source.readAll();
        QDir().mkpath(directory);
        QSaveFile file(directory + "/SKILL.md");
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            return output({{"ok", false}, {"message", file.errorString()}}, 1);
        return output({{"ok", true}, {"message", directory + "/SKILL.md"}}, 0);
    }
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) return output({{"ok", false}, {"message", "XDG_RUNTIME_DIR is unavailable"}}, 1);
    const QString socketName = runtime + "/micpad-control";
    auto send = [&](const QStringList &command) -> int {
        QLocalSocket socket; socket.connectToServer(socketName);
        if (!socket.waitForConnected(500)) return -1;
        socket.write(QJsonDocument(QJsonArray::fromStringList(command)).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        if (!socket.waitForReadyRead(3000)) return output({{"ok", false}, {"message", "Micpad did not respond"}}, 1);
        const QString response = QString::fromUtf8(socket.readAll()).trimmed();
        const bool ok = response == "ok";
        return output({{"ok", ok}, {"message", response}}, ok ? 0 : 1);
    };
    if (!screenshot) {
        const int result = send(args.isEmpty() ? QStringList{"open"} : args);
        if (result >= 0) return result;
        if (headless) return output({{"ok", false}, {"message", "Open Micpad first: micpad open"}}, 1);
    }
    QLockFile lock(runtime + "/micpad.lock"); lock.setStaleLockTime(0);
    if (!screenshot && !lock.tryLock()) return output({{"ok", false}, {"message", "Another Micpad instance is starting"}}, 1);
    QLocalServer server;
    if (!screenshot) {
        QLocalServer::removeServer(socketName);
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(socketName)) return output({{"ok", false}, {"message", server.errorString()}}, 1);
    }
    QQuickStyle::setStyle("Material");
    Translator translator; app.installTranslator(&translator);
    app.setProperty("demo", screenshot);
    Theme theme(screenshot ? qEnvironmentVariable("MICPAD_DEMO_THEME_DIR", "/tmp/micpad-demo-no-theme") : QString(),
                nullptr, {}, screenshot ? QDir::homePath()+"/.config/fontconfig" : QString(), {},
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/appearance.json");
    Backend backend(screenshot);
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto socket = server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            auto timeout = new QTimer(socket); timeout->setSingleShot(true); timeout->setInterval(3000);
            QObject::connect(timeout, &QTimer::timeout, socket, &QLocalSocket::disconnectFromServer); timeout->start();
            QObject::connect(socket, &QLocalSocket::readyRead, &backend, [&, socket, buffer] {
                buffer->append(socket->readAll());
                if (buffer->size() > 65536) { socket->disconnectFromServer(); return; }
                if (!buffer->contains('\n')) return;
                const auto array = QJsonDocument::fromJson(buffer->left(buffer->indexOf('\n'))).array();
                QStringList command; for (const auto &v : array) command << v.toString();
                socket->write(backend.command(command).toUtf8() + '\n'); socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject::connect(&backend, &Backend::openRequested, window, [window] { window->showNormal(); window->raise(); window->requestActivate(); });
    std::signal(SIGTERM, onSignal); std::signal(SIGINT, onSignal);
    QTimer signalPoll; QObject::connect(&signalPoll, &QTimer::timeout, &app, [&] { if (terminateRequested) app.quit(); }); signalPoll.start(100);
    if (screenshot) QTimer::singleShot(1000, &app, [&] {
        const QString path = args.value(args.indexOf("--screenshot") + 1);
        app.exit(window->grabWindow().save(path) ? 0 : 1);
    });
    return app.exec();
}
