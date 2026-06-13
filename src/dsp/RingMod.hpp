#pragma once
#include <cmath>

struct RingMod {
    static inline float process(float inA, float inB) {
        // We model a four-quadrant analog multiplier with soft saturation using
        // the Schottky diode-ring approximation (Sebastian Azevedo design).
        // A_sat and B_sat represent saturation thresholds for both paths (carrier & modulator).
        const float A_sat = 10.0f;
        const float B_sat = 10.0f;

        // Apply soft saturation to both inputs
        float satA = A_sat * std::tanh(inA / A_sat);
        float satB = B_sat * std::tanh(inB / B_sat);

        // Perform the multiplication scaled by 5V (normalizing the product to VCV levels)
        float product = (satA * satB) / 5.0f;

        // Final output stage soft-limiting clipping (op-amp rails at ±12V)
        return 12.0f * std::tanh(product / 12.0f);
    }
};
