#pragma once
#include <QJsonObject>
#include <algorithm>
#include <cmath>

struct AudioLevels {
    static constexpr int Steps = 20;
    static constexpr double DefaultHeadphones = .35;
    static constexpr double DefaultMicrophone = DefaultHeadphones;
    double microphone = DefaultMicrophone;
    double headphones = DefaultHeadphones;

    static double bounded(double value, double fallback) {
        return std::isfinite(value) ? std::clamp(value, 0., 1.) : fallback;
    }
    static double stepped(double value, double fallback) {
        return std::round(bounded(value, fallback) * Steps) / Steps;
    }
    static AudioLevels fromJson(const QJsonObject &settings) {
        const double legacy = stepped(settings.value("volume").toDouble(DefaultHeadphones), DefaultHeadphones);
        // Both feeds use the same five-percent grid as the segmented sliders.
        // Loading older settings rounds each independent level only once.
        return {stepped(settings.value("microphoneVolume").toDouble(legacy), legacy),
                stepped(settings.value("headphoneVolume").toDouble(legacy), legacy)};
    }
    QJsonObject toJson() const {
        return {{"microphoneVolume", microphone}, {"headphoneVolume", headphones}};
    }
};
