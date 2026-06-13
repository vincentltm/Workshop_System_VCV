#pragma once
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Mixer {
    struct Output {
        float mixL;
        float mixR;
        float phones1;
        float phones2;
    };

    Output process(float ch1, float ch2, float ch3, float ch4,
                   float vol1, float vol2, float vol3, float vol4,
                   float pan1, float pan2, float masterVol) {
        // Equal power panning for Ch 1 and Ch 2
        // pan is in range -1.0 to 1.0
        float theta1 = (pan1 + 1.0f) * (M_PI / 4.0f);
        float pan1L = std::cos(theta1);
        float pan1R = std::sin(theta1);

        float theta2 = (pan2 + 1.0f) * (M_PI / 4.0f);
        float pan2L = std::cos(theta2);
        float pan2R = std::sin(theta2);

        // Apply channel volume gains
        float ch1_L = ch1 * vol1 * pan1L;
        float ch1_R = ch1 * vol1 * pan1R;

        float ch2_L = ch2 * vol2 * pan2L;
        float ch2_R = ch2 * vol2 * pan2R;

        // Ch 3 and Ch 4 are mono, hardpanned center (0.707 to keep power equal)
        float ch3_L = ch3 * vol3 * 0.707f;
        float ch3_R = ch3 * vol3 * 0.707f;

        float ch4_L = ch4 * vol4 * 0.707f;
        float ch4_R = ch4 * vol4 * 0.707f;

        // Mixer Sum (modular level, unaffected by master vol)
        float mixL = ch1_L + ch2_L + ch3_L + ch4_L;
        float mixR = ch1_R + ch2_R + ch3_R + ch4_R;

        // Phones Out (post-master-volume)
        // masterVol is in range 0.0 to 2.0
        float phones1 = mixL * masterVol;
        float phones2 = mixR * masterVol;

        return { mixL, mixR, phones1, phones2 };
    }
};
