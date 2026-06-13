#pragma once
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct VCO {
    float phase = 0.0f;
    float sampleRate = 48000.0f;
    float lastSine = 0.0f;

    void setSampleRate(float sr) {
        sampleRate = sr;
    }

    float poly_blep(float t, float dt) {
        if (t < dt) {
            float x = t / dt;
            return 2.0f * x - x * x - 1.0f;
        } else if (t > 1.0f - dt) {
            float x = (t - 1.0f) / dt;
            return x * x + 2.0f * x + 1.0f;
        }
        return 0.0f;
    }

    struct Output {
        float sine;
        float square;
    };

    Output process(float baseFreq, float detuneCents, float fmInput, float fmAmt, float feedbackAmt) {
        float sumSine = 0.0f;
        float sumSqr = 0.0f;
        int oversampleFactor = 4;

        for (int sub = 0; sub < oversampleFactor; ++sub) {
            float totalDetune = detuneCents;

            // Apply FM modulation from the input (exponential FM, maps to detune cents)
            totalDetune += fmInput * fmAmt;

            // Apply feedback (VCO's own sine modulated back into detune)
            if (feedbackAmt > 0.01f) {
                totalDetune += (lastSine * fmAmt * 0.3f * feedbackAmt);
            }

            // Calculate frequency
            float freq = baseFreq * std::pow(2.0f, totalDetune / 1200.0f);

            // Stability clamp at oversampled Nyquist
            float nyquist = (sampleRate * oversampleFactor) / 2.0f;
            if (freq > nyquist) freq = nyquist;
            if (freq < 0.0f) freq = 0.0f;

            float dt = freq / (sampleRate * oversampleFactor);
            phase += dt;
            if (phase >= 1.0f) phase -= 1.0f;

            float theta = phase * 2.0f * M_PI;
            // Calibrated with phase-aligned harmonics from hardware recording (10.5V P2P when scaled by 5.25V)
            float rawSine = std::sin(theta)
                            - 0.000279f * std::sin(2.0f * theta) - 0.020761f * std::cos(2.0f * theta)
                            - 0.026747f * std::sin(3.0f * theta) + 0.005528f * std::cos(3.0f * theta)
                            + 0.001756f * std::sin(4.0f * theta) + 0.005727f * std::cos(4.0f * theta)
                            + 0.004720f * std::sin(5.0f * theta) - 0.000879f * std::cos(5.0f * theta)
                            - 0.000405f * std::sin(6.0f * theta) - 0.000762f * std::cos(6.0f * theta)
                            - 0.001979f * std::sin(7.0f * theta) + 0.001379f * std::cos(7.0f * theta)
                            + 0.000088f * std::sin(8.0f * theta) + 0.000775f * std::cos(8.0f * theta);
            float sineSamp = rawSine * 0.972f;
            
            // Square with 52% duty cycle (analog asymmetry) and PolyBLEP
            float D = 0.52f;
            float sqrSamp = (phase < D) ? 1.0f : -1.0f;
            sqrSamp += poly_blep(phase, dt);
            float phaseShifted = phase - D;
            if (phaseShifted < 0.0f) phaseShifted += 1.0f;
            sqrSamp -= poly_blep(phaseShifted, dt);

            lastSine = sineSamp;

            sumSine += sineSamp;
            sumSqr += sqrSamp;
        }

        return { sumSine / oversampleFactor, sumSqr / oversampleFactor };
    }
};
