// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QEvent>
#include <QGuiApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

namespace {
double luminance(const QColor &color) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

QString contrastingText(const QColor &background, const QColor &preferred) {
    const double a = luminance(background), b = luminance(preferred);
    if ((qMax(a, b) + .05) / (qMin(a, b) + .05) >= 4.5) return preferred.name();
    return a > .179 ? "#000000" : "#ffffff";
}

QString mix(const QColor &a, const QColor &b, double amount) {
    return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                            a.greenF() * (1 - amount) + b.greenF() * amount,
                            a.blueF() * (1 - amount) + b.blueF() * amount).name();
}
}

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    qGuiApp->installEventFilter(this);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

QFont Theme::uiFont() const {
    QFont font = QGuiApplication::font();
    // Keep the desktop's font family and scaling. A readable minimum also
    // covers platform themes that return Qt's tiny generic fallback font.
    font.setFamilies(font.families());
    if (font.pointSizeF() > 0) font.setPointSizeF(qMax(11., font.pointSizeF()));
    else font.setPixelSize(qMax(15, font.pixelSize()));
    return font;
}

bool Theme::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ApplicationFontChange) emit fontChanged();
    return QObject::eventFilter(watched, event);
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    return readColors(path).value("accent").toString();
}

QVariantMap Theme::readColors(const QString &path) {
    // Omarchy's palette is a flat TOML table. Only accept quoted RGB colors;
    // a malformed field must never propagate an invalid color into QML.
    QVariantMap values;
    QString mode;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QString content = QString::fromUtf8(file.readAll()).remove('\r');
        const QRegularExpression expression(R"re(^[ \t]*([a-z_]+)[ \t]*=[ \t]*(["'])(#[0-9a-fA-F]{6})\2[ \t]*(?:#[^\n]*)?$)re",
                                            QRegularExpression::MultilineOption);
        auto matches = expression.globalMatch(content);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (match.captured(1) != "mode") values.insert(match.captured(1), QColor(match.captured(3)).name());
        }
        const QRegularExpression modeExpression(R"re(^[ \t]*mode[ \t]*=[ \t]*(["'])(dark|light)\1[ \t]*(?:#[^\n]*)?$)re",
                                                QRegularExpression::MultilineOption);
        mode = modeExpression.match(content).captured(2);
    }
    const bool dark = mode.isEmpty() ? luminance(QColor(values.value("background", "#232528").toString())) < .5
                                    : mode == "dark";
    const auto background = values.value("background", dark ? "#232528" : "#f0f0f0").toString();
    const auto foreground = values.value("foreground", dark ? "#e2e4e6" : "#202020").toString();
    const auto accent = values.value("accent", "#FFD60A").toString();
    QVariantMap colors{
        {"mode", dark ? "dark" : "light"}, {"background", background}, {"foreground", foreground}, {"accent", accent},
        {"dark_background", mix(background, Qt::black, .22)},
        {"darker_background", mix(background, Qt::black, .4)},
        {"lighter_background", mix(background, foreground, .08)},
        {"muted", mix(background, foreground, .3)},
        {"dark_foreground", mix(foreground, background, .5)},
        {"light_foreground", mix(foreground, background, .22)}, {"bright_foreground", foreground},
        {"selection", mix(background, accent, .22)},
        {"red", dark ? "#e49191" : "#bc4040"}, {"green", dark ? "#91be8a" : "#358338"},
        {"yellow", dark ? "#d4ba73" : "#967021"}, {"blue", dark ? "#87b9e4" : "#2974ac"}
    };
    for (auto it = values.cbegin(); it != values.cend(); ++it) colors.insert(it.key(), it.value());
    if (!colors.contains("selection_foreground"))
        colors.insert("selection_foreground", contrastingText(QColor(colors.value("selection").toString()), foreground));
    return colors;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    return contrastingText(QColor(accent()), Qt::black);
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const auto colors = readColors(m_directory + "/theme/colors.toml");
    if (colors != m_colors) {
        m_colors = colors;
        emit changed();
    }
}
