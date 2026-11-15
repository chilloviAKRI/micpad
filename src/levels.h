#pragma once
#include <QJsonObject>
#include <algorithm>
#include <cmath>

struct AudioLevels {
    static constexpr double DefaultHeadphones = .35;
    static constexpr double DefaultMicrophone = DefaultHeadphones;
    double microphone = DefaultMicrophone;
    double headphones = DefaultHeadphones;

    static double bounded(double value, double fallback) {
        return std::isfinite(value) ? std::clamp(value, 0., 1.) : fallback;
    }
    static AudioLevels fromJson(const QJsonObject &settings) {
        const double legacy = bounded(settings.value("volume").toDouble(DefaultHeadphones), DefaultHeadphones);
        // Preserve the old shared level for both feeds unless the user has
        // already saved separate microphone and headphone volumes.
        return {bounded(settings.value("microphoneVolume").toDouble(legacy), legacy),
                bounded(settings.value("headphoneVolume").toDouble(legacy), legacy)};
    }
    QJsonObject toJson() const {
        return {{"microphoneVolume", microphone}, {"headphoneVolume", headphones}};
    }
};
