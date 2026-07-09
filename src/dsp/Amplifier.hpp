#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>

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

    // --- Noise State Variables ---
    float pinkB0 = 0.0f;
    float pinkB1 = 0.0f;
    float pinkB2 = 0.0f;

    float stagePinkB0 = 0.0f;
    float stagePinkB1 = 0.0f;
    float stagePinkB2 = 0.0f;

    float humPhase = 0.0f;
    float popcornState = 0.0f;

    Amplifier() {
        static uint32_t counter = 0;
        noiseSeed = std::rand() ^ (counter++ * 0x9e3779b9) ^ reinterpret_cast<uintptr_t>(this);
        humPhase = (((float)std::rand() / RAND_MAX) * 2.0f * M_PI);
    }

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

        // 1. Update Noise Sources
        // Fast thread-safe LCG noise generator
        noiseSeed = noiseSeed * 1664525 + 1013904223;
        float noiseRaw = ((float)noiseSeed / 4294967296.0f) - 0.5f; // [-0.5, 0.5]

        // Pink noise (1/f) approximation via 3-pole filter
        float white = noiseRaw * 2.0f; // Scale to [-1.0, 1.0]
        pinkB0 = 0.99765f * pinkB0 + white * 0.0990460f;
        pinkB1 = 0.96300f * pinkB1 + white * 0.2965164f;
        pinkB2 = 0.57000f * pinkB2 + white * 1.0526913f;
        float pink = (pinkB0 + pinkB1 + pinkB2 + white * 0.1848f) * 0.05f; // [-0.5, 0.5] approx

        // Mains hum (60 Hz + 120 Hz + 180 Hz)
        humPhase += 2.0f * M_PI * 60.0f * dt;
        if (humPhase > 2.0f * M_PI) {
            humPhase -= 2.0f * M_PI;
        }
        float hum = std::sin(humPhase) + 0.3f * std::sin(humPhase * 2.0f) + 0.15f * std::sin(humPhase * 3.0f);
        hum /= 1.45f; // [-0.5, 0.5] approx

        if (mode == 0) {
            // --- Clean / Mic Mode (Mikrophonie style) ---
            // Update popcorn noise (crackle)
            noiseSeed = noiseSeed * 1664525 + 1013904223;
            float randPop = ((float)noiseSeed / 4294967296.0f) - 0.5f;
            if (randPop > 0.4998f) {
                noiseSeed = noiseSeed * 1664525 + 1013904223;
                popcornState = (((float)noiseSeed / 4294967296.0f) - 0.5f) * 0.05f;
            }

            // Input-referred noise floor components (higher noise presence to scale with the massive 207x gain):
            // White: 300uV pp, Pink: 400uV pp, Hum: 150uV pp, Popcorn: 50uV pp
            float analogNoise = (noiseRaw * 0.0006f) + (pink * 0.0008f) + (hum * 0.0003f) + (popcornState * 0.001f);
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

            // Clip-aware non-inverting op-amp simulation to prevent state blow-up ("lockup")
            float limit = 11.5f;

            // 1. Calculate linear state update
            float V_c_lin = micLpState + alpha_lp * ((Av - 1.0f) * hpFiltered - micLpState);
            float V_out_lin = hpFiltered + V_c_lin;

            float preamp_out = V_out_lin;
            if (std::abs(V_out_lin) > limit * 0.8f) { // Enter soft-clipping region
                preamp_out = soft_clip(V_out_lin, limit);
                // Update feedback capacitor based on actual physically-clamped output
                micLpState = ((1.0f - alpha_lp) * micLpState + alpha_lp * (Av - 1.0f) * preamp_out) / (1.0f + alpha_lp * (Av - 1.0f));
            } else {
                micLpState = V_c_lin;
            }

            preamp_out_volts = preamp_out;
        } else {
            // --- LoFi Mode (Mini Drive style) ---
            // Input-referred noise floor (white + pink + hum) - scaled to standard levels
            float v_n_in = (noiseRaw * 0.0002f) + (pink * 0.00024f) + (hum * 0.0001f);

            // Input attenuation based on fitted gain taper lookup table
            float taper = getLofiTaper(norm_gain);
            float input_attenuated = (inputVal + v_n_in) * taper;

            // Input HPF: C3 (100n) and R2 (100k) to GND -> fc = 15.9 Hz
            float rc_in = 1.0f / (2.0f * M_PI * 15.9f);
            float alpha_in = dt / (rc_in + dt);
            lofiInHpState += alpha_in * (input_attenuated - lofiInHpState);
            float v_in_ac = input_attenuated - lofiInHpState;

            // Stage-referred noise floor of the BJT differential pair (scales with gain so it is silent when off)
            noiseSeed = noiseSeed * 1664525 + 1013904223;
            float stageNoiseRaw = ((float)noiseSeed / 4294967296.0f) - 0.5f;

            stagePinkB0 = 0.99765f * stagePinkB0 + stageNoiseRaw * 2.0f * 0.0990460f;
            stagePinkB1 = 0.96300f * stagePinkB1 + stageNoiseRaw * 2.0f * 0.2965164f;
            stagePinkB2 = 0.57000f * stagePinkB2 + stageNoiseRaw * 2.0f * 1.0526913f;
            float stagePink = (stagePinkB0 + stagePinkB1 + stagePinkB2 + stageNoiseRaw * 2.0f * 0.1848f) * 0.05f;

            // Scale stage-referred noise directly by norm_gain and set standard levels (300uV pp at max gain)
            float v_n_stage = ((stageNoiseRaw * 0.0003f) + (stagePink * 0.0003f) + (hum * 0.00015f)) * norm_gain;

            // Physically accurate closed-loop BJT differential pair & output stage model.
            // Closed-loop gain A_cl = 201.0f, asymmetrical clipping rails: V_low = -9.2f, V_high = 5.3f.
            const float A_cl = 201.0f;
            const float V_low = -9.2f;
            const float V_high = 5.3f;
            float v_out = std::max(V_low, std::min(V_high, A_cl * (v_in_ac + v_n_stage)));
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
