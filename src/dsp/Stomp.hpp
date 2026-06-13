#pragma once
#include <cmath>
#include <algorithm>

struct Stomp {
    float processSend(float stompIn, float returnIn, float feedbackGain) {
        // feedbackGain is in range -1.2 to 1.2
        float feedbackSignal = returnIn * feedbackGain;

        // Apply a soft limiter (tanh) to emulate the feedbackLimiter compressor
        // and prevent runaway oscillation from blowing up.
        feedbackSignal = std::tanh(feedbackSignal / 5.0f) * 5.0f;

        return stompIn + feedbackSignal;
    }

    float processOut(float stompIn, float returnIn, float blend) {
        // blend is 0.0 (dry) to 1.0 (wet)
        // Scale by 0.5f to match the dry/wet summing mixer attenuation observed in hardware
        return (stompIn * (1.0f - blend) + returnIn * blend) * 0.5f;
    }
};
