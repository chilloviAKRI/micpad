#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Pcm {
inline std::size_t frameAt(double progress, std::size_t count) {
    if (!std::isfinite(progress) || progress <= 0) return 0;
    if (progress >= 1) return count;
    const long double frame = std::round(static_cast<long double>(progress)*count);
    return frame >= count ? count : static_cast<std::size_t>(frame);
}
// This runs on the audio thread and does no allocation, locking or I/O.
inline bool render(const float *source, std::size_t count, std::size_t &cursor,
                   float *destination, std::size_t frames, float microphoneGain,
                   float headphoneGain, bool repeat, bool microphoneAllowed, bool headphonesAllowed,
                   bool paused = false) {
    const bool allowed = !paused && (microphoneAllowed || headphonesAllowed);
    bool finished = false;
    for (std::size_t i = 0; i < frames; ++i) {
        float value = 0;
        if (allowed && count > 0) {
            if (cursor >= count && repeat) cursor = 0;
            if (cursor < count) value = source[cursor++];
            else finished = true;
        }
        const float microphone = microphoneAllowed ? value * microphoneGain : 0;
        const float headphones = headphonesAllowed ? value * headphoneGain : 0;
        destination[2*i] = std::isfinite(microphone) ? std::clamp(microphone, -1.0f, 1.0f) : 0;
        destination[2*i+1] = std::isfinite(headphones) ? std::clamp(headphones, -1.0f, 1.0f) : 0;
    }
    return finished || (allowed && !repeat && cursor >= count);
}
}
