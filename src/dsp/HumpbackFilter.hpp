#pragma once
#include <cmath>
#include <algorithm>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct HumpbackFilter {
    float ic1eq = 0.0f; // Internal State 1 (Bandpass)
    float ic2eq = 0.0f; // Internal State 2 (Lowpass)
    float resHpState = 0.0f; // AC-coupling state for C3
    float sampleRate = 48000.0f;
    bool jumperClosed = false;

    void setSampleRate(float sr) {
        sampleRate = sr;
    }

    struct Output {
        float lp;
        float switched; // HP, BP, or Notch based on mode
    };

    Output process(float inputSample, float cutoffHz, float resonanceVal, float fmInput, float fmDepth, float mode) {
        float oversampleRate = sampleRate * 4.0f;
        float cutoff = cutoffHz * std::exp2f(fmInput * fmDepth);
        cutoff = std::max(1.0f, std::min(22000.0f, cutoff));

        // 1. Pre-calculate coefficient (f) with 1.60f compensation factor
        float compensatedCutoff = cutoff * 1.60f;
        float f = 2.0f * std::sin((float)M_PI * (compensatedCutoff / oversampleRate));
        if (f > 0.9f) f = 0.9f;

        // 2. Potentiometer mapping w_raw -> w_eff
        float w = std::max(0.0f, std::min(1.0f, resonanceVal));
        float w_eff = 0.0f;
        if (w <= 0.83f) {
            // Map [0.0, 0.83] to [-0.55, 0.83] to start from a clean, non-resonant Q ~ 0.707 at CCW
            w_eff = -0.55f + w * (1.38f / 0.83f);
        } else {
            w_eff = w;
        }

        float R_pot = 100000.0f;  // 100K pot (B100K)
        float R_fb = 47000.0f;   // 47K feedback resistor (calibrated for zero-crossing at 0.83)
        float R_in = 22000.0f;   // 22K exact schematic value (resonance jumper ignored)

        float alpha = 101000.0f / (101000.0f + w_eff * R_pot);
        float gainRes = alpha - R_fb / (R_in + (1.0f - w_eff) * R_pot) * (1.0f - alpha);

        // AC-coupling highpass coefficients for C3 (0.33uF) and R16 (33K)
        float dt = 1.0f / oversampleRate;
        float tau = 33000.0f * 0.33e-6f;
        float alphaHp = tau / (tau + dt);

        // Input with tiny noise floor to allow self-oscillation start
        float noise = (((float)std::rand() / RAND_MAX) - 0.5f) * 0.002f;
        float inSample = inputSample + noise;

        // 3. OVERSAMPLING LOOP (Run 4x)
        const float V_rail = 2.1f;       // +/-10.5V physical op-amp rail
        const float V_jfet_clamp = 2.52f; // +/-12.6V physical JFET gate clamp
        const float G_ota = 2.0f;        // OTA saturation threshold
        const float V_thresh = 2.0f;     // Soft-knee threshold
        const float S_res = 0.12f;       // tuned resonance feedback scaling

        // Helper lambda for soft-knee state clipping
        auto softKneeClip = [](float x, float vt, float limit) {
            float abs_x = std::abs(x);
            if (abs_x <= vt) {
                return x;
            } else {
                float diff = limit - vt;
                float val = vt + diff * std::tanh((abs_x - vt) / diff);
                return (x > 0.0f ? 1.0f : -1.0f) * val;
            }
        };

        float sumLow = 0.0f;
        float sumVal = 0.0f;

        for (int sub = 0; sub < 4; ++sub) {
            float low = ic2eq;
            float band = ic1eq;

            // JFET source follower buffering (clipped at JFET gate limits)
            float lowBuffered = std::max(-V_jfet_clamp, std::min(V_jfet_clamp, low));
            float bandBuffered = std::max(-V_jfet_clamp, std::min(V_jfet_clamp, band));

            // Resonance feedback path (IC4B differential feedback op-amp output)
            float vOutRes = gainRes * bandBuffered;
            float resFeedback = std::max(-V_rail, std::min(V_rail, vOutRes));

            // AC-coupling highpass filter C3
            float hpOut = resFeedback - resHpState;
            resHpState = resHpState * alphaHp + resFeedback * (1.0f - alphaHp);

            // Input summer (IC3A inverting summer output, clipped at rails limit)
            float high = -0.4545f * (inSample + lowBuffered) - 3.03f * S_res * hpOut;
            float highClipped = std::max(-V_rail, std::min(V_rail, high));

            // OTA updates (integrators with asymmetrical input saturation)
            float bandNew = band + f * G_ota * (std::tanh(highClipped / G_ota + 0.28f) - 0.27290f);
            float lowNew = low + f * G_ota * (std::tanh(bandNew / G_ota + 0.28f) - 0.27290f);

            // Clamp state variables at JFET gate limits using soft-knee clipping
            ic1eq = softKneeClip(bandNew, V_thresh, V_jfet_clamp);
            ic2eq = softKneeClip(lowNew, V_thresh, V_jfet_clamp);

            float currentLow = std::max(-V_rail, std::min(V_rail, ic2eq));
            float currentBand = std::max(-V_rail, std::min(V_rail, ic1eq));

            float currentVal = 0.0f;
            if (mode < 0.5f) {
                currentVal = currentBand; // BP
            } else if (mode < 1.5f) {
                currentVal = highClipped; // HP
            } else {
                currentVal = highClipped + currentLow; // Notch (Summing preserves phase cancellation)
            }

            sumLow += currentLow;
            sumVal += currentVal;
        }

        float lpOut = sumLow / 4.0f;
        float switchedOut = sumVal / 4.0f;

        return { lpOut, switchedOut };
    }
};
