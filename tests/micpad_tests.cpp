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
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString outputMode READ outputMode NOTIFY changed)
    Q_PROPERTY(bool sending READ sending NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
public:
    PlaybackUiBackend() : Backend(true) {}
    QString displayedClip() const { return m_clip; }
    bool loading() const { return m_loading; }
    bool paused() const { return m_paused; }
    double duration() const { return m_duration; }
    double position() const { return m_position; }
    double progress() const { return m_duration > 0 ? m_position/m_duration : 0; }
    QString outputMode() const { return m_mode; }
    bool sending() const { return m_send && !m_paused; }
    bool listening() const { return m_listen && !m_paused; }
    void setPaused(bool value) { m_paused = value; emit changed(); }
    void showPlayable(const QString &name, double seconds, double position) {
        m_clip = name; m_loading = false; m_duration = seconds; m_position = position;
        m_paused = false; m_send = true; m_listen = true; m_mode = "normal"; emit changed();
    }
    Q_INVOKABLE bool seek(double progress) {
        if (m_duration <= 0 || !std::isfinite(progress)) return false;
        m_position = std::clamp(progress,0.,1.)*m_duration; emit changed(); return true;
    }
    Q_INVOKABLE void seekBy(double seconds) { if (m_duration > 0) seek((m_position+seconds)/m_duration); }
    Q_INVOKABLE void playMicrophoneOnly(int index) {
        showPlayable(clips().value(index).toMap().value("name").toString(),120,0);
        m_mode = "microphone"; m_listen = false; emit changed();
    }
    Q_INVOKABLE void preview(int index) {
        showPlayable(clips().value(index).toMap().value("name").toString(),120,0);
        m_mode = "headphones"; m_send = false; emit changed();
    }
    void showTransport(const QString &clip, bool loading) {
        m_clip = clip; m_loading = loading;
        if (clip.isEmpty()) { m_paused = false; m_position = 0; m_duration = 0; }
        emit changed();
    }
private:
    QString m_clip;
    bool m_loading = false;
    bool m_paused = false, m_send = false, m_listen = false;
    double m_duration = 0, m_position = 0;
    QString m_mode = "normal";
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
    void pausedPcmStaysSilentAndSeekingResumesFromTheChosenSample() {
        const float source[]{.1f,.2f,.3f,.4f}; float output[12]; std::size_t cursor = 2;
        std::fill(std::begin(output),std::end(output),1.f);
        QVERIFY(!Pcm::render(source,4,cursor,output,6,.5f,.75f,false,true,true,true));
        QCOMPARE(cursor,2u);
        for (const auto sample : output) QCOMPARE(sample,0.f);
        cursor = Pcm::frameAt(.75,4);
        QVERIFY(!Pcm::render(source,4,cursor,output,1,.5f,.75f,false,true,true,true));
        QCOMPARE(cursor,3u);
        QVERIFY(Pcm::render(source,4,cursor,output,1,.5f,.75f,false,true,true,false));
        QCOMPARE(cursor,4u); QCOMPARE(output[0],.2f); QCOMPARE(output[1],.3f);
        // Pausing or seeking at the end must not finish until resumed.
        QVERIFY(!Pcm::render(source,4,cursor,output,1,1,1,false,true,true,true));
        QCOMPARE(cursor,4u);
        QVERIFY(!Pcm::render(source,4,cursor,output,1,1,1,true,true,false,false));
        QCOMPARE(cursor,1u); QCOMPARE(output[0],.1f); QCOMPARE(output[1],0.f);
        QCOMPARE(Pcm::frameAt(-.5,4),0u); QCOMPARE(Pcm::frameAt(1.5,4),4u);
        QCOMPARE(Pcm::frameAt(std::numeric_limits<double>::quiet_NaN(),4),0u);
        const std::size_t longRecording = std::size_t(48000)*60*60*20;
        QCOMPARE(Pcm::frameAt(.5,longRecording),longRecording/2);
    }
    void exclusivePlaybackModesNeverCreateLinksToTheOtherDestination() {
        auto graph = monitorFixture();
        graph.links.append({30,20,40,50,"active"});
        graph.links.append({30,13,41,80,"active"});
        const auto normal = graph.playbackPlan("alsa_input.analog-stereo","",30,true,true);
        QVERIFY(normal.captureReady); QVERIFY(normal.monitorReady);
        QVERIFY(!normal.capture.isEmpty()); QVERIFY(!normal.monitor.isEmpty());
        const auto microphone = graph.playbackPlan("alsa_input.analog-stereo","",30,false,true);
        QVERIFY(microphone.captureReady); QVERIFY(!microphone.monitorReady);
        QVERIFY(!microphone.capture.isEmpty()); QVERIFY(microphone.monitor.isEmpty());
        for (const auto &route : microphone.links()) QCOMPARE(route.outputPort,40);
        const auto headphones = graph.playbackPlan("alsa_input.analog-stereo","",30,true,false);
        QVERIFY(!headphones.captureReady); QVERIFY(headphones.monitorReady);
        QVERIFY(headphones.capture.isEmpty()); QVERIFY(!headphones.monitor.isEmpty());
        for (const auto &route : headphones.links()) QCOMPARE(route.outputPort,41);
        graph.nodes[10].muted = true;
        QVERIFY(!graph.playbackPlan("alsa_input.analog-stereo","",30,false,true).canAdvance());
        QVERIFY(graph.playbackPlan("alsa_input.analog-stereo","",30,true,false).monitorReady);
        QCOMPARE(graph.links.size(),5);
    }
    void realTransportControlsPauseSeekAndSelectAnExclusiveFeed() {
        QTemporaryDir dir;
        PlaybackUiBackend backend; Theme theme(dir.path()); Translator translator(dir.path()+"/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        int warnings = 0;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &error : errors) qWarning() << error;
        });
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QVERIFY(saveFile(dir.path()+"/Example sound.wav","demo"));
        backend.addUrls({dir.path()+"/Example sound.wav"});
        backend.showPlayable("Example sound",120,30);
        QTest::qWait(80); window->requestActivate();
        auto button = window->findChild<QQuickItem *>("playButton"); QVERIFY(button);
        auto timeline = window->findChild<QQuickItem *>("playbackProgress"); QVERIFY(timeline);
        auto diagram = window->findChild<QQuickItem *>("routingDiagram"); QVERIFY(diagram);
        QVERIFY(!window->findChild<QObject *>("localListeningToggle"));
        QVERIFY(!window->findChild<QObject *>("monitorCheckBox"));
        auto feed = diagram->findChild<QQuickItem *>("routingFeed"); QVERIFY(feed);
        auto micStem = diagram->findChild<QQuickItem *>("microphoneStem"); QVERIFY(micStem);
        auto headStem = diagram->findChild<QQuickItem *>("headphoneStem"); QVERIFY(headStem);
        auto micBranch = diagram->findChild<QQuickItem *>("microphoneBranch"); QVERIFY(micBranch);
        auto headBranch = diagram->findChild<QQuickItem *>("headphoneBranch"); QVERIFY(headBranch);
        const auto accent = window->property("accent").value<QColor>();
        const auto red = window->property("red").value<QColor>();
        const auto blue = window->property("blue").value<QColor>();
        const auto inactive = window->property("rule").value<QColor>();
        auto click = [window](QQuickItem *item) { QTest::mouseClick(window,Qt::LeftButton,{},item->mapToScene(item->boundingRect().center()).toPoint()); };
        const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR");
        auto snapshot = [&](const QString &name) {
            if (!capture.isEmpty()) { QDir().mkpath(capture); window->grabWindow().save(capture+"/"+name+".png"); }
        };
        const auto size = window->size(); QSignalSpy resized(window,&QWindow::heightChanged);
        QCOMPARE(button->property("iconSymbol").toString(),"pause");
        QVERIFY(diagram->property("microphoneActive").toBool()); QVERIFY(diagram->property("headphoneActive").toBool());
        QCOMPARE(feed->property("color").value<QColor>(),accent);
        QCOMPARE(micStem->property("color").value<QColor>(),red);
        QCOMPARE(micBranch->property("color").value<QColor>(),red);
        QCOMPARE(headStem->property("color").value<QColor>(),blue);
        QCOMPARE(headBranch->property("color").value<QColor>(),blue);
        snapshot("transport-active");
        click(button); QVERIFY(backend.paused()); QCOMPARE(backend.position(),30.);
        QCOMPARE(button->property("iconSymbol").toString(),"play");
        QVERIFY(!diagram->property("microphoneActive").toBool()); QVERIFY(!diagram->property("headphoneActive").toBool());
        for (auto path : {feed,micStem,headStem,micBranch,headBranch})
            QCOMPARE(path->property("color").value<QColor>(),inactive);
        snapshot("transport-paused");
        QTest::keyClick(window,Qt::Key_Space); QVERIFY(!backend.paused());
        click(button); QVERIFY(backend.paused());
        const double padding = timeline->property("leftPadding").toDouble();
        auto handle = timeline->property("handle").value<QQuickItem *>(); QVERIFY(handle);
        const double targetX = padding+handle->width()/2+.75*(timeline->width()-2*padding-handle->width());
        QTest::mouseClick(window,Qt::LeftButton,{},timeline->mapToScene(QPointF(targetX,timeline->height()/2)).toPoint());
        QVERIFY(qAbs(backend.position()-90.) < 1.); QVERIFY(backend.paused());
        QTest::keyClick(window,Qt::Key_Space); QVERIFY(!backend.paused());
        QTest::keyClick(window,Qt::Key_Space); QVERIFY(backend.paused());
        auto back = window->findChild<QQuickItem *>("seekBackButton"); QVERIFY(back);
        const auto previous = backend.position(); click(back); QVERIFY(qAbs(backend.position()-(previous-5)) < 1e-8);
        auto microphone = window->findChild<QQuickItem *>("microphoneOnlyButton"); QVERIFY(microphone);
        auto headphones = window->findChild<QQuickItem *>("headphonesOnlyButton"); QVERIFY(headphones);
        click(microphone); QCOMPARE(backend.outputMode(),"microphone");
        QVERIFY(diagram->property("microphoneActive").toBool()); QVERIFY(!diagram->property("headphoneActive").toBool());
        for (auto path : {feed,micStem,micBranch}) QCOMPARE(path->property("color").value<QColor>(),red);
        for (auto path : {headStem,headBranch}) QCOMPARE(path->property("color").value<QColor>(),inactive);
        snapshot("transport-microphone");
        click(headphones); QCOMPARE(backend.outputMode(),"headphones");
        QVERIFY(!diagram->property("microphoneActive").toBool()); QVERIFY(diagram->property("headphoneActive").toBool());
        for (auto path : {feed,headStem,headBranch}) QCOMPARE(path->property("color").value<QColor>(),blue);
        for (auto path : {micStem,micBranch}) QCOMPARE(path->property("color").value<QColor>(),inactive);
        snapshot("transport-headphones");
        auto forward = window->findChild<QQuickItem *>("seekForwardButton"); QVERIFY(forward);
        click(forward); QCOMPARE(backend.position(),5.);
        auto time = window->findChild<QQuickItem *>("playbackTimeLabel"); QVERIFY(time);
        QCOMPARE(time->property("text").toString(),"0:05 / 2:00");
        QCOMPARE(window->size(),size); QCOMPARE(resized.count(),0);
        QCOMPARE(warnings,0);
    }
    void languageDefaultsToEnglishAndPersistsAnIndependentChoice() {
        const bool hadOverride = qEnvironmentVariableIsSet("MICPAD_LANGUAGE");
        const auto previousOverride = qgetenv("MICPAD_LANGUAGE");
        const QLocale previousLocale;
        const auto previousDirection = QGuiApplication::layoutDirection();
        const auto restore = qScopeGuard([&] {
            if (hadOverride) qputenv("MICPAD_LANGUAGE",previousOverride); else qunsetenv("MICPAD_LANGUAGE");
            QLocale::setDefault(previousLocale); QGuiApplication::setLayoutDirection(previousDirection);
        });
        qunsetenv("MICPAD_LANGUAGE");
        QLocale::setDefault(QLocale("ru_RU"));
        QTemporaryDir dir;
        const auto preference = dir.path()+"/preferences/language";
        Translator translator(preference);
        QCOMPARE(translator.language(), "en"); QVERIFY(!translator.isEmpty());
        QCOMPARE(translator.translate("Main", "Microphone"), "Microphone");
        QSignalSpy changed(&translator,&Translator::changed);
        QVERIFY(translator.selectLanguage("ru"));
        QCOMPARE(translator.language(), "ru"); QVERIFY(changed.count() > 0);
        QCOMPARE(Translator::readLanguage(preference), "ru");
        QCOMPARE(translator.translate("Main", "Microphone"), QString::fromUtf8("Микрофон"));
        Translator restored(preference); QCOMPARE(restored.language(), "ru");
        QVERIFY(saveFile(preference, "en\n"));
        QTRY_COMPARE(translator.language(), "en"); QTRY_COMPARE(restored.language(), "en");
        QVERIFY(saveFile(dir.path()+"/invalid", "not-a-language"));
        QCOMPARE(Translator::readLanguage(dir.path()+"/invalid"), "en");
        QVERIFY(saveFile(dir.path()+"/blocked", "file"));
        Translator blocked(dir.path()+"/blocked/language");
        QVERIFY(!blocked.selectLanguage("ru")); QCOMPARE(blocked.language(), "en");
        QVERIFY(!blocked.error().isEmpty()); blocked.clearError(); QVERIFY(blocked.error().isEmpty());
        qputenv("MICPAD_LANGUAGE", "ru");
        Translator launchOverride(preference); QCOMPARE(launchOverride.language(), "ru");
        QVERIFY(launchOverride.selectLanguage("en"));
        QTest::qWait(120); QCOMPARE(launchOverride.language(), "en");
    }
    void languagePreferencesRetranslatesRealControlsAndKeepsPlaybackAndGeometry() {
        const bool hadOverride = qEnvironmentVariableIsSet("MICPAD_LANGUAGE");
        const auto previousOverride = qgetenv("MICPAD_LANGUAGE");
        const QLocale previousLocale;
        const auto previousDirection = QGuiApplication::layoutDirection();
        const auto restore = qScopeGuard([&] {
            if (hadOverride) qputenv("MICPAD_LANGUAGE",previousOverride); else qunsetenv("MICPAD_LANGUAGE");
            QLocale::setDefault(previousLocale); QGuiApplication::setLayoutDirection(previousDirection);
        });
        qunsetenv("MICPAD_LANGUAGE");
        QTemporaryDir dir;
        Translator translator(dir.path()+"/language");
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
        PlaybackUiBackend backend; Theme theme(dir.path());
        QQmlApplicationEngine engine;
        connect(&translator,&Translator::changed,&engine,&QQmlEngine::retranslate);
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        int warnings = 0;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &error : errors) qWarning() << error;
        });
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(60); window->requestActivate();
        auto add = window->findChild<QQuickItem *>("addSoundButton"); QVERIFY(add);
        auto channel = window->findChild<QQuickItem *>("microphoneChannelLabel"); QVERIFY(channel);
        auto language = window->findChild<QQuickItem *>("languageComboBox"); QVERIFY(language);
        auto fileNames = window->findChild<QQuickItem *>("fileNamesCheckBox"); QVERIFY(fileNames);
        auto preferencesButton = window->findChild<QQuickItem *>("preferencesButton"); QVERIFY(preferencesButton);
        auto preferences = window->findChild<QObject *>("preferencesDialog"); QVERIFY(preferences);
        QCOMPARE(add->property("text").toString(), "Add sound");
        QCOMPARE(channel->property("text").toString(), "Microphone");
        QCOMPARE(language->property("currentValue").toString(),"en");
        backend.showTransport("Example sound",false);
        const auto size = window->size();
        QSignalSpy resized(window,&QWindow::heightChanged);
        auto click = [window](QQuickItem *item) {
            QTest::mouseClick(window,Qt::LeftButton,{},item->mapToScene(item->boundingRect().center()).toPoint());
        };
        click(preferencesButton); QTRY_VERIFY(preferences->property("opened").toBool());
        QVERIFY(window->property("fileColumnVisible").toBool());
        click(fileNames); QVERIFY(!window->property("fileColumnVisible").toBool());
        click(language); QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return);
        QTRY_COMPARE(translator.language(), "ru");
        auto languagePopup = language->property("popup").value<QObject *>(); QVERIFY(languagePopup);
        QTRY_VERIFY(!languagePopup->property("visible").toBool());
        QCOMPARE(add->property("text").toString(),QString::fromUtf8("Добавить звук"));
        QCOMPARE(channel->property("text").toString(),QString::fromUtf8("Микрофон"));
        QCOMPARE(language->property("currentValue").toString(),"ru");
        QVERIFY(!fileNames->property("checked").toBool());
        QCOMPARE(fileNames->property("text").toString(),QString::fromUtf8("Имена файлов"));
        fileNames->forceActiveFocus(); QTest::keyClick(window,Qt::Key_Space);
        QVERIFY(window->property("fileColumnVisible").toBool());
        QCOMPARE(preferences->property("title").toString(),QString::fromUtf8("Настройки"));
        auto close = preferences->findChild<QQuickItem *>("dialogHeaderCloseButton"); QVERIFY(close);
        QCOMPARE(close->property("text").toString(),QString::fromUtf8("Закрыть"));
        click(language); QTest::keyClick(window,Qt::Key_Home); QTest::keyClick(window,Qt::Key_Return);
        QTRY_COMPARE(translator.language(),"en");
        QCOMPARE(add->property("text").toString(), "Add sound");
        QCOMPARE(channel->property("text").toString(), "Microphone");
        QCOMPARE(preferences->property("title").toString(), "Preferences");
        QCOMPARE(close->property("text").toString(), "Close");
        QCOMPARE(window->size(),size); QCOMPARE(resized.count(),0);
        QCOMPARE(backend.displayedClip(), "Example sound");
        Translator nextLaunch(dir.path()+"/language"); QCOMPARE(nextLaunch.language(), "en");
        QCOMPARE(warnings,0);
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
    void builtInThemesWorkWithoutOmarchyAndRememberIndependentChoices() {
        QTemporaryDir dir;
        const auto current = dir.path()+"/current";
        Theme theme(current);
        QVERIFY(!theme.omarchyAvailable()); QVERIFY(!theme.syncOmarchy());
        QCOMPARE(theme.preset(),"micpad-dark");
        QVERIFY(theme.presets().size() >= 6);
        QSet<QString> ids;
        for (const auto &item : theme.presets()) {
            const auto id = item.toMap().value("id").toString();
            QVERIFY(!ids.contains(id)); ids.insert(id);
            QVERIFY(theme.selectPreset(id));
            const auto colors = theme.colors();
            for (auto it = colors.cbegin(); it != colors.cend(); ++it) {
                if (it.key() != "mode") QVERIFY2(QColor(it.value().toString()).isValid(),qPrintable(id+":"+it.key()));
            }
        }
        QVERIFY(theme.selectPreset("paper")); QVERIFY(!theme.dark());
        const auto paper = theme.colors();
        Theme restored(current);
        QVERIFY(!restored.syncOmarchy()); QCOMPARE(restored.preset(),"paper"); QCOMPARE(restored.colors(),paper);
        QVERIFY(!theme.selectPreset("unknown-palette")); QCOMPARE(theme.colors(),paper);
        QVERIFY(!theme.error().isEmpty()); theme.clearError(); QVERIFY(theme.error().isEmpty());

        // Installing Omarchy must not replace an explicitly selected palette.
        const auto palette = current+"/theme/colors.toml";
        QVERIFY(saveFile(palette,themePalette(false)));
        QTRY_VERIFY(theme.omarchyAvailable()); QCOMPARE(theme.colors(),paper);
        theme.setSyncOmarchy(true); QVERIFY(theme.syncOmarchy()); QVERIFY(theme.dark());
        QCOMPARE(theme.colors().value("background").toString(),"#282828");
        QCOMPARE(theme.preset(),"paper");
        Theme synced(current); QVERIFY(synced.syncOmarchy()); QCOMPARE(synced.preset(),"paper");
        // Missing Omarchy files use the remembered preset and recover live.
        QVERIFY(QFile::remove(palette)); QTRY_VERIFY(!theme.omarchyAvailable());
        QVERIFY(theme.syncOmarchy()); QCOMPARE(theme.colors(),paper);
        QVERIFY(saveFile(palette,themePalette(false))); QTRY_VERIFY(theme.dark());
        theme.setSyncOmarchy(false); QCOMPARE(theme.colors(),paper);
        QFile unchanged(palette); QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(),themePalette(false));

        QVERIFY(saveFile(dir.path()+"/blocked","file"));
        Theme blocked(current,nullptr,{},{},{},dir.path()+"/blocked/appearance.json");
        QVERIFY(blocked.syncOmarchy()); const auto previous = blocked.colors();
        blocked.setSyncOmarchy(false); QVERIFY(blocked.syncOmarchy());
        QVERIFY(!blocked.error().isEmpty()); QCOMPARE(blocked.colors(),previous);
        QVERIFY(!blocked.selectPreset("nord")); QCOMPARE(blocked.preset(),"micpad-dark");
        const auto invalid = dir.path()+"/invalid.json";
        QVERIFY(saveFile(invalid,R"({"syncOmarchy":"false","preset":"missing"})"));
        Theme invalidSettings(current,nullptr,{},{},{},invalid);
        QVERIFY(invalidSettings.syncOmarchy()); QCOMPARE(invalidSettings.preset(),"micpad-dark");
    }
    void appearanceControlsSwitchLiveWithoutChangingPlaybackOrWindowGeometry() {
        QTemporaryDir dir;
        const auto palette = dir.path()+"/theme/colors.toml";
        QVERIFY(saveFile(palette,themePalette(false)));
        PlaybackUiBackend backend; Theme theme(dir.path()); Translator translator(dir.path()+"/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        int warnings = 0;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &error : errors) qWarning() << error;
        });
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(1260,900); backend.showPlayable("Example sound",120,45);
        QTest::qWait(60); window->requestActivate();
        const auto size = window->size(); QSignalSpy resized(window,&QWindow::heightChanged);
        auto dialog = window->findChild<QObject *>("preferencesDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog,"open")); QTRY_VERIFY(dialog->property("opened").toBool());
        auto sync = window->findChild<QQuickItem *>("omarchySyncCheckBox"); QVERIFY(sync);
        auto selector = window->findChild<QQuickItem *>("themeComboBox"); QVERIFY(selector);
        QVERIFY(sync->property("checked").toBool()); QVERIFY(!selector->isEnabled());
        auto click = [window](QQuickItem *item) {
            QTest::mouseClick(window,Qt::LeftButton,{},item->mapToScene(item->boundingRect().center()).toPoint());
        };
        auto snapshot = [&](const QString &name) {
            const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR");
            if (!capture.isEmpty()) { QDir().mkpath(capture); window->grabWindow().save(capture+"/"+name+".png"); }
        };
        click(sync); QVERIFY(!theme.syncOmarchy()); QVERIFY(selector->isEnabled());
        click(selector); QTest::keyClick(window,Qt::Key_End); QTest::keyClick(window,Qt::Key_Return);
        QTRY_COMPARE(theme.preset(),"paper"); QVERIFY(!theme.dark());
        QCOMPARE(window->color(),QColor("#f5f1e8")); snapshot("appearance-paper");
        QVERIFY(saveFile(palette,themePalette(true))); QTest::qWait(180);
        QCOMPARE(window->color(),QColor("#f5f1e8"));
        click(sync); QVERIFY(theme.syncOmarchy()); QVERIFY(!selector->isEnabled());
        QCOMPARE(window->color(),QColor("#eff1f5"));
        QVERIFY(saveFile(palette,themePalette(false))); QTRY_COMPARE(window->color(),QColor("#282828"));
        click(sync); QVERIFY(!theme.syncOmarchy()); QCOMPARE(theme.preset(),"paper");
        QCOMPARE(window->color(),QColor("#f5f1e8"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"close")); QTRY_VERIFY(!dialog->property("visible").toBool());
        for (const auto &entry : theme.presets()) {
            const auto id = entry.toMap().value("id").toString();
            QVERIFY(theme.selectPreset(id)); QTest::qWait(30); snapshot("preset-"+id);
        }
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto removeTranslator = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
        QVERIFY(translator.selectLanguage("ru")); engine.retranslate();
        QCOMPARE(sync->property("text").toString(),QString::fromUtf8("Синхронизировать цвета с Omarchy"));
        QVERIFY(QMetaObject::invokeMethod(dialog,"open")); QTRY_VERIFY(dialog->property("opened").toBool());
        snapshot("appearance-russian");
        QCOMPARE(window->size(),size); QCOMPARE(resized.count(),0);
        QCOMPARE(backend.displayedClip(),"Example sound"); QCOMPARE(backend.position(),45.); QVERIFY(!backend.paused());
        QCOMPARE(warnings,0);
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
        auto microphone = window->findChild<QObject *>("microphoneComboBox"); QVERIFY(microphone);
        auto comboText = microphone->property("contentItem").value<QQuickItem *>(); QVERIFY(comboText);
        QCOMPARE(comboText->property("color").value<QColor>(), QColor("#4c4f69"));
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
        QCOMPARE(migrated.microphone,.25);
        QCOMPARE(migrated.headphones,.25);
        const auto restored = AudioLevels::fromJson(migrated.toJson());
        QCOMPARE(restored.microphone,migrated.microphone);
        QCOMPARE(restored.headphones,migrated.headphones);
        const auto initial = AudioLevels::fromJson({});
        QCOMPARE(initial.microphone,AudioLevels::DefaultHeadphones);
        QCOMPARE(initial.headphones,AudioLevels::DefaultHeadphones);
        const auto explicitLevels = AudioLevels::fromJson({{"volume",.9},{"microphoneVolume",.03},{"headphoneVolume",.6}});
        QCOMPARE(explicitLevels.microphone,.05); QCOMPARE(explicitLevels.headphones,.6);
        const auto previous = AudioLevels::fromJson({{"microphoneVolume",.472},{"headphoneVolume",.101}});
        QCOMPARE(previous.microphone,.45); QCOMPARE(previous.headphones,.1);
        const auto maximum = AudioLevels::fromJson({{"microphoneVolume",1.},{"headphoneVolume",0.}});
        QCOMPARE(maximum.microphone,1.); QCOMPARE(maximum.headphones,0.);
        for (int step=0; step<=AudioLevels::Steps; ++step) {
            const double level = double(step)/AudioLevels::Steps;
            QCOMPARE(AudioLevels::stepped(level,0),level);
        }
        QCOMPARE(AudioLevels::stepped(-1,.35),0.);
        QCOMPARE(AudioLevels::stepped(2,.35),1.);
        QCOMPARE(AudioLevels::stepped(std::numeric_limits<double>::quiet_NaN(),.35),.35);
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
        backend.clearError();
        const int beforeSlider = window->property("selected").toInt();
        const double beforeSliderLevel = backend.microphoneVolume();
        microphoneVolume->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(window->property("selected").toInt(),beforeSlider);
        QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(qAbs(backend.microphoneVolume()-(beforeSliderLevel-1./backend.volumeSteps())) < 1e-9);
        // Mouse and keyboard changes both land on the displayed segment grid.
        QCOMPARE(backend.volumeSteps(),20);
        const double keepHeadphones = backend.headphoneVolume();
        auto percentage = window->findChild<QQuickItem *>("microphoneVolumeLabel"); QVERIFY(percentage);
        for (int step=0; step<=backend.volumeSteps(); ++step) {
            const double value = double(step)/backend.volumeSteps();
            const double padding = microphoneVolume->property("leftPadding").toDouble();
            auto handle = microphoneVolume->property("handle").value<QQuickItem *>(); QVERIFY(handle);
            const double travel = microphoneVolume->width()-2*padding-handle->width();
            QTest::mouseClick(window, Qt::LeftButton, {}, microphoneVolume->mapToScene(QPointF(padding+handle->width()/2+value*travel,microphoneVolume->height()/2)).toPoint());
            QVERIFY(qAbs(backend.microphoneVolume()-value) < 1e-9);
            QCOMPARE(percentage->property("text").toString(),QString::number(step*5)+"%");
            QCOMPARE(backend.headphoneVolume(),keepHeadphones);
        }
        auto menuButton = window->findChild<QQuickItem *>("appMenuButton"); QVERIFY(menuButton);
        QTest::mouseClick(window, Qt::LeftButton, {}, menuButton->mapToScene(menuButton->boundingRect().center()).toPoint());
        auto appMenu = window->findChild<QObject *>("appMenu"); QVERIFY(appMenu);
        QTRY_VERIFY(appMenu->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!appMenu->property("visible").toBool());

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
        QVERIFY(window->findChild<QQuickItem *>("microphoneComboBox"));
        QVERIFY(!dialog->property("footer").value<QQuickItem *>());
        auto close = dialog->findChild<QQuickItem *>("dialogHeaderCloseButton"); QVERIFY(close);
        QVERIFY(close->isVisible()); QCOMPARE(close->property("keyHint").toString(),"Esc");
        auto header = dialog->property("header").value<QQuickItem *>(); QVERIFY(header);
        QVERIFY(close->mapToItem(header,QPointF()).y() >= 0);
        QVERIFY(close->mapToItem(header,QPointF(0,close->height())).y() <= header->height());
        if (const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR"); !capture.isEmpty()) {
            QDir().mkpath(capture);
            window->grabWindow().save(capture + "/preferences.png");
        }
        QTest::mouseClick(window, Qt::LeftButton, {}, close->mapToScene(close->boundingRect().center()).toPoint());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTest::mouseClick(window, Qt::LeftButton, {}, preferences->mapToScene(preferences->boundingRect().center()).toPoint());
        QTRY_VERIFY(dialog->property("opened").toBool());
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
        // The redesigned mixer and transport occupy their own panels. The
        // library must still expand with the compositor's tall allocation.
        QVERIFY(table->height() > assigned.height()*.6);
        auto player = window->findChild<QQuickItem *>("playerPanel"); QVERIFY(player);
        QVERIFY(qAbs(player->mapToScene(QPointF(0,player->height())).y()-(window->height()-window->property("uiScale").toDouble()*12)) <= 2);
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
        auto header = window->findChild<QQuickItem *>("appHeader"); QVERIFY(header);
        auto menuButton = window->findChild<QQuickItem *>("appMenuButton"); QVERIFY(menuButton);
        QVERIFY(qAbs(menuButton->mapToScene(QPointF(menuButton->width(),0)).x()-(window->width()-window->property("uiScale").toDouble()*12)) <= 2);
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
        for (const QString &name : {QString("microphoneVolumeLabel"),QString("headphoneVolumeLabel"),QString("microphoneChannelLabel"),QString("headphoneChannelLabel")}) {
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
        for (auto glyph : window->findChildren<QQuickItem *>("buttonGlyph")) {
            if (!glyph->isVisible()) continue;
            auto button = glyph->parentItem();
            while (button && !button->property("iconSymbol").isValid()) button = button->parentItem();
            QVERIFY(button);
            const QRectF buttonBounds(button->mapToScene(QPointF()),button->size());
            const QRectF glyphBounds(glyph->mapToScene(QPointF()),glyph->size());
            QVERIFY2(buttonBounds.adjusted(-1,-1,1,1).contains(glyphBounds),qPrintable(button->objectName()));
            if (button->property("text").toString().isEmpty() && button->property("keyHint").toString().isEmpty()) {
                QVERIFY2(qAbs(buttonBounds.center().x()-glyphBounds.center().x()) <= 1,qPrintable(button->objectName()));
                QVERIFY2(qAbs(buttonBounds.center().y()-glyphBounds.center().y()) <= 1,qPrintable(button->objectName()));
            }
        }
        if (!capture.isEmpty()) window->grabWindow().save(capture+"/large-font.png");
        QFont pixels = normal; pixels.setPixelSize(22);
        QGuiApplication::setFont(pixels);
        QTRY_COMPARE(window->property("font").value<QFont>().pixelSize(),22);
        QCOMPARE(warnings,0);
    }
    void fontconfigAliasUpdatesBodyAndConsoleFontsWithoutRestarting() {
        const auto original = QGuiApplication::font();
        const auto restore = qScopeGuard([original] { QGuiApplication::setFont(original); });
        QFont desktop = original; desktop.setFamily("sans-serif"); desktop.setPointSizeF(11);
        QGuiApplication::setFont(desktop);
        QTemporaryDir dir;
        const auto fontDirectory = dir.path()+"/fontconfig";
        const auto familyFile = fontDirectory+"/family.conf";
        const auto matcher = dir.path()+"/match-font";
        QVERIFY(saveFile(matcher,"#!/bin/sh\ncat -- \"$(dirname -- \"$0\")/fontconfig/family.conf\"\n"));
        QVERIFY(QFile::setPermissions(matcher,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        Backend backend(true);
        Theme theme(dir.path()+"/current",nullptr,{},fontDirectory,matcher);
        Translator translator(dir.path()+"/language");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        int warnings = 0;
        connect(&engine,&QQmlEngine::warnings,&engine,[&](const QList<QQmlError> &errors) {
            warnings += errors.size(); for (const auto &error : errors) qWarning() << error;
        });
        engine.load(QUrl("qrc:/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
        QSignalSpy changes(&theme,&Theme::fontChanged);
        // A first fontconfig file can appear after the window has opened.
        QVERIFY(saveFile(familyFile,"monospace, Secondary Font Alias\n"));
        QTRY_COMPARE(theme.uiFont().family(),"monospace");
        QTRY_COMPARE(window->property("font").value<QFont>().family(),"monospace");
        QCOMPARE(theme.consoleFont().families(),theme.uiFont().families());
        QVERIFY(changes.count() > 0);
        for (const QString &name : {QString("addSoundButton"),QString("searchField"),QString("microphoneChannelLabel"),QString("microphoneVolumeLabel"),QString("currentSoundLabel")}) {
            auto item = window->findChild<QQuickItem *>(name); QVERIFY(item);
            QCOMPARE(item->property("font").value<QFont>().family(),"monospace");
        }
        QTest::qWait(70);
        if (const auto capture = qEnvironmentVariable("MICPAD_CAPTURE_DIR"); !capture.isEmpty()) {
            QDir().mkpath(capture); window->grabWindow().save(capture+"/system-font.png");
        }
        // Atomic replacement must re-arm the watcher and update both styles.
        QVERIFY(saveFile(familyFile,"sans-serif, Another Alias\n"));
        QTRY_COMPARE(window->property("font").value<QFont>().family(),"sans-serif");
        QCOMPARE(window->property("monoFont").value<QFont>().family(),"sans-serif");
        QVERIFY(saveFile(familyFile,"monospace\n"));
        QTRY_COMPARE(window->property("font").value<QFont>().family(),"monospace");
        QFont larger = desktop; larger.setPointSizeF(18);
        QGuiApplication::setFont(larger);
        QTRY_COMPARE(window->property("font").value<QFont>().pointSizeF(),18.);
        QCOMPARE(window->property("font").value<QFont>().family(),"monospace");
        QCOMPARE(theme.consoleFont().pointSizeF(),18.);
        QFont pixels = desktop; pixels.setPixelSize(22);
        QGuiApplication::setFont(pixels);
        QTRY_COMPARE(window->property("font").value<QFont>().pixelSize(),22);
        QCOMPARE(theme.consoleFont().pixelSize(),22);
        QCOMPARE(warnings,0);
    }
};
QTEST_MAIN(MicpadTests)
#include "micpad_tests.moc"
