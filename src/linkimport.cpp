#include "linkimport.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {
const QRegularExpression videoId(R"(^[A-Za-z0-9_-]{11}$)");

QUrl webUrl(QString link) {
    link = link.trimmed();
    if (link.startsWith("//")) link.prepend("https:");
    else if (!link.contains("://")) link.prepend("https://");
    QUrl url(link, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")
        || !url.userInfo().isEmpty() || url.port() != -1) return {};
    url.setScheme("https");
    url.setFragment({});
    return url;
}

QString normalizedWords(const QString &text) {
    QString result;
    const auto normalized = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    for (const auto c : normalized) {
        if (c.isLetterOrNumber()) result += c;
        else if (c.category() != QChar::Mark_NonSpacing && c.category() != QChar::Mark_SpacingCombining)
            result += ' ';
    }
    return result.simplified();
}

bool containsWords(const QString &text, const QString &words) {
    return !words.isEmpty() && (' ' + text + ' ').contains(' ' + words + ' ');
}
}

QUrl youtubeImportUrl(const QString &link) {
    const auto url = webUrl(link);
    const auto host = url.host();
    const auto parts = url.path().split('/', Qt::SkipEmptyParts);
    const QUrlQuery query(url);
    QString id;
    if (host == "youtu.be" && parts.size() == 1) id = parts.first();
    const bool youtube = host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com"
                         || host == "music.youtube.com";
    if (youtube && url.path() == "/watch") id = query.queryItemValue("v");
    if ((youtube || host == "www.youtube-nocookie.com" || host == "youtube-nocookie.com")
        && parts.size() == 2 && (parts.first() == "shorts" || parts.first() == "live" || parts.first() == "embed")) {
        id = parts.last();
    }
    if (videoId.match(id).hasMatch()) return QUrl("https://www.youtube.com/watch?v=" + id);
    if (youtube && url.path() == "/playlist") {
        static const QRegularExpression playlistId(R"(^[A-Za-z0-9_-]{2,100}$)");
        const auto list = query.queryItemValue("list");
        if (playlistId.match(list).hasMatch()) return QUrl("https://www.youtube.com/playlist?list=" + list);
    }
    return {};
}

QUrl spotifyImportUrl(const QString &link) {
    static const QRegularExpression resource(R"(^(track|album|playlist):([A-Za-z0-9]{22})$)");
    QString typeAndId;
    const auto trimmed = link.trimmed();
    if (trimmed.startsWith("spotify:")) {
        typeAndId = trimmed.mid(8);
    } else {
        const auto url = webUrl(trimmed);
        if (url.host() == "spotify.link" || url.host() == "spoti.fi")
            return url.path().size() > 1 ? url : QUrl{};
        if (url.host() != "open.spotify.com") return {};
        auto parts = url.path().split('/', Qt::SkipEmptyParts);
        if (!parts.isEmpty() && parts.first().startsWith("intl-")) parts.removeFirst();
        if (!parts.isEmpty() && parts.first() == "embed") parts.removeFirst();
        if (parts.size() != 2) return {};
        typeAndId = parts.join(':');
    }
    const auto match = resource.match(typeAndId);
    if (!match.hasMatch()) return {};
    return QUrl("https://open.spotify.com/embed/" + match.captured(1) + '/' + match.captured(2));
}

QList<SpotifyTrack> spotifyTracks(const QByteArray &page) {
    const auto html = QString::fromUtf8(page);
    static const QRegularExpression script(
        R"re(<script\b[^>]*\bid="__NEXT_DATA__"[^>]*>(.*?)</script>)re",
        QRegularExpression::DotMatchesEverythingOption);
    const auto match = script.match(html);
    const auto entity = QJsonDocument::fromJson(match.captured(1).toUtf8()).object()
        .value("props").toObject().value("pageProps").toObject().value("state").toObject()
        .value("data").toObject().value("entity").toObject();
    QList<SpotifyTrack> tracks;
    const auto entries = entity.value("type") == "track" ? QJsonArray{entity} : entity.value("trackList").toArray();
    for (const auto &entry : entries) {
        const auto item = entry.toObject();
        if (!item.value("uri").toString().startsWith("spotify:track:")) continue;
        const auto title = item.value("title").toString().simplified();
        const auto artists = item.value("artists").toArray();
        auto artist = artists.isEmpty() ? item.value("subtitle").toString().section(',',0,0)
                                        : artists.first().toObject().value("name").toString();
        artist = artist.simplified();
        const auto duration = item.value("duration").toDouble()/1000.;
        if (!title.isEmpty() && !artist.isEmpty() && duration > 0)
            tracks.append({title,artist,duration});
    }
    return tracks;
}

QString matchingYoutubeId(const SpotifyTrack &track, const QByteArray &searchOutput) {
    const auto result = QJsonDocument::fromJson(searchOutput).object();
    const auto title = normalizedWords(track.title);
    const auto artist = normalizedWords(track.artist);
    const auto requested = title + ' ' + artist;
    const QStringList variants{"cover", "karaoke", "live", "nightcore", "sped up", "slowed", "remix", "instrumental", "reaction"};
    QString best;
    double score = -1e9;
    const auto entries = result.value("entries").toArray();
    for (const auto &entry : entries) {
        const auto video = entry.toObject();
        const auto id = video.value("id").toString();
        const auto name = normalizedWords(video.value("title").toString());
        const auto channelName = video.value("channel").toString(video.value("uploader").toString());
        const auto channel = normalizedWords(channelName);
        const auto duration = video.value("duration").toDouble();
        const auto difference = std::abs(track.duration-duration);
        if (!videoId.match(id).hasMatch() || duration <= 0 || difference > std::max(12.,track.duration*.12)
            || !containsWords(name,title) || !(containsWords(name,artist) || containsWords(channel,artist))) continue;
        if (std::any_of(variants.cbegin(),variants.cend(),[&](const QString &word) {
            return containsWords(name,word) && !containsWords(requested,word);
        })) continue;
        const double candidate = (channelName.endsWith(" - Topic",Qt::CaseInsensitive) ? 30. : 0.)
                               + (containsWords(name,"official audio") ? 15. : 0.)
                               + (containsWords(channel,artist) ? 8. : 0.) - difference;
        if (candidate > score) { best = id; score = candidate; }
    }
    return best;
}
