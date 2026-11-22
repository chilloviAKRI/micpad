#pragma once
#include <QList>
#include <QString>
#include <QUrl>

struct SpotifyTrack {
    QString title;
    QString artist;
    double duration;
};

QUrl youtubeImportUrl(const QString &link);
QUrl spotifyImportUrl(const QString &link);
QList<SpotifyTrack> spotifyTracks(const QByteArray &page);
QString matchingYoutubeId(const SpotifyTrack &track, const QByteArray &searchOutput);
