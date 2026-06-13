#pragma once
#include <cmath>
#include <algorithm>

struct Slopes {
    float currentVoltage = 0.0f;
    bool loopOffset = true;

    // LED Accumulators
    float riseSamples = 0.0f;
    float fallSamples = 0.0f;
    int totalSamples = 0;

    // LED Brightness Outputs
    float riseLed = 0.0f;
    float fallLed = 0.0f;

    struct Output {
        float value;
        float riseLed;
        float fallLed;
    };

    Output process(float inputVal, float cvVal, float knobRate, int mode, int shape, bool isExponential) {
        const float RAIL_MAX = 12.0f;
        const float RAIL_MIN = -12.0f;
        const float LOOP_OFFSET = 12.0f;
        const float BLIP_OFFSET = 8.0f;
        const float EXP_AMT = 0.17f / 12.0f; // Scale feedback amount to 12V range
        const float MAX_COEFF = 0.144f;      // Scale coefficients to 12V range
        const float INSTANT = 0.1416f;
        const int LED_UPDATE_RATE = 1600;

        // --- 1. Rise/fall rate control ---
        float isExpFactor = isExponential ? 1.0f : 0.0f;
        float speedCtrl = knobRate + (cvVal * 0.5f) - isExpFactor * currentVoltage * EXP_AMT;
        speedCtrl = std::max(-0.2f, std::min(1.2f, speedCtrl)); // Allow negative CV to speed up slightly beyond knob limit

        // Rate mapping
        float rate = MAX_COEFF * std::exp(-4.0f * speedCtrl * (2.0f + speedCtrl));
        float riseCoeff = (shape == 0) ? INSTANT : rate;
        float fallCoeff = (shape == 2) ? INSTANT : rate;

        // --- 2. Target logic ---
        float target = inputVal; // target is input by default

        if (mode == 0 && loopOffset) {
            target += LOOP_OFFSET;
        } else if (mode == 2) {
            target += BLIP_OFFSET;
        }

        // --- 3. Travel towards target ---
        float delta = target - currentVoltage;
        float incr = (delta > 0.0f) ? riseCoeff : -fallCoeff;
        
        float nextVoltage;
        if (delta > 0.0f) {
            nextVoltage = std::min(currentVoltage + incr, RAIL_MAX);
        } else {
            nextVoltage = std::max(currentVoltage + incr, RAIL_MIN);
        }

        // --- 4. Behaviour upon hitting target ---
        bool crossed = false;
        if (delta > 0.0f && nextVoltage >= target) {
            crossed = true;
        } else if (delta < 0.0f && nextVoltage <= target) {
            crossed = true;
        }

        if (crossed) {
            nextVoltage = target;
            if (mode == 0) {
                loopOffset = !loopOffset;
            }
        }
        currentVoltage = nextVoltage;

        // --- 5. LED accumulation ---
        // dir > 0 when delta < 0 (falling), dir < 0 when delta > 0 (rising)
        // Scale threshold by 12.0 for Volts
        float dir = std::tanh(delta * (30.0f / 12.0f));
        if (dir > 0.0f) {
            riseSamples += dir;
        } else {
            fallSamples -= dir;
        }

        totalSamples++;
        if (totalSamples >= LED_UPDATE_RATE) {
            riseLed = std::pow(riseSamples / totalSamples, 0.25f);
            fallLed = std::pow(fallSamples / totalSamples, 0.25f);
            riseSamples = 0.0f;
            fallSamples = 0.0f;
            totalSamples = 0;
        }

        return { currentVoltage, riseLed, fallLed };
    }
};
