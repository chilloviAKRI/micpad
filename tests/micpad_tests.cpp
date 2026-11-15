#include "graph.h"
#include "pcm.h"
#include "backend.h"
#include "theme.h"
#include "translator.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFontMetricsF>
#include <QScopeGuard>
#include <QSaveFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <limits>
#include <cstdio>

class PlaybackUiBackend : public Backend {
    Q_OBJECT
    Q_PROPERTY(QString playing READ displayedClip NOTIFY changed)
    Q_PROPERTY(bool busy READ loading NOTIFY changed)
public:
    PlaybackUiBackend() : Backend(true) {}
    QString displayedClip() const { return m_clip; }
    bool loading() const { return m_loading; }
    void showTransport(const QString &clip, bool loading) {
        m_clip = clip; m_loading = loading; emit changed();
    }
private:
    QString m_clip;
    bool m_loading = false;
};

class MicpadTests : public QObject {
    Q_OBJECT
    bool saveFile(const QString &path, const QByteArray &data) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
    }
    QByteArray themePalette(bool light) {
        return light ? R"toml(mode = "light"
accent = "#1e66f5"
selection = "#ccd0da"
muted = "#acb0be"
background = "#eff1f5"
dark_background = "#e3e4e8"
darker_background = "#d7d8dc"
lighter_background = "#dce0e8"
foreground = "#4c4f69"
light_foreground = "#5c5f77"
red = "#d20f39"
yellow = "#df8e1d"
green = "#40a02b"
blue = "#1e66f5"
)toml" : R"toml(mode = "dark"
accent = "#7daea3"
selection = "#504945"
muted = "#665c54"
background = "#282828"
dark_background = "#1e1e1e"
darker_background = "#161616"
lighter_background = "#3c3836"
foreground = "#d4be98"
light_foreground = "#bdae93"
red = "#ea6962"
yellow = "#d8a657"
green = "#a9b665"
blue = "#7daea3"
)toml";
    }
    QJsonArray fixture() {
        return QJsonDocument::fromJson(R"json([
          {"id":10,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":100,"node.name":"alsa_input.analog-stereo","node.description":"Analog mic","media.class":"Audio/Source"},"params":{"Props":[{"mute":false}]}}},
          {"id":11,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":101,"node.name":"alsa_input.usb","media.class":"Audio/Source"}}},
          {"id":12,"type":"PipeWire:Interface:Node","info":{"props":{"node.name":"virtual-source","media.class":"Audio/Source"}}},
          {"id":13,"type":"PipeWire:Interface:Node","info":{"props":{"node.name":"alsa_output.analog-stereo","device.api":"alsa","media.class":"Audio/Sink"}}},
          {"id":20,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":200,"node.name":"voice-client","application.name":"Voice chat","media.class":"Stream/Input/Audio"}}},
          {"id":21,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":201,"node.name":"usb-recorder","media.class":"Stream/Input/Audio"}}},
          {"id":30,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":300,"node.name":"micpad.clips","media.class":"Stream/Output/Audio"}}},
          {"id":40,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":30,"object.serial":400,"port.direction":"out","format.dsp":"32 bit float mono audio","audio.channel":"FL"}}},
          {"id":41,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":30,"object.serial":401,"port.direction":"out","format.dsp":"32 bit float mono audio","audio.channel":"FR"}}},
          {"id":50,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":20,"object.serial":500,"port.direction":"in","format.dsp":"32 bit float mono audio"}}},
          {"id":51,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":20,"object.serial":501,"port.direction":"in","format.dsp":"32 bit float mono audio"}}},
          {"id":52,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":21,"object.serial":502,"port.direction":"in","format.dsp":"32 bit float mono audio"}}},
          {"id":60,"type":"PipeWire:Interface:Link","info":{"output-node-id":10,"input-node-id":20,"output-port-id":70,"input-port-id":50,"state":"active"}},
          {"id":61,"type":"PipeWire:Interface:Link","info":{"output-node-id":10,"input-node-id":20,"output-port-id":71,"input-port-id":51,"state":"active"}},
          {"id":62,"type":"PipeWire:Interface:Link","info":{"output-node-id":11,"input-node-id":21,"output-port-id":72,"input-port-id":52,"state":"active"}},
          {"id":63,"type":"PipeWire:Interface:Metadata","metadata":[{"key":"default.audio.source","value":{"name":"alsa_input.analog-stereo"}}]}
        ])json").array();
    }
    Graph monitorFixture() {
        auto objects = fixture();
        const auto output = QJsonDocument::fromJson(R"json([
          {"id":14,"type":"PipeWire:Interface:Node","info":{"props":{"object.serial":140,"node.name":"bluez_output.headphones","media.class":"Audio/Sink"}}},
          {"id":80,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":13,"object.serial":800,"port.direction":"in","format.dsp":"32 bit float mono audio","audio.channel":"FL"}}},
          {"id":81,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":13,"object.serial":801,"port.direction":"in","format.dsp":"32 bit float mono audio","audio.channel":"FR"}}},
          {"id":82,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":13,"object.serial":802,"port.direction":"out","format.dsp":"32 bit float mono audio","audio.channel":"FL"}}},
          {"id":90,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":14,"object.serial":900,"port.direction":"in","format.dsp":"32 bit float mono audio","audio.channel":"FL"}}},
          {"id":91,"type":"PipeWire:Interface:Port","info":{"props":{"node.id":14,"object.serial":901,"port.direction":"in","format.dsp":"32 bit float mono audio","audio.channel":"FR"}}},
          {"id":95,"type":"PipeWire:Interface:Metadata","metadata":[{"key":"default.audio.sink","value":{"name":"alsa_output.analog-stereo"}}]}
        ])json").array();
        for (const auto &object : output) objects.append(object);
        return Graph::parse(objects);
    }
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle("Material");
    }
    void longAudioDecodesToTheEndWithoutKeepingPcmInAByteArray() {
        QTemporaryDir dir;
        const QString input = dir.path()+"/long.flac";
        QProcess generator;
        generator.start("ffmpeg", {"-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
            "sine=frequency=440:sample_rate=8000:duration=185", "-c:a", "flac", input});
        QVERIFY(generator.waitForFinished(15000));
        QCOMPARE(generator.exitCode(), 0);
        const QString cache = dir.path()+"/cache";
        AudioDecoder decoder(cache);
        QSignalSpy ready(&decoder, &AudioDecoder::ready), errors(&decoder, &AudioDecoder::error);
        decoder.start(input);
        QVERIFY(decoder.busy());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 15000);
        QCOMPARE(errors.count(), 0); QVERIFY(!decoder.busy());
        auto pcm = decoder.takeSamples(); QVERIFY(pcm);
        QCOMPARE(pcm->count(), qsizetype(185)*48000);
        QVERIFY(!decoder.takeSamples());
        // Check the actual tail past the old cutoff, both independent feeds,
        // and wrapping the same recording back to its beginning.
        std::size_t cursor = std::size_t(pcm->count())-480;
        float output[960];
        QVERIFY(Pcm::render(pcm->samples(), pcm->count(), cursor, output, 480, .2f, .6f, false, true, true));
        QCOMPARE(cursor, std::size_t(pcm->count()));
        bool audibleTail = false;
        for (int i = 0; i < 480; ++i) {
            const auto value = pcm->samples()[pcm->count()-480+i];
            QVERIFY(qAbs(output[2*i]-value*.2f) < .000001f);
            QVERIFY(qAbs(output[2*i+1]-value*.6f) < .000001f);
            audibleTail |= qAbs(output[2*i+1]) > .01f;
        }
        QVERIFY(audibleTail);
        cursor = std::size_t(pcm->count())-10;
        QVERIFY(!Pcm::render(pcm->samples(), pcm->count(), cursor, output, 480, .2f, .6f, true, true, true));
        QCOMPARE(cursor, std::size_t(470));
        QVERIFY(!Pcm::render(pcm->samples(), pcm->count(), cursor, output, 480, .2f, .6f, true, false, false));
        QCOMPARE(cursor, std::size_t(470));
        // The cache has no named PCM files, even while the recording is alive.
        QCOMPARE(QDir(cache).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
        std::weak_ptr<const PcmFile> lifetime = pcm;
        decoder.stop();
        QVERIFY(!lifetime.expired());
        pcm.reset(); QVERIFY(lifetime.expired());
    }
    void decoderCancelsAndRecoversWithoutStalePlaybackOrCacheFiles() {
        QTemporaryDir dir;
        const QString input = dir.path()+"/sound.wav";
        QProcess generator;
        generator.start("ffmpeg", {"-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
            "sine=frequency=440:sample_rate=48000:duration=1", input});
        QVERIFY(generator.waitForFinished(10000)); QCOMPARE(generator.exitCode(), 0);
        AudioDecoder decoder(dir.path()+"/cache");
        QSignalSpy ready(&decoder, &AudioDecoder::ready), errors(&decoder, &AudioDecoder::error);
        decoder.start(input); decoder.stop();
        QTest::qWait(50);
        QVERIFY(!decoder.busy()); QVERIFY(!decoder.takeSamples());
        QCOMPARE(ready.count(), 0); QCOMPARE(errors.count(), 0);
        decoder.start(dir.path()+"/missing.mp3");
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
        QVERIFY(!decoder.busy()); QVERIFY(!decoder.takeSamples()); QCOMPARE(ready.count(), 0);
        errors.clear();
        decoder.start(input);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QCOMPARE(errors.count(), 0); QVERIFY(!decoder.busy());
        auto pcm = decoder.takeSamples(); QVERIFY(pcm); QCOMPARE(pcm->count(), qsizetype(48000));
        decoder.stop(); pcm.reset();
        QCOMPARE(QDir(dir.path()+"/cache").entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
        QVERIFY(saveFile(dir.path()+"/blocked", "file"));
        AudioDecoder blocked(dir.path()+"/blocked");
        QSignalSpy cacheError(&blocked, &AudioDecoder::error);
        blocked.start(input);
        QCOMPARE(cacheError.count(), 1); QVERIFY(!blocked.busy()); QVERIFY(!blocked.takeSamples());
    }
    void fileBackedPcmRejectsEmptyAndIncompleteDecodedAudio() {
        QTemporaryDir dir;
        PcmFile empty(dir.path()); QVERIFY(empty.open()); QVERIFY(!empty.seal());
        PcmFile partial(dir.path()); QVERIFY(partial.open());
        QVERIFY(partial.append("123")); QVERIFY(!partial.seal());
        PcmFile valid(dir.path()); QVERIFY(valid.open());
        const float values[] = { .5f, -.25f, 0.f };
        const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(values), sizeof(values));
        // Pipe reads can split a sample; only the completed recording needs
        // to be aligned to whole float samples.
        QVERIFY(valid.append(bytes.first(3))); QVERIFY(valid.append(bytes.sliced(3)));
        QVERIFY(valid.seal()); QCOMPARE(valid.count(), qsizetype(3));
        for (int i = 0; i < 3; ++i) QCOMPARE(valid.samples()[i], values[i]);
        QVERIFY(!valid.append(bytes));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).size(), 0);
    }
    void themeReadsFullPaletteAndValidatesFallbacks() {
        QTemporaryDir dir;
        const QString path = dir.path()+"/colors.toml";
        QVERIFY(saveFile(path, themePalette(false)));
        const auto dark = Theme::readColors(path);
        QCOMPARE(dark.value("mode").toString(), "dark");
        QCOMPARE(dark.value("background").toString(), "#282828");
        QCOMPARE(dark.value("red").toString(), "#ea6962");
        QCOMPARE(dark.value("selection_foreground").toString(), "#d4be98");
        QVERIFY(saveFile(path, themePalette(true)));
        const auto light = Theme::readColors(path);
        QCOMPARE(light.value("mode").toString(), "light");
        QCOMPARE(light.value("dark_background").toString(), "#e3e4e8");
        QCOMPARE(light.value("green").toString(), "#40a02b");
        QVERIFY(saveFile(path, "# mode = 'dark'\nbackground = '#FFFFFF' # infer light\naccent = '#aBc123'\nforeground = 'invalid'\nred = '#12345'\nselection = '#000000'\nblue = '#123456'unexpected\n"));
        const auto partial = Theme::readColors(path);
        QCOMPARE(partial.value("mode").toString(), "light");
        QCOMPARE(partial.value("accent").toString(), "#abc123");
        QCOMPARE(partial.value("selection_foreground").toString(), "#ffffff");
        QVERIFY(partial.value("blue") != "#123456");
        QVERIFY(partial.value("red") != "#12345");
        for (const auto &colors : {dark, light, partial, Theme::readColors(dir.path()+"/missing")}) {
            for (auto it = colors.cbegin(); it != colors.cend(); ++it)
                if (it.key() != "mode") QVERIFY2(QColor(it.value().toString()).isValid(), qPrintable(it.key()));
        }
        QCOMPARE(Theme::readAccent(dir.path()+"/missing"), "#FFD60A");
    }
    void themeWatchesAtomicWritesDirectoriesAndSymlinkSwaps() {
        QTemporaryDir dir;
        const QString current = dir.path()+"/current";
        const QString path = current+"/theme/colors.toml";
        QVERIFY(saveFile(path, themePalette(false)));
        Theme theme(current);
        QSignalSpy changes(&theme, &Theme::changed);
        QCOMPARE(theme.colors().value("background").toString(), "#282828");
        // Saving only a non-accent field must still notify every palette binding.
        auto modified = themePalette(false); modified.replace("#d4be98", "#eeddbb");
        QVERIFY(saveFile(path, modified));
        QTRY_COMPARE(theme.colors().value("foreground").toString(), "#eeddbb");
        QVERIFY(changes.count() > 0);
        QCOMPARE(theme.accent(), "#7daea3");
        // Omarchy stages a new directory, then replaces current/theme.
        QVERIFY(saveFile(current+"/next-theme/colors.toml", themePalette(true)));
        QVERIFY(QDir(current+"/theme").removeRecursively());
        QVERIFY(QDir().rename(current+"/next-theme", current+"/theme"));
        QTRY_VERIFY(!theme.dark());
        QCOMPARE(theme.colors().value("background").toString(), "#eff1f5");
        // Support installations that switch the theme or current symlink, too.
        QVERIFY(saveFile(dir.path()+"/dark/colors.toml", themePalette(false)));
        QVERIFY(QDir(current+"/theme").removeRecursively());
        QVERIFY(QFile::link(dir.path()+"/dark", current+"/theme"));
        QTRY_VERIFY(theme.dark());
        QVERIFY(saveFile(dir.path()+"/light/colors.toml", themePalette(true)));
        QVERIFY(QFile::link(dir.path()+"/light", current+"/next-theme"));
        QCOMPARE(std::rename(QFile::encodeName(current+"/next-theme").constData(), QFile::encodeName(current+"/theme").constData()), 0);
        QTRY_VERIFY(!theme.dark());
        QVERIFY(saveFile(dir.path()+"/replacement/theme/colors.toml", themePalette(false)));
        QVERIFY(QDir().rename(current, dir.path()+"/previous"));
        QVERIFY(QFile::link(dir.path()+"/replacement", current));
        QTRY_VERIFY(theme.dark());
        QVERIFY(saveFile(current+"/theme/colors.toml", themePalette(true)));
        QTRY_VERIFY(!theme.dark());
    }
    void realQmlChangesThemeLiveWithoutResizingOrInterruptingPlayback() {
        QTemporaryDir dir;
        const QString path = dir.path()+"/theme/colors.toml";
        QVERIFY(saveFile(path, themePalette(false)));
        PlaybackUiBackend backend; Theme theme(dir.path()); Translator translator(dir.path()+"/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        int warnings = 0;
        connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &error : errors) qWarning() << error;
        });
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        for (const QString &name : {QString("Airhorn"), QString("Applause"), QString("Drum roll")}) {
            const auto file = dir.path()+"/"+name+".wav";
            QVERIFY(saveFile(file, "demo")); backend.addUrls({file});
        }
        QTRY_COMPARE(window->color(), QColor("#282828"));
        QCOMPARE(window->property("paper").value<QColor>(), QColor("#1e1e1e"));
        QCOMPARE(window->property("ink").value<QColor>(), QColor("#d4be98"));
        const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR");
        auto snapshot = [&](const QString &name) {
            if (!capture.isEmpty()) { QDir().mkpath(capture); window->grabWindow().save(capture+"/"+name+".png"); }
        };
        QTest::qWait(80); snapshot("theme-gruvbox");
        window->resize(1260,1390); QTest::qWait(30);
        const auto size = window->size();
        QSignalSpy resized(window, &QWindow::heightChanged);
        backend.showTransport("Applause", false);
        auto preferences = window->findChild<QObject *>("preferencesDialog"); QVERIFY(preferences);
        QVERIFY(QMetaObject::invokeMethod(preferences, "open"));
        QTRY_VERIFY(preferences->property("opened").toBool());
        QVERIFY(saveFile(path, themePalette(true)));
        QTRY_COMPARE(window->color(), QColor("#eff1f5"));
        QCOMPARE(window->property("paper").value<QColor>(), QColor("#e3e4e8"));
        QCOMPARE(window->property("ink").value<QColor>(), QColor("#4c4f69"));
        QCOMPARE(window->property("selection").value<QColor>(), QColor("#ccd0da"));
        QCOMPARE(window->property("blue").value<QColor>(), QColor("#1e66f5"));
        QVERIFY(!window->property("darkMode").toBool());
        auto background = preferences->property("background").value<QQuickItem *>(); QVERIFY(background);
        QCOMPARE(background->property("color").value<QColor>(), QColor("#eff1f5"));
        auto monitor = window->findChild<QObject *>("monitorCheckBox"); QVERIFY(monitor);
        auto checkboxText = monitor->property("contentItem").value<QQuickItem *>(); QVERIFY(checkboxText);
        QCOMPARE(checkboxText->property("color").value<QColor>(), QColor("#4c4f69"));
        QCOMPARE(window->size(), size); QCOMPARE(resized.count(), 0);
        QCOMPARE(backend.displayedClip(), "Applause");
        QVERIFY(preferences->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(preferences, "close"));
        QTRY_VERIFY(!preferences->property("visible").toBool());
        window->resize(1040,700); QTest::qWait(80); snapshot("theme-latte");
        auto menu = window->findChild<QObject *>("soundContextMenu"); QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto menuBackground = menu->property("background").value<QQuickItem *>(); QVERIFY(menuBackground);
        QCOMPARE(menuBackground->property("color").value<QColor>(), QColor("#eff1f5"));
        QVERIFY(saveFile(path, themePalette(false)));
        QTRY_COMPARE(menuBackground->property("color").value<QColor>(), QColor("#282828"));
        QVERIFY(menu->property("opened").toBool());
        snapshot("theme-gruvbox-menu");
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(saveFile(path, themePalette(true)));
        QTRY_COMPARE(window->color(), QColor("#eff1f5"));
        QVERIFY(QMetaObject::invokeMethod(preferences, "open"));
        QTRY_VERIFY(preferences->property("opened").toBool());
        snapshot("theme-latte-preferences");
        QVERIFY(saveFile(path, themePalette(false)));
        QTRY_COMPARE(window->color(), QColor("#282828"));
        QTest::qWait(80); snapshot("theme-gruvbox-preferences");
        QCOMPARE(warnings, 0);
    }
    void selectPhysicalMicrophonesOnly() {
        const auto g = Graph::parse(fixture());
        QCOMPARE(g.microphones().size(), 2);
        QCOMPARE(g.defaultSource, "alsa_input.analog-stereo");
        QCOMPARE(g.applications("alsa_input.analog-stereo").size(), 1);
        QCOMPARE(g.applications("alsa_input.analog-stereo").first().toMap().value("label").toString(), "Voice chat");
    }
    void keepBothMicChannelsAndAvoidOtherMicrophones() {
        const auto g = Graph::parse(fixture());
        const auto routes = g.routes("alsa_input.analog-stereo", "", 30);
        QCOMPARE(routes.size(), 2);
        for (const auto &r : routes) { QCOMPARE(r.outputNode, 30); QCOMPARE(r.inputNode, 20); QCOMPARE(r.outputPort, 40); }
        QCOMPARE(g.links.size(), 3);
        QVERIFY(g.routes("alsa_input.analog-stereo", "201", 30).isEmpty());
        QCOMPARE(g.routes("alsa_input.usb", "201", 30).size(), 1);
        QVERIFY(g.routes("virtual-source", "", 30).isEmpty());
    }
    void muteAndUnavailablePortsBlockInjection() {
        auto g = Graph::parse(fixture());
        g.nodes[10].muted = true;
        QVERIFY(g.routes("alsa_input.analog-stereo", "", 30).isEmpty());
        g.nodes[10].muted = false;
        g.ports[50].dsp = "16 bit interleaved audio"; g.ports[51].direction = "out";
        QVERIFY(g.routes("alsa_input.analog-stereo", "", 30).isEmpty());
        QVERIFY(g.routes("alsa_input.analog-stereo", "", -1).isEmpty());
    }
    void reusedPortGetsANewRouteIdentity() {
        auto g = Graph::parse(fixture());
        const auto oldKey = g.routes("alsa_input.analog-stereo", "", 30).first().key;
        g.ports[50].serial = "900";
        QVERIFY(g.routes("alsa_input.analog-stereo", "", 30).first().key != oldKey);
    }
    void existingActiveLinkIsRequiredBeforePlayback() {
        auto g = Graph::parse(fixture());
        auto routes = g.routes("alsa_input.analog-stereo", "", 30);
        QVERIFY(!g.hasActiveRoute(30, routes));
        g.links.append({30, 20, 40, 50, "paused"});
        QVERIFY(!g.hasActiveRoute(30, routes));
        g.links.last().state = "active";
        QVERIFY(g.hasActiveRoute(30, routes));
        g.links.last().inputPort = 52;
        QVERIFY(!g.hasActiveRoute(30, routes));
    }
    void localListeningUsesOnlyDefaultPlaybackInputs() {
        const auto g = monitorFixture();
        QCOMPARE(g.defaultSource, "alsa_input.analog-stereo");
        QCOMPARE(g.defaultSink, "alsa_output.analog-stereo");
        const auto capture = g.routes("alsa_input.analog-stereo", "", 30);
        const auto monitor = g.monitorRoutes(30);
        QCOMPARE(capture.size(), 2); QCOMPARE(monitor.size(), 2);
        QSet<int> inputs;
        for (const auto &route : monitor) {
            QCOMPARE(route.outputNode, 30); QCOMPARE(route.outputPort, 41);
            QCOMPARE(route.inputNode, 13); inputs.insert(route.inputPort);
        }
        QCOMPARE(inputs, (QSet<int>{80,81}));
        QCOMPARE(g.links.size(), 3);
        const QJsonObject encodedMetadata{{"id", 96}, {"type", "PipeWire:Interface:Metadata"},
            {"metadata", QJsonArray{QJsonObject{{"key", "default.audio.sink"},
            {"value", QString("{\"name\":\"alsa_output.analog-stereo\"}")}}}}};
        QCOMPARE(Graph::parse(QJsonArray{encodedMetadata}).defaultSink, g.defaultSink);
    }
    void outputSwitchReplacesOnlyOwnMonitorDestinations() {
        auto g = monitorFixture();
        const auto capture = g.routes("alsa_input.analog-stereo", "", 30);
        const auto old = g.monitorRoutes(30);
        g.defaultSink = "bluez_output.headphones";
        const auto next = g.monitorRoutes(30);
        QCOMPARE(next.size(), 2);
        for (const auto &r : next) {
            QCOMPARE(r.inputNode, 14);
            for (const auto &previous : old) QVERIFY(r.key != previous.key);
        }
        QCOMPARE(g.routes("alsa_input.analog-stereo", "", 30).size(), capture.size());
        QCOMPARE(g.links.size(), 3);
        g.defaultSink = "missing-output"; QVERIFY(g.monitorRoutes(30).isEmpty());
        g.defaultSink.clear(); QVERIFY(g.monitorRoutes(30).isEmpty());
        QVERIFY(g.monitorRoutes(-1).isEmpty());
    }
    void localOutputDoesNotBypassMicReadinessOrMute() {
        auto g = monitorFixture();
        g.links.append({30,13,41,80,"active"});
        QVERIFY(g.hasActiveRoute(30, g.monitorRoutes(30)));
        QVERIFY(!g.hasActiveRoute(30, g.routes("alsa_input.analog-stereo", "", 30)));
        g.links.append({30,20,40,50,"active"});
        QVERIFY(g.hasActiveRoute(30, g.routes("alsa_input.analog-stereo", "", 30)));
        g.nodes[10].muted = true;
        QVERIFY(g.routes("alsa_input.analog-stereo", "", 30).isEmpty());
        QVERIFY(!g.hasActiveRoute(30, g.routes("alsa_input.analog-stereo", "", 30)));
    }
    void localPlaybackWorksWithoutACallAndTracksCallChanges() {
        auto graph = monitorFixture();
        graph.links.clear();
        graph.links.append({30,13,41,80,"active"});
        auto plan = graph.playbackPlan("alsa_input.analog-stereo","",30,true);
        QVERIFY(plan.capture.isEmpty()); QVERIFY(!plan.captureReady);
        QVERIFY(plan.monitorReady); QVERIFY(plan.canAdvance());
        QCOMPARE(plan.links().size(),2);
        graph.links.append({10,20,70,50,"active"});
        graph.links.append({30,20,40,50,"active"});
        plan = graph.playbackPlan("alsa_input.analog-stereo","",30,true);
        QVERIFY(plan.captureReady); QVERIFY(plan.monitorReady);
        graph.nodes[10].muted = true;
        plan = graph.playbackPlan("alsa_input.analog-stereo","",30,true);
        QVERIFY(!plan.captureReady); QVERIFY(plan.monitorReady); QVERIFY(plan.canAdvance());
        graph.nodes.remove(10);
        plan = graph.playbackPlan("alsa_input.analog-stereo","",30,true);
        QVERIFY(plan.capture.isEmpty()); QVERIFY(plan.monitorReady);
        graph.links.clear(); graph.links.append({30,13,41,80,"active"});
        plan = graph.playbackPlan("alsa_input.analog-stereo","",30,true);
        QVERIFY(!plan.captureReady); QVERIFY(plan.monitorReady);
        plan = graph.playbackPlan("alsa_input.analog-stereo","",30,false);
        QVERIFY(plan.monitor.isEmpty()); QVERIFY(!plan.canAdvance());
    }
    void outputMustBeActiveBeforeStandalonePlaybackAdvances() {
        auto graph = monitorFixture(); graph.links.clear();
        auto plan = graph.playbackPlan("alsa_input.analog-stereo","",30,true);
        QVERIFY(!plan.canAdvance()); QVERIFY(!plan.monitor.isEmpty());
        graph.links.append({30,13,41,80,"paused"});
        QVERIFY(!graph.playbackPlan("alsa_input.analog-stereo","",30,true).canAdvance());
        graph.links.last().state = "active";
        QVERIFY(graph.playbackPlan("alsa_input.analog-stereo","",30,true).canAdvance());
        graph.defaultSink.clear();
        QVERIFY(!graph.playbackPlan("alsa_input.analog-stereo","",30,true).canAdvance());
    }
    void surroundOutputUsesFrontPairAndRejectsUnsupportedPorts() {
        auto g = monitorFixture();
        g.ports.insert(83, {83,13,"803","in","32 bit float mono audio","LFE"});
        g.ports.insert(84, {84,13,"804","in","32 bit float mono audio","RL"});
        QCOMPARE(g.monitorRoutes(30).size(), 2);
        for (const auto &r : g.monitorRoutes(30)) QVERIFY(r.inputPort == 80 || r.inputPort == 81);
        g.ports[80].dsp = "16 bit interleaved audio";
        const auto remaining = g.monitorRoutes(30);
        QCOMPARE(remaining.size(), 1); QCOMPARE(remaining.first().inputPort, 81);
        g.ports.remove(41); QVERIFY(g.monitorRoutes(30).isEmpty());
    }
    void pcmDoesNotAdvanceWhenMutedAndStopsCleanly() {
        float source[]{.5f, -.5f, .25f}; float out[10]{}; std::size_t cursor = 0;
        QVERIFY(!Pcm::render(source, 3, cursor, out, 5, .5f, 1.f, false,false,false));
        QCOMPARE(cursor, 0u); for (float v : out) QCOMPARE(v, 0.f);
        QVERIFY(Pcm::render(source, 3, cursor, out, 5, .5f, 1.f, false,true,true));
        QCOMPARE(cursor, 3u);
        QCOMPARE(out[0], .25f); QCOMPARE(out[1], .5f);
        QCOMPARE(out[2], -.25f); QCOMPARE(out[3], -.5f);
        QCOMPARE(out[4], .125f); QCOMPARE(out[5], .25f);
        for (int i=6; i<10; ++i) QCOMPARE(out[i], 0.f);
    }
    void pcmLoopsAndSanitizesBadSamples() {
        float source[]{2.f, -2.f, std::numeric_limits<float>::quiet_NaN()};
        float out[10]{}; std::size_t cursor = 0;
        QVERIFY(!Pcm::render(source, 3, cursor, out, 5, .25f, 1.f, true,true,true));
        QCOMPARE(out[0], .5f); QCOMPARE(out[1], 1.f);
        QCOMPARE(out[2], -.5f); QCOMPARE(out[3], -1.f);
        QCOMPARE(out[4], 0.f); QCOMPARE(out[5], 0.f);
        QCOMPARE(out[6], .5f); QCOMPARE(out[7], 1.f);
        QCOMPARE(out[8], -.5f); QCOMPARE(out[9], -1.f); QCOMPARE(cursor, 2u);
    }
    void independentGainsShareCursorAndCanMuteEitherDestination() {
        float source[]{.5f,.5f,.5f}; float out[2]{}; std::size_t cursor = 0;
        QVERIFY(!Pcm::render(source,3,cursor,out,1,.2f,.8f,false,true,true));
        QCOMPARE(out[0],.1f); QCOMPARE(out[1],.4f); QCOMPARE(cursor,1u);
        QVERIFY(!Pcm::render(source,3,cursor,out,1,0.f,.8f,false,true,true));
        QCOMPARE(out[0],0.f); QCOMPARE(out[1],.4f); QCOMPARE(cursor,2u);
        QVERIFY(Pcm::render(source,3,cursor,out,1,.2f,0.f,false,true,true));
        QCOMPARE(out[0],.1f); QCOMPARE(out[1],0.f); QCOMPARE(cursor,3u);
    }
    void pcmKeepsLocalMusicMovingWhenCaptureStartsStopsOrMutes() {
        float source[]{.1f,.2f,.3f,.4f}; float out[2]{}; std::size_t cursor = 0;
        QVERIFY(!Pcm::render(source,4,cursor,out,1,.5f,1.f,false,false,true));
        QCOMPARE(out[0],0.f); QCOMPARE(out[1],.1f); QCOMPARE(cursor,1u);
        QVERIFY(!Pcm::render(source,4,cursor,out,1,.5f,1.f,false,true,true));
        QCOMPARE(out[0],.1f); QCOMPARE(out[1],.2f); QCOMPARE(cursor,2u);
        QVERIFY(!Pcm::render(source,4,cursor,out,1,.5f,1.f,false,false,true));
        QCOMPARE(out[0],0.f); QCOMPARE(out[1],.3f); QCOMPARE(cursor,3u);
        QVERIFY(!Pcm::render(source,4,cursor,out,1,.5f,1.f,false,false,false));
        QCOMPARE(out[0],0.f); QCOMPARE(out[1],0.f); QCOMPARE(cursor,3u);
        QVERIFY(Pcm::render(source,4,cursor,out,1,.5f,1.f,false,true,false));
        QCOMPARE(out[0],.2f); QCOMPARE(out[1],0.f); QCOMPARE(cursor,4u);
    }
    void legacyVolumeMigrationPreservesBothLevelsWithoutAttenuation() {
        const auto migrated = AudioLevels::fromJson({{"volume",.27}});
        QCOMPARE(migrated.microphone,.27);
        QCOMPARE(migrated.headphones,.27);
        const auto restored = AudioLevels::fromJson(migrated.toJson());
        QCOMPARE(restored.microphone,migrated.microphone);
        QCOMPARE(restored.headphones,migrated.headphones);
        const auto initial = AudioLevels::fromJson({});
        QCOMPARE(initial.microphone,AudioLevels::DefaultHeadphones);
        QCOMPARE(initial.headphones,AudioLevels::DefaultHeadphones);
        const auto explicitLevels = AudioLevels::fromJson({{"volume",.9},{"microphoneVolume",.03},{"headphoneVolume",.6}});
        QCOMPARE(explicitLevels.microphone,.03); QCOMPARE(explicitLevels.headphones,.6);
    }
    void routesNeverCrossMicrophoneAndHeadphoneFeeds() {
        auto graph = monitorFixture();
        for (const auto &r : graph.routes("alsa_input.analog-stereo","",30)) QCOMPARE(r.outputPort,40);
        for (const auto &r : graph.monitorRoutes(30)) QCOMPARE(r.outputPort,41);
        graph.ports[40].channel.clear();
        QVERIFY(graph.routes("alsa_input.analog-stereo","",30).isEmpty());
        QCOMPARE(graph.monitorRoutes(30).size(),2);
    }
    void muteParsedFromNodeProps() {
        auto objects = fixture(); auto o = objects[0].toObject(); auto info = o["info"].toObject();
        info["params"] = QJsonObject{{"Props", QJsonArray{QJsonObject{{"mute", true}}}}};
        o["info"] = info; objects[0] = o;
        auto g = Graph::parse(objects); QVERIFY(g.nodes[10].muted);
        QVERIFY(g.routes("alsa_input.analog-stereo", "", 30).isEmpty());
    }
    void libraryUndoPreservesOriginalFiles() {
        QTemporaryDir dir;
        const QString path = dir.path() + "/a sound.wav";
        QFile original(path); QVERIFY(original.open(QIODevice::WriteOnly));
        const QByteArray bytes("unchanged file contents");
        original.write(bytes); original.close();
        Backend backend(true);
        const int initial = backend.clips().size();
        backend.addUrls({QUrl::fromLocalFile(path).toString()});
        QCOMPARE(backend.clips().size(), initial + 1);
        backend.addUrls({path}); QCOMPARE(backend.clips().size(), initial + 1);
        backend.rename(initial, "New name");
        QCOMPARE(backend.clips().last().toMap().value("name").toString(), "New name");
        backend.undo(); QCOMPARE(backend.clips().last().toMap().value("name").toString(), "a sound");
        backend.redo(); QCOMPARE(backend.clips().last().toMap().value("name").toString(), "New name");
        backend.remove(initial); QCOMPARE(backend.clips().size(), initial);
        backend.undo(); QCOMPARE(backend.clips().size(), initial + 1);
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
    }
    void realQmlLoadsAndShortcutsAreUsable() {
        QTemporaryDir dir;
        Backend backend(true); Theme theme(dir.path()); Translator translator(dir.path() + "/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.rootContext()->setContextProperty("theme", &theme);
        engine.rootContext()->setContextProperty("translator", &translator);
        int warnings = 0;
        connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &e : errors) qWarning() << e;
        });
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        auto typeText = [window](const char *text) { while (*text) QTest::keyClick(window, *text++); };
        QFile clip(dir.path() + "/test.wav"); QVERIFY(clip.open(QIODevice::WriteOnly)); clip.write("test"); clip.close();
        backend.addUrls({clip.fileName()});
        QTest::qWait(100);
        window->requestActivate(); QTest::qWait(30);
        QTest::keyClick(window, Qt::Key_1);
        QVERIFY(!backend.error().isEmpty()); QCOMPARE(window->property("selected").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(backend.playing().isEmpty());
        auto microphoneVolume = window->findChild<QQuickItem *>("microphoneVolumeSlider"); QVERIFY(microphoneVolume);
        auto headphoneVolume = window->findChild<QQuickItem *>("headphoneVolumeSlider"); QVERIFY(headphoneVolume);
        const double previousHeadphones = backend.headphoneVolume();
        const double previousMicrophone = backend.microphoneVolume();
        QTest::mouseClick(window, Qt::LeftButton, {}, microphoneVolume->mapToScene(QPointF(microphoneVolume->width()*.8,microphoneVolume->height()/2)).toPoint());
        QVERIFY(qAbs(backend.microphoneVolume()-previousMicrophone) > .01);
        QCOMPARE(backend.headphoneVolume(),previousHeadphones);
        const double adjustedMicrophone = backend.microphoneVolume();
        QTest::mouseClick(window, Qt::LeftButton, {}, headphoneVolume->mapToScene(QPointF(headphoneVolume->width()*.6,headphoneVolume->height()/2)).toPoint());
        QVERIFY(qAbs(backend.headphoneVolume()-previousHeadphones) > .01);
        QCOMPARE(backend.microphoneVolume(),adjustedMicrophone);

        QFile another(dir.path() + "/another.wav"); QVERIFY(another.open(QIODevice::WriteOnly)); another.write("test"); another.close();
        backend.addUrls({another.fileName()});
        backend.rename(0, "Zulu"); backend.rename(1, "Alpha");
        auto search = window->findChild<QQuickItem *>("searchField"); QVERIFY(search);
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(search->hasActiveFocus());
        typeText("Alpha");
        QCOMPARE(window->property("visibleCount").toInt(), 1);
        QCOMPARE(window->property("selected").toInt(), 1);
        backend.clearError();
        QTest::keyClick(window, Qt::Key_1);
        QVERIFY(backend.error().isEmpty());
        QCOMPARE(search->property("text").toString(), "Alpha1");
        QCOMPARE(window->property("visibleCount").toInt(), 0);
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        typeText("Alpha");
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(!backend.error().isEmpty());
        QCOMPARE(window->property("selected").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("visibleCount").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(window, "changeSort", Q_ARG(QVariant, QVariant("name"))));
        window->setProperty("selected", 1);
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(window->property("selected").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(window->property("selected").toInt(), 1);
        QTest::keyClick(window, Qt::Key_F2);
        auto rename = window->findChild<QQuickItem *>("renameField"); QVERIFY(rename);
        QTRY_VERIFY(rename->hasActiveFocus());
        typeText("Queen 2");
        QCOMPARE(rename->property("text").toString(), "Queen 2");
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(backend.clips()[1].toMap().value("name").toString(), "Alpha");
        QTRY_VERIFY(!window->property("dialogOpen").toBool());
        window->setWidth(qMax(640,window->minimumWidth()));
        window->setHeight(qMax(360,window->minimumHeight()));
        QTest::qWait(60);
        QVERIFY(microphoneVolume->width() > 50);
        QVERIFY(headphoneVolume->width() > 50);
        auto preferences = window->findChild<QQuickItem *>("preferencesButton"); QVERIFY(preferences);
        QTest::mouseClick(window, Qt::LeftButton, {}, preferences->mapToScene(preferences->boundingRect().center()).toPoint());
        auto dialog = window->findChild<QObject *>("preferencesDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->property("height").toDouble() <= window->height()-24);
        auto monitor = window->findChild<QQuickItem *>("monitorCheckBox"); QVERIFY(monitor);
        if (const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR"); !capture.isEmpty()) {
            QDir().mkpath(capture);
            window->grabWindow().save(capture + "/preferences.png");
            qInfo() << "Monitor bounds" << monitor->mapToScene(monitor->boundingRect().center()) << "dialog height" << dialog->property("height");
        }
        QVERIFY(backend.monitorEnabled());
        QTest::mouseClick(window, Qt::LeftButton, {}, monitor->mapToScene(monitor->boundingRect().center()).toPoint());
        QVERIFY(!backend.monitorEnabled());
        QTest::mouseClick(window, Qt::LeftButton, {}, monitor->mapToScene(monitor->boundingRect().center()).toPoint());
        QVERIFY(backend.monitorEnabled());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QCOMPARE(warnings, 0);
    }
    void transportKeepsTheGeometryAssignedByTheCompositor() {
        QTemporaryDir dir;
        PlaybackUiBackend backend; Theme theme(dir.path()); Translator translator(dir.path()+"/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QFile clip(dir.path()+"/A sound.wav"); QVERIFY(clip.open(QIODevice::WriteOnly)); clip.write("test"); clip.close();
        backend.addUrls({clip.fileName()});
        QTest::qWait(60);
        const QSize assigned(1260,1390);
        window->resize(assigned);
        QTest::qWait(60);
        QCOMPARE(window->size(),assigned);
        QSignalSpy heightChanges(window,&QWindow::heightChanged);
        QSignalSpy widthChanges(window,&QWindow::widthChanged);
        QSignalSpy minimumChanges(window,&QWindow::minimumHeightChanged);
        for (int i=0; i<3; ++i) {
            backend.showTransport(QString::fromUtf8("云原神"),true);
            QTest::qWait(30);
            QCOMPARE(window->size(),assigned);
            backend.showTransport(QString::fromUtf8("云原神"),false);
            QTest::qWait(30);
            QCOMPARE(window->size(),assigned);
            backend.showTransport({},false);
            backend.stop();
            QTest::qWait(30);
            QCOMPARE(window->size(),assigned);
            backend.play(0);
            QTest::qWait(30);
            QCOMPARE(window->size(),assigned);
            QVERIFY(!backend.error().isEmpty());
            backend.clearError();
            backend.stop();
            QTest::qWait(30);
            QCOMPARE(window->size(),assigned);
        }
        QCOMPARE(heightChanges.count(),0);
        QCOMPARE(widthChanges.count(),0);
        QCOMPARE(minimumChanges.count(),0);
        auto table = window->findChild<QQuickItem *>("soundTable"); QVERIFY(table);
        QVERIFY(table->height() > 1000);
    }
    void interfaceTracksSystemFontAndFitsLargerText() {
        const auto original = QGuiApplication::font();
        const auto restore = qScopeGuard([original] { QGuiApplication::setFont(original); });
        QFont normal = original; normal.setPointSizeF(11);
        QGuiApplication::setFont(normal);
        QTemporaryDir dir;
        Backend backend(true); Theme theme(dir.path()); Translator translator(dir.path()+"/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        int warnings = 0;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &error : errors) qWarning() << error;
        });
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QFile clip(dir.path()+"/Example sound.wav"); QVERIFY(clip.open(QIODevice::WriteOnly)); clip.write("test"); clip.close();
        backend.addUrls({clip.fileName()});
        QTest::qWait(70);
        auto grid = window->findChild<QObject *>("volumeGrid"); QVERIFY(grid);
        QCOMPARE(grid->property("columns").toInt(),2);
        QVERIFY(window->width() >= 1040); QVERIFY(window->height() >= 700);
        const auto initialRowHeight = window->property("rowHeight").toInt();
        const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR");
        if (!capture.isEmpty()) QDir().mkpath(capture);
        if (!capture.isEmpty()) window->grabWindow().save(capture+"/normal-font.png");
        QSignalSpy fontChange(&theme,&Theme::fontChanged);
        QFont larger = normal; larger.setPointSizeF(18);
        QGuiApplication::setFont(larger);
        QTRY_VERIFY(fontChange.count() > 0);
        QTRY_COMPARE(window->property("font").value<QFont>().pointSizeF(),18.);
        QTRY_VERIFY(window->property("rowHeight").toInt() > initialRowHeight);
        QTest::qWait(70);
        for (const QString &name : {QString("microphoneVolumeLabel"),QString("headphoneVolumeLabel")}) {
            auto label = window->findChild<QQuickItem *>(name); QVERIFY(label);
            const QFontMetricsF metrics(label->property("font").value<QFont>());
            QVERIFY(metrics.horizontalAdvance(label->property("text").toString()) <= label->width()+1);
        }
        window->setWidth(window->minimumWidth());
        window->setHeight(window->minimumHeight());
        QTest::qWait(70);
        QCOMPARE(grid->property("columns").toInt(),1);
        QVERIFY(!window->property("fileColumnVisible").toBool());
        auto microphone = window->findChild<QQuickItem *>("microphoneVolumeSlider"); QVERIFY(microphone);
        auto headphones = window->findChild<QQuickItem *>("headphoneVolumeSlider"); QVERIFY(headphones);
        QVERIFY(microphone->width() > 90); QVERIFY(headphones->width() > 90);
        QVERIFY(microphone->mapToScene(QPointF()).y()+microphone->height() <= headphones->mapToScene(QPointF()).y());
        auto table = window->findChild<QQuickItem *>("soundTable"); QVERIFY(table);
        QVERIFY(table->height() >= window->property("rowHeight").toInt()*3-1);
        if (!capture.isEmpty()) window->grabWindow().save(capture+"/large-font.png");
        QFont pixels = normal; pixels.setPixelSize(22);
        QGuiApplication::setFont(pixels);
        QTRY_COMPARE(window->property("font").value<QFont>().pixelSize(),22);
        QCOMPARE(warnings,0);
    }
};
QTEST_MAIN(MicpadTests)
#include "micpad_tests.moc"
