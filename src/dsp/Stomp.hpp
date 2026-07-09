#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>

struct Stomp {
    float sampleRate = 44100.0f;
    uint32_t noiseSeed = 123456789;

    // State variables
    float last_V_U4C_out = 0.0f;
    float last_V_target = 0.0f;
    float last_V_U4A_out = 0.0f;
    
    // DC blocker (High-pass filter / AC coupling) state
    float hp_w_last = 0.0f;
    float hp_input_last = 0.0f;

    // Noise State variables
    float pinkB0 = 0.0f;
    float pinkB1 = 0.0f;
    float pinkB2 = 0.0f;
    float humPhase = 0.0f;

    Stomp() {
        static uint32_t counter = 0;
        noiseSeed = std::rand() ^ (counter++ * 0x9e3779b9) ^ reinterpret_cast<uintptr_t>(this);
        humPhase = (((float)std::rand() / RAND_MAX) * 2.0f * M_PI);
    }

    void setSampleRate(float sr) {
        if (sr > 0.0f) {
            sampleRate = sr;
        }
    }

    void reset() {
        last_V_U4C_out = 0.0f;
        last_V_target = 0.0f;
        last_V_U4A_out = 0.0f;
        hp_w_last = 0.0f;
        hp_input_last = 0.0f;
    }

    // Fast thread-safe LCG noise generator
    float nextNoise() {
        noiseSeed = noiseSeed * 1664525 + 1013904223;
        return ((float)noiseSeed / 4294967296.0f) - 0.5f; // [-0.5, 0.5]
    }

    // Process a single sample
    // Inputs:
    // - stompIn: Eurorack level input voltage (STOMP_IN)
    // - returnInJack: Voltage at STOMP_RETURN jack (if connected)
    // - returnConnected: true if a cable is plugged into STOMP_RETURN
    // - pedalboardConnected: true if connected internally to the Pedalboard module
    // - fbKnobDeg: feedback knob angle (-150.0 to 150.0)
    // - blendKnobDeg: blend knob angle (-150.0 to 150.0)
    // Outputs:
    // - stompSend: The voltage to output on STOMP_SEND (instrument level or boosted)
    // - stompOut: The voltage to output on STOMP_OUT (Eurorack level)
    void process(float stompIn, float returnInJack, bool returnConnected, bool pedalboardConnected, float fbKnobDeg, float blendKnobDeg, float& stompSend, float& stompOut) {
        // Update Noise Sources
        float dt = 1.0f / sampleRate;
        
        // Mains hum (60 Hz + 120 Hz + 180 Hz)
        humPhase += 2.0f * M_PI * 60.0f * dt;
        if (humPhase > 2.0f * M_PI) {
            humPhase -= 2.0f * M_PI;
        }
        float hum = std::sin(humPhase) + 0.3f * std::sin(humPhase * 2.0f) + 0.15f * std::sin(humPhase * 3.0f);
        hum /= 1.45f; // [-0.5, 0.5] approx

        // Pink noise (1/f) approximation via 3-pole filter
        float white = nextNoise() * 2.0f; // Scale to [-1.0, 1.0]
        pinkB0 = 0.99765f * pinkB0 + white * 0.0990460f;
        pinkB1 = 0.96300f * pinkB1 + white * 0.2965164f;
        pinkB2 = 0.57000f * pinkB2 + white * 1.0526913f;
        float pink = (pinkB0 + pinkB1 + pinkB2 + white * 0.1848f) * 0.05f; // [-0.5, 0.5] approx

        // Generate small analog noise floors for each stage
        float v_n_U4A = (nextNoise() * 0.00005f) + (pink * 0.00005f) + (hum * 0.00003f); // Input stage U4A
        float v_n_U4D = nextNoise() * 0.0001f; // Attenuverter feedback stage U4D (mostly thermal)
        float v_n_U4C = (nextNoise() * 0.00005f) + (pink * 0.00005f) + (hum * 0.00003f); // Sum stage U4C

        // 1. Map feedback knob angle to x in [0.0, 1.0]
        float x = (fbKnobDeg + 150.0f) / 300.0f;
        x = std::max(0.0f, std::min(1.0f, x));

        // 2. Compute law-shaped ratio using 24k resistors (100k potentiometer)
        // ratio(x) = (31*x - 25*x^2) / (6 + 50*x - 50*x^2)
        float num = 31.0f * x - 25.0f * x * x;
        float den = 6.0f + 50.0f * x - 50.0f * x * x;
        float ratio = 0.5f;
        if (std::abs(den) > 1e-6f) {
            ratio = num / den;
        }
        ratio = std::max(0.0f, std::min(1.0f, ratio));

        // 3. Feedback gain is 2 * ratio - 1
        float gain_fb = 2.0f * ratio - 1.0f;

        // 4. Calculate U4D (attenuverter) output.
        // It attenuverts U4C's output from the previous sample and adds its own noise floor.
        // V_U4D_out = gain_fb * last_V_U4C_out + v_n_U4D, clamped to +/-11.5V rails
        float V_U4D_out = gain_fb * last_V_U4C_out + v_n_U4D;
        V_U4D_out = std::max(-11.5f, std::min(11.5f, V_U4D_out));

        // 5. Calculate U4A (input stage) output.
        // V_U4A_out = -0.082 * stompIn - 0.082 * V_U4D_out + v_n_U4A, clamped to +/-11.5V rails.
        float V_U4A_out = -0.082f * stompIn - 0.082f * V_U4D_out + v_n_U4A;
        V_U4A_out = std::max(-11.5f, std::min(11.5f, V_U4A_out));

        // 6. Stompbox Send jack output
        // If connected to Pedalboard, boost back to Eurorack level.
        // Otherwise output at natural instrument level.
        if (pedalboardConnected) {
            stompSend = V_U4A_out / 0.082f;
        } else {
            stompSend = V_U4A_out;
        }

        // 7. Get the input to the return high-pass filter (DC blocker).
        // If external return is plugged in:
        //   - If pedalboardConnected, scale down the Eurorack level returnInJack to instrument level.
        //   - Otherwise, use returnInJack directly (which is at instrument level).
        // If not connected, it normals to V_U4A_out (which is also at instrument level) as per the schematic.
        float V_ret_before_hp = 0.0f;
        if (returnConnected) {
            if (pedalboardConnected) {
                V_ret_before_hp = returnInJack * 0.082f;
            } else {
                V_ret_before_hp = returnInJack;
            }
        } else {
            V_ret_before_hp = V_U4A_out;
        }

        // 8. Process high-pass filter (DC blocker / AC coupling, fc = 1.59 Hz due to C28 = 100n and R98 = 1M)
        // w[n] = x[n] - x[n-1] + alpha_hp * w[n-1]
        float alpha_hp = std::exp(-2.0f * 3.14159265359f * 1.59f / sampleRate);
        float hp_w = V_ret_before_hp - hp_input_last + alpha_hp * hp_w_last;
        hp_input_last = V_ret_before_hp;
        hp_w_last = hp_w;
        float V_ret = hp_w;

        // 9. Map blend knob angle to y in [0.0, 1.0]
        float y = (blendKnobDeg + 150.0f) / 300.0f;
        y = std::max(0.0f, std::min(1.0f, y));

        // 10. Compute U4C (Dry/Wet mixer) target output voltage V_target.
        // Passive blend circuit equations:
        // V_target = -22 * (V_ret * 2y / (4y + 3) + V_U4A_out * 2(1-y) / (7 - 4y) + v_n_U4C)
        float term_wet = V_ret * (2.0f * y) / (4.0f * y + 3.0f);
        float term_dry = V_U4A_out * (2.0f * (1.0f - y)) / (7.0f - 4.0f * y);
        float V_target = -22.0f * (term_wet + term_dry + v_n_U4C);

        // 11. Process low-pass filter (fc = 10.26 kHz, due to R94 = 330k and C27 = 47pf)
        float tau = 3.3e5f * 47e-12f;
        float K = 2.0f * tau * sampleRate;
        float coef_b = 1.0f / (1.0f + K);
        float coef_a = (1.0f - K) / (1.0f + K);
        float V_U4C_out = coef_b * (V_target + last_V_target) - coef_a * last_V_U4C_out;

        last_V_target = V_target;
        V_U4C_out = std::max(-11.5f, std::min(11.5f, V_U4C_out));
        last_V_U4C_out = V_U4C_out;
        last_V_U4A_out = V_U4A_out;

        stompOut = V_U4C_out;
    }
};
