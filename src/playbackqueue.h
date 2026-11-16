#pragma once
#include <QStringList>
#include <QVariantList>

// Paths identify tracks even when the user renames or sorts the library.
class PlaybackQueue {
public:
    void setOrder(const QStringList &paths) { m_order = paths; m_explicitOrder = true; }
    void started(const QString &path) { m_current = path; }
    void clear() { m_current.clear(); }
    int next(const QVariantList &clips) const {
        if (m_current.isEmpty()) return -1;
        QStringList paths = m_order;
        if (!m_explicitOrder)
            for (const auto &clip : clips) paths.append(clip.toMap().value("path").toString());
        const auto current = paths.indexOf(m_current);
        if (current < 0 || current+1 >= paths.size()) return -1;
        const auto path = paths.at(current+1);
        for (qsizetype i = 0; i < clips.size(); ++i)
            if (clips.at(i).toMap().value("path").toString() == path) return int(i);
        return -1;
    }
private:
    QStringList m_order;
    QString m_current;
    bool m_explicitOrder = false;
};
