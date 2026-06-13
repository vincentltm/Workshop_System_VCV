#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Amplifier {
    float rmsSmooth = 0.001f;
    float smoothAmpLevel = 0.0f;
    float sampleRate = 48000.0f;
    uint32_t noiseSeed = 123456789;

    // --- Clean/Mic Mode (Mikrophonie Preamp) States ---
    float micHpState = 0.0f;
    float micLpState = 0.0f;

    // --- Lofi Mode (Mini Drive) States ---
    float lofiInHpState = 0.0f;
    float lofiFeedbackState = 0.0f;
    float lofiOutHpState = 0.0f;

    float soft_clip(float x, float limit) {
        float threshold = limit * 0.8f;
        if (std::abs(x) < threshold) {
            return x;
        }
        float range = limit - threshold;
        if (x > 0.0f) {
            return threshold + range * std::tanh((x - threshold) / range);
        } else {
            return -threshold + range * std::tanh((x + threshold) / range);
        }
    }

    float getMicTaper(float norm) {
        if (norm <= 0.0f) return 0.0f;
        if (norm >= 1.0f) return 1.0f;
        static const float x[] = {0.0f, 0.05f, 0.20f, 0.35f, 0.50f, 0.65f, 0.80f, 0.95f, 1.0f};
        static const float y[] = {0.0f, 0.073398f, 0.314201f, 0.836420f, 0.943054f, 0.973946f, 0.988000f, 0.996000f, 1.0f};
        for (int i = 0; i < 8; ++i) {
            if (norm >= x[i] && norm <= x[i+1]) {
                float t = (norm - x[i]) / (x[i+1] - x[i]);
                return y[i] + t * (y[i+1] - y[i]);
            }
        }
        return 1.0f;
    }

    float getLofiTaper(float norm) {
        if (norm <= 0.0f) return 0.0f;
        if (norm >= 1.0f) return 0.014845f;
        static const float x[] = {0.0f, 0.05f, 0.20f, 0.35f, 0.50f, 0.65f, 0.80f, 0.95f, 1.0f};
        static const float y[] = {0.000000f, 0.000938f, 0.007323f, 0.011752f, 0.013114f, 0.013894f, 0.014824f, 0.015079f, 0.014845f};
        for (int i = 0; i < 8; ++i) {
            if (norm >= x[i] && norm <= x[i+1]) {
                float t = (norm - x[i]) / (x[i+1] - x[i]);
                return y[i] + t * (y[i+1] - y[i]);
            }
        }
        return 0.014845f;
    }

    void setSampleRate(float sr) {
        sampleRate = sr;
    }

    float process(float inputVal, float gainVal, int mode) {
        float dt = 1.0f / sampleRate;
        float preamp_out_volts = 0.0f;

        float norm_gain = std::max(0.0f, std::min(1.0f, gainVal));

        // Fast thread-safe LCG noise generator
        noiseSeed = noiseSeed * 1664525 + 1013904223;
        float noiseRaw = ((float)noiseSeed / 4294967296.0f) - 0.5f; // [-0.5, 0.5]
        float analogNoise = noiseRaw * 0.0002f; // 100uV peak-to-peak input noise floor

        if (mode == 0) {
            // --- Clean / Mic Mode (Mikrophonie style) ---
            float micInput = inputVal + analogNoise;
            // Input HPF: C5 (4.7u) and R5 (1.0M) to GND -> fc = 0.034 Hz
            float rc_hp = 4.7f;
            float alpha_hp = dt / (rc_hp + dt);
            micHpState += alpha_hp * (micInput - micHpState);
            float hpFiltered = micInput - micHpState;

            // Non-inverting gain feedback network from mikrophonie_3-another0402 schematic:
            // R7 = 100k log pot, R6 = 10k resistor, R8 = 510 ohms resistor.
            float w_pot = getMicTaper(norm_gain);
            float R_pot = 100000.0f;
            float R_6 = 10000.0f;
            float R_8 = 510.0f;

            float R_pot23 = (1.0f - w_pot) * R_pot;
            // R_g is R_6 connected to GND in parallel with (R_pot23 + R_8) which also goes to GND
            float R_g = (R_6 * (R_pot23 + R_8)) / (R_6 + (R_pot23 + R_8));
            float R_f = w_pot * R_pot;

            float Av = 1.0f + R_f / R_g;

            // Feedback LPF: C7 = 22p in parallel with R_f
            float rc_lp = R_f * 22e-12f;
            float alpha_lp = dt / (rc_lp + dt);

            // Shelving LPF feedback path:
            float feedback_term = hpFiltered * (Av - 1.0f);
            micLpState += alpha_lp * (feedback_term - micLpState);

            float preamp_out = hpFiltered + micLpState;

            // Clip symmetrically using soft_clip at 11.5V (standard rail limit on 12V rails)
            preamp_out_volts = soft_clip(preamp_out, 11.5f);
        } else {
            // --- LoFi Mode (Mini Drive style) ---
            // Input attenuation based on fitted gain taper lookup table
            float taper = getLofiTaper(norm_gain);
            float input_attenuated = (inputVal + analogNoise) * taper;

            // Input HPF: C3 (100n) and R2 (100k) to GND -> fc = 15.9 Hz
            float rc_in = 1.0f / (2.0f * M_PI * 15.9f);
            float alpha_in = dt / (rc_in + dt);
            lofiInHpState += alpha_in * (input_attenuated - lofiInHpState);
            float v_in_ac = input_attenuated - lofiInHpState;

            // Physically accurate closed-loop BJT differential pair & output stage model.
            // With high open-loop gain (~3000) and no local degeneration, the feedback loop closes
            // and behaves as a hard-clipped op-amp stage.
            // Closed-loop gain A_cl = 201.0f, asymmetrical clipping rails: V_low = -9.2f, V_high = 5.3f.
            const float A_cl = 201.0f;
            const float V_low = -9.2f;
            const float V_high = 5.3f;
            float v_out = std::max(V_low, std::min(V_high, A_cl * v_in_ac));
            lofiFeedbackState = v_out / A_cl;

            // Output HPF: C7 (1u) and load (100k) -> fc = 1.59 Hz
            float rc_out = 1.0f / (2.0f * M_PI * 1.59f);
            float alpha_out = dt / (rc_out + dt);
            lofiOutHpState += alpha_out * (v_out - lofiOutHpState);
            // Apply output gain boost to allow the signal to reach the 11.5V rails, then clip
            preamp_out_volts = std::max(-11.5f, std::min(11.5f, (v_out - lofiOutHpState) * 1.58f));
        }

        // VU meter calculation (based on output)
        float rect = std::abs(preamp_out_volts) / 5.0f;
        rmsSmooth += (rect - rmsSmooth) * (rect > rmsSmooth ? 0.005f : 0.0005f);

        // Calibrate VU meter: scale range from -20 dB to +6 dB (where 0 dB = 5.0V output)
        float db = 20.0f * std::log10(std::max(1e-5f, rmsSmooth));
        float targetLevel = (db + 20.0f) / 26.0f;
        targetLevel = std::max(0.0f, std::min(1.0f, targetLevel));

        if (targetLevel > smoothAmpLevel) {
            smoothAmpLevel += (targetLevel - smoothAmpLevel) * 0.008f;
        } else {
            smoothAmpLevel += (targetLevel - smoothAmpLevel) * 0.0004f;
        }

        return preamp_out_volts;
    }

    float getVULevel() const {
        return smoothAmpLevel * 4.5f;
    }
};
