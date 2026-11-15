#include "graph.h"
#include <QJsonDocument>

static QString string(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toInteger()) : QString();
}

static int ownOutputPort(const Graph &graph, int node, const QString &channel) {
    for (const auto &port : graph.ports)
        if (port.node == node && port.direction == "out" && port.channel == channel &&
            port.dsp == "32 bit float mono audio") return port.id;
    return -1;
}

Graph Graph::parse(const QJsonArray &objects) {
    Graph g;
    for (const auto &v : objects) {
        const auto o = v.toObject();
        const auto info = o.value("info").toObject();
        const auto p = info.value("props").toObject();
        const int id = o.value("id").toInt(-1);
        const QString type = o.value("type").toString();
        if (type.endsWith(":Node")) {
            GraphNode n;
            n.id = id; n.serial = string(p, "object.serial");
            n.name = string(p, "node.name"); n.mediaClass = string(p, "media.class");
            n.label = string(p, "node.description");
            if (n.label.isEmpty()) n.label = string(p, "application.name");
            if (n.label.isEmpty()) n.label = string(p, "node.nick");
            if (n.label.isEmpty()) n.label = n.name;
            n.physical = n.mediaClass == "Audio/Source" &&
                (string(p, "device.api") == "alsa" || n.name.startsWith("alsa_input.")) &&
                !n.name.contains(".monitor");
            for (const auto &props : info.value("params").toObject().value("Props").toArray()) {
                const auto params = props.toObject();
                n.muted |= params.value("mute").toBool() || params.value("softMute").toBool();
            }
            g.nodes.insert(id, n);
        } else if (type.endsWith(":Port")) {
            g.ports.insert(id, {id, string(p, "node.id").toInt(), string(p, "object.serial"),
                                string(p, "port.direction"), string(p, "format.dsp"), string(p, "audio.channel")});
        } else if (type.endsWith(":Link")) {
            g.links.append({info.value("output-node-id").toInt(-1), info.value("input-node-id").toInt(-1),
                            info.value("output-port-id").toInt(-1), info.value("input-port-id").toInt(-1),
                            info.value("state").toString()});
        } else if (type.endsWith(":Metadata")) {
            for (const auto &entry : o.value("metadata").toArray()) {
                const auto m = entry.toObject();
                const auto key = m.value("key").toString();
                if (key == "default.audio.source" || key == "default.audio.sink") {
                    auto value = m.value("value");
                    if (value.isString()) value = QJsonDocument::fromJson(value.toString().toUtf8()).object();
                    const auto name = value.toObject().value("name").toString();
                    if (key == "default.audio.source") g.defaultSource = name;
                    else g.defaultSink = name;
                }
            }
        }
    }
    return g;
}

QVariantList Graph::microphones() const {
    QVariantList list;
    for (const auto &n : nodes)
        if (n.physical) list.append(QVariantMap{{"name", n.name}, {"label", n.label}, {"muted", n.muted}});
    return list;
}

QVariantList Graph::applications(const QString &source) const {
    QSet<int> ids;
    for (const auto &l : links)
        if (nodes.value(l.outputNode).physical && nodes.value(l.outputNode).name == source &&
            nodes.value(l.inputNode).mediaClass == "Stream/Input/Audio" && l.state != "error")
            ids.insert(l.inputNode);
    QVariantList result;
    for (int id : ids) {
        const auto n = nodes.value(id);
        result.append(QVariantMap{{"serial", n.serial}, {"label", n.label}});
    }
    return result;
}

QList<Route> Graph::routes(const QString &source, const QString &applicationSerial, int ownNode) const {
    QList<Route> result;
    if (ownNode < 0) return result;
    const int outputPort = ownOutputPort(*this, ownNode, "FL");
    if (outputPort < 0) return result;
    QSet<int> seen;
    for (const auto &l : links) {
        const auto mic = nodes.value(l.outputNode);
        const auto app = nodes.value(l.inputNode);
        const auto port = ports.value(l.inputPort);
        if (!mic.physical || mic.muted || mic.name != source || app.mediaClass != "Stream/Input/Audio" ||
            app.serial.isEmpty() || (!applicationSerial.isEmpty() && app.serial != applicationSerial) ||
            port.id < 0 || port.node != app.id || port.direction != "in" || port.serial.isEmpty() ||
            port.dsp != "32 bit float mono audio" || l.state == "error" || seen.contains(port.id)) continue;
        seen.insert(port.id);
        result.append({ownNode, outputPort, app.id, port.id,
                       QString::number(outputPort) + ":" + port.serial});
    }
    return result;
}

QList<Route> Graph::monitorRoutes(int ownNode) const {
    QList<Route> result;
    if (ownNode < 0 || defaultSink.isEmpty()) return result;
    const int outputPort = ownOutputPort(*this, ownNode, "FR");
    if (outputPort < 0) return result;
    QList<GraphPort> playbackPorts;
    for (const auto &port : ports) {
        const auto sink = nodes.value(port.node);
        if (sink.id == ownNode || sink.name != defaultSink || sink.mediaClass != "Audio/Sink" ||
            port.direction != "in" || port.serial.isEmpty() || port.dsp != "32 bit float mono audio") continue;
        playbackPorts.append(port);
    }
    for (const auto &port : playbackPorts) {
        // On surround outputs, use the front pair instead of sending a full
        // range mono effect into every channel, including the subwoofer.
        if (playbackPorts.size() > 2 && port.channel != "FL" && port.channel != "FR" && port.channel != "MONO") continue;
        result.append({ownNode, outputPort, port.node, port.id,
                       QString::number(outputPort) + ":" + port.serial});
    }
    return result;
}

PlaybackPlan Graph::playbackPlan(const QString &source, const QString &applicationSerial,
                                 int ownNode, bool monitorEnabled) const {
    PlaybackPlan plan;
    plan.capture = routes(source, applicationSerial, ownNode);
    if (monitorEnabled) plan.monitor = monitorRoutes(ownNode);
    plan.captureReady = hasActiveRoute(ownNode, plan.capture);
    plan.monitorReady = hasActiveRoute(ownNode, plan.monitor);
    return plan;
}

bool Graph::hasActiveRoute(int ownNode, const QList<Route> &desired) const {
    for (const auto &link : links)
        if (link.outputNode == ownNode && link.state == "active")
            for (const auto &r : desired)
                if (link.outputPort == r.outputPort && link.inputPort == r.inputPort) return true;
    return false;
}
