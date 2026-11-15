#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QVariantList>

struct GraphNode {
    int id = -1;
    QString serial, name, label, mediaClass;
    bool physical = false, muted = false;
};
struct GraphPort {
    int id = -1, node = -1;
    QString serial, direction, dsp, channel;
};
struct GraphLink {
    int outputNode = -1, inputNode = -1, outputPort = -1, inputPort = -1;
    QString state;
};
struct Route {
    int outputNode, outputPort, inputNode, inputPort;
    QString key;
};

struct PlaybackPlan {
    QList<Route> capture, monitor;
    bool captureReady = false, monitorReady = false;
    bool canAdvance() const { return captureReady || monitorReady; }
    QList<Route> links() const { return capture + monitor; }
};

// Capture routes only target apps already using the selected microphone.
// Local listening targets the existing default output, never a new device.
class Graph {
public:
    static Graph parse(const QJsonArray &objects);
    QMap<int, GraphNode> nodes;
    QMap<int, GraphPort> ports;
    QList<GraphLink> links;
    QString defaultSource;
    QString defaultSink;
    QVariantList microphones() const;
    QVariantList applications(const QString &source) const;
    QList<Route> routes(const QString &source, const QString &applicationSerial, int ownNode) const;
    QList<Route> monitorRoutes(int ownNode) const;
    PlaybackPlan playbackPlan(const QString &source, const QString &applicationSerial,
                              int ownNode, bool monitorEnabled, bool captureEnabled = true) const;
    bool hasActiveRoute(int ownNode, const QList<Route> &desired) const;
};
