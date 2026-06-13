#include "plugin_local.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Custom lights matching panel themes
// ─────────────────────────────────────────────────────────────────────────────
struct OrangeLight : componentlibrary::GrayModuleLightWidget {
    OrangeLight() { addBaseColor(color::fromHexString("#ff8c00")); }
};
struct PurpleLight : componentlibrary::GrayModuleLightWidget {
    PurpleLight() { addBaseColor(color::fromHexString("#bf5fff")); }
};
struct CyanLight : componentlibrary::GrayModuleLightWidget {
    CyanLight() { addBaseColor(color::fromHexString("#00e5ff")); }
};
struct NeonGreenLight : componentlibrary::GrayModuleLightWidget {
    NeonGreenLight() { addBaseColor(color::fromHexString("#39ff14")); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Custom knob / switch widgets matching the new designs
// ─────────────────────────────────────────────────────────────────────────────
struct WorkshopSvgKnob : app::SvgKnob {
    WorkshopSvgKnob() {
        minAngle = -0.83f * (float)M_PI;
        maxAngle =  0.83f * (float)M_PI;
        shadow->opacity = 0.0f;
    }
};

struct WorkshopLargeKnobRed : WorkshopSvgKnob {
    WorkshopLargeKnobRed() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/knob_large_red.svg")));
    }
};
struct WorkshopSmallKnobRed : WorkshopSvgKnob {
    WorkshopSmallKnobRed() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/knob_small_red.svg")));
    }
};
struct WorkshopLargeKnobGrey : WorkshopSvgKnob {
    WorkshopLargeKnobGrey() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/knob_large_grey.svg")));
    }
};
struct WorkshopSmallKnobGrey : WorkshopSvgKnob {
    WorkshopSmallKnobGrey() {
        setSvg(Svg::load(asset::plugin(pluginInstance, "res/knob_small_grey.svg")));
    }
};

struct WorkshopToggleSwitch2way : app::SvgSwitch {
    WorkshopToggleSwitch2way() {
        momentary = false;
        shadow->opacity = 0.0;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/switch_up.svg")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/switch_down.svg")));
        box.size = Vec(24.f, 28.f);
    }
    void draw(const DrawArgs& args) override {
        float svgW = 24.f, svgH = 28.f;
        float offX = (box.size.x - svgW) / 2.f;
        float offY = (box.size.y - svgH) / 2.f;
        nvgSave(args.vg);
        nvgTranslate(args.vg, offX, offY);
        float scale = 1.125f;
        nvgTranslate(args.vg, svgW / 2.f * (1.f - scale), svgH / 2.f * (1.f - scale));
        nvgScale(args.vg, scale, scale);
        SvgSwitch::draw(args);
        nvgRestore(args.vg);
    }
};

struct WorkshopToggleSwitch3way : app::SvgSwitch {
    WorkshopToggleSwitch3way() {
        momentary = false;
        shadow->opacity = 0.0;
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/toggleSwitch_0.svg")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/toggleSwitch_1.svg")));
        addFrame(Svg::load(asset::plugin(pluginInstance, "res/toggleSwitch_2.svg")));
        box.size = Vec(24.f, 28.f);
    }
    void draw(const DrawArgs& args) override {
        float svgW = 24.f, svgH = 28.f;
        float offX = (box.size.x - svgW) / 2.f;
        float offY = (box.size.y - svgH) / 2.f;
        nvgSave(args.vg);
        nvgTranslate(args.vg, offX, offY);
        float scale = 1.125f;
        nvgTranslate(args.vg, svgW / 2.f * (1.f - scale), svgH / 2.f * (1.f - scale));
        nvgScale(args.vg, scale, scale);
        SvgSwitch::draw(args);
        nvgRestore(args.vg);
    }
};

// Transparent footswitch widget placed on top of panel graphics
struct TransparentSwitch : app::SvgSwitch {
    TransparentSwitch() {
        momentary = false;
        shadow->opacity = 0.0;
        box.size = Vec(24.f, 24.f);
    }
    void draw(const DrawArgs& args) override {
        // Invisible
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Pedalboard Module
// 42HP panel — 5 independent effects with serial normaling & Level Converter
// ─────────────────────────────────────────────────────────────────────────────
struct Pedalboard : Module {

    enum PedalId { PEDAL_FUZZ=0, PEDAL_DELAY, PEDAL_CHORUS, PEDAL_REVERB, PEDAL_BOOST };

    enum ParamIds {
        // Fuzz
        FUZZ_ACTIVE_PARAM,
        FUZZ_DRIVE_PARAM,
        FUZZ_TONE_PARAM,
        FUZZ_LEVEL_PARAM,
        FUZZ_BYPASS_PARAM,

        // Delay
        DELAY_ACTIVE_PARAM,
        DELAY_TIME_PARAM,
        DELAY_FEEDBACK_PARAM,
        DELAY_MIX_PARAM,
        DELAY_TAP_PARAM,

        // Chorus
        CHORUS_ACTIVE_PARAM,
        CHORUS_RATE_PARAM,
        CHORUS_DEPTH_PARAM,
        CHORUS_MIX_PARAM,
        CHORUS_MODE_PARAM,

        // Reverb
        REVERB_ACTIVE_PARAM,
        REVERB_DECAY_PARAM,
        REVERB_MIX_PARAM,
        REVERB_TYPE_PARAM,

        // Boost
        BOOST_ACTIVE_PARAM,
        BOOST_GAIN_PARAM,
        BOOST_BASS_PARAM,
        BOOST_TREBLE_PARAM,
        BOOST_FAT_PARAM,

        NUM_PARAMS
    };

    enum InputIds  {
        // Fuzz
        FUZZ_IN_INPUT,
        FUZZ_DRIVE_CV_INPUT,
        FUZZ_LEVEL_CV_INPUT,

        // Delay
        DELAY_IN_INPUT,
        DELAY_TIME_CV_INPUT,
        DELAY_FDBK_CV_INPUT,

        // Chorus
        CHORUS_IN_L_INPUT,
        CHORUS_IN_R_INPUT,
        CHORUS_RATE_CV_INPUT,
        CHORUS_DEPTH_CV_INPUT,

        // Reverb
        REVERB_IN_L_INPUT,
        REVERB_IN_R_INPUT,
        REVERB_DECAY_CV_INPUT,
        REVERB_MIX_CV_INPUT,

        // Boost
        BOOST_IN_L_INPUT,
        BOOST_IN_R_INPUT,
        BOOST_CV_INPUT,

        // Level Converter
        EURO_TO_LINE_IN_INPUT,
        LINE_TO_EURO_IN_INPUT,

        NUM_INPUTS
    };

    enum OutputIds {
        // Fuzz
        FUZZ_OUT_L_OUTPUT,
        FUZZ_OUT_R_OUTPUT,

        // Delay
        DELAY_OUT_L_OUTPUT,
        DELAY_OUT_R_OUTPUT,

        // Chorus
        CHORUS_OUT_L_OUTPUT,
        CHORUS_OUT_R_OUTPUT,

        // Reverb
        REVERB_OUT_L_OUTPUT,
        REVERB_OUT_R_OUTPUT,

        // Boost
        BOOST_OUT_L_OUTPUT,

        // Level Converter
        EURO_TO_LINE_OUT_OUTPUT,
        LINE_TO_EURO_OUT_OUTPUT,

        NUM_OUTPUTS
    };

    enum LightIds  {
        FUZZ_ACTIVE_LIGHT,
        DELAY_ACTIVE_LIGHT,
        DELAY_TAP_LIGHT,
        CHORUS_ACTIVE_LIGHT,
        REVERB_ACTIVE_LIGHT,
        BOOST_ACTIVE_LIGHT,
        NUM_LIGHTS
    };

    // Right-click options
    int pedalOrder[5] = {PEDAL_FUZZ, PEDAL_DELAY, PEDAL_CHORUS, PEDAL_REVERB, PEDAL_BOOST};
    bool pedalMode = true;

    json_t* dataToJson() override {
        json_t* rootJ = json_object();
        json_object_set_new(rootJ, "pedalMode", json_boolean(pedalMode));
        json_t* orderA = json_array();
        for (int i = 0; i < 5; i++) {
            json_array_append_new(orderA, json_integer(pedalOrder[i]));
        }
        json_object_set_new(rootJ, "pedalOrder", orderA);
        return rootJ;
    }

    void dataFromJson(json_t* rootJ) override {
        json_t* modeJ = json_object_get(rootJ, "pedalMode");
        if (modeJ) {
            pedalMode = json_boolean_value(modeJ);
        }
        json_t* orderA = json_object_get(rootJ, "pedalOrder");
        if (orderA && json_is_array(orderA)) {
            for (int i = 0; i < 5; i++) {
                json_t* idJ = json_array_get(orderA, i);
                if (idJ && json_is_integer(idJ)) {
                    pedalOrder[i] = json_integer_value(idJ);
                }
            }
        }
    }

    // ── DSP helpers ──────────────────────────────────────────────────────────
    struct OnePoleLPF {
        float state = 0.0f;
        void process(float in, float fc, float sr, float& out) {
            float a = std::max(0.0001f, std::min(1.0f, 2.f * (float)M_PI * fc / sr));
            state += a * (in - state);
            out = state;
        }
        void reset() { state = 0.f; }
    };

    struct SVF {
        float s1 = 0.f, s2 = 0.f;
        void reset() { s1 = s2 = 0.f; }
        float processLowpass(float x, float fc, float Q, float fs) {
            float g = std::max(0.001f, std::min(0.99f, tanf((float)M_PI * fc / fs)));
            float k = 1.f / Q;
            float h = 1.f / (1.f + g * (g + k));
            float v1 = (x - s2 - (g + k) * s1) * h;
            float v2 = s1 + g * v1;
            float lp = s2 + g * v2;
            s1 = 2.f * v2 - s1;
            s2 = 2.f * lp - s2;
            return lp;
        }
    };

    struct DelayBuffer {
        std::vector<float> buf;
        int wi = 0;
        void resize(int n) { buf.assign(n, 0.f); wi = 0; }
        void write(float s) {
            if (buf.empty()) return;
            buf[wi++] = s;
            if (wi >= (int)buf.size()) wi = 0;
        }
        float read(float ds) {
            if (buf.empty()) return 0.f;
            float ri = (float)wi - ds;
            while (ri < 0.f) ri += buf.size();
            while (ri >= (float)buf.size()) ri -= buf.size();
            int i0 = (int)ri, i1 = (i0 + 1) % (int)buf.size();
            float f = ri - i0;
            return (1.f - f) * buf[i0] + f * buf[i1];
        }
        void reset() { std::fill(buf.begin(), buf.end(), 0.f); wi = 0; }
    };

    struct CombFilter {
        std::vector<float> buf;
        int wi = 0;
        float feedback = 0.f, filterState = 0.f, damp = 0.f;
        void resize(int n) { buf.assign(n, 0.f); wi = 0; filterState = 0.f; }
        float process(float in) {
            float out = buf[wi];
            filterState = out * (1.f - damp) + filterState * damp;
            buf[wi] = in + filterState * feedback;
            if (++wi >= (int)buf.size()) wi = 0;
            return out;
        }
        void reset() { std::fill(buf.begin(), buf.end(), 0.f); wi = 0; filterState = 0.f; }
    };

    struct AllpassFilter {
        std::vector<float> buf;
        int wi = 0;
        float g = 0.5f;
        void resize(int n) { buf.assign(n, 0.f); wi = 0; }
        float process(float in) {
            float out = buf[wi];
            float y = -g * in + out;
            buf[wi] = in + g * out;
            if (++wi >= (int)buf.size()) wi = 0;
            return y;
        }
        void reset() { std::fill(buf.begin(), buf.end(), 0.f); wi = 0; }
    };

    struct ReverbEffect {
        CombFilter    combs[8];
        AllpassFilter allpasses[4];
        void init(const int* cl, const int* al, float ratio) {
            for (int i = 0; i < 8; i++) { int n = std::max(2,(int)roundf(cl[i]*ratio)); combs[i].resize(n); }
            for (int i = 0; i < 4; i++) { int n = std::max(2,(int)roundf(al[i]*ratio)); allpasses[i].resize(n); }
        }
        float process(float in, float room, float damp) {
            float s = in * 0.015f, sum = 0.f;
            for (int i = 0; i < 8; i++) { combs[i].feedback = room; combs[i].damp = damp; sum += combs[i].process(s); }
            float ap = sum;
            for (int i = 0; i < 4; i++) ap = allpasses[i].process(ap);
            return ap;
        }
        void reset() { for (auto& c : combs) c.reset(); for (auto& a : allpasses) a.reset(); }
    };

    // ── BOOST DSP Stage ──────────────────────────────────────────────────────
    struct BoostEffect {
        OnePoleLPF bassLPF;
        OnePoleLPF trebleLPF;
        
        void reset() {
            bassLPF.reset();
            trebleLPF.reset();
        }
        
        float process(float in, float gain, float bass, float treble, bool fat, float sr) {
            // Gain parameter maps to clean boost up to +24dB (factor 1.0 to 16.0)
            float ampGain = 1.f + gain * 15.f;
            float x = in * ampGain;
            
            // Bass: low shelf filter at 150 Hz
            float bassLp = 0.f;
            bassLPF.process(x, 150.f, sr, bassLp);
            x += bass * 0.75f * bassLp;
            
            // Treble: high shelf filter (HP = x - LP at 3000 Hz)
            float trebleLp = 0.f;
            trebleLPF.process(x, 3000.f, sr, trebleLp);
            float trebleHp = x - trebleLp;
            x += treble * 0.75f * trebleHp;
            
            // JFET Saturation or soft clipping
            if (fat) {
                // Low-mid bump at 350 Hz plus asymmetric waveshaping
                float offset = 0.12f;
                float x_off = x * 0.45f + offset;
                float y = x_off / (1.f + fabsf(x_off));
                float y_offset = offset / (1.f + offset);
                x = (y - y_offset) * 2.8f;
            } else {
                // Gentle soft clip
                float limit = 10.f;
                if (fabsf(x) < limit) {
                    x = x - 0.05f * (x*x*x) / (limit*limit);
                } else {
                    x = (x > 0.f ? limit : -limit);
                }
            }
            return x;
        }
    };

    // ── DSP state ────────────────────────────────────────────────────────────
    OnePoleLPF distLPF_L, distLPF_R;
    
    float chorusPhase = 0.f;
    DelayBuffer chorusDelay_L, chorusDelay_R;
    
    DelayBuffer feedbackDelay_L, feedbackDelay_R;
    SVF delayLPF_L, delayLPF_R;
    
    ReverbEffect reverb_L, reverb_R;
    BoostEffect boost_L, boost_R;

    // Delay Tap tempo helpers
    dsp::SchmittTrigger tapTrigger;
    float timeSinceLastTap = 999.f;
    float targetDelayTime = 0.35f;
    float lastKnobValue = -1.f;
    float tapLfoPhase = 0.f;

    // ── Constructor ──────────────────────────────────────────────────────────
    Pedalboard() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        // Fuzz
        configParam(FUZZ_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Fuzz: Active");
        configParam(FUZZ_DRIVE_PARAM,  0.f, 100.f, 30.f, "Fuzz: Drive");
        configParam(FUZZ_TONE_PARAM,   500.f, 8000.f, 4000.f, "Fuzz: Tone", " Hz");
        configParam(FUZZ_LEVEL_PARAM,  0.f, 2.f, 1.f, "Fuzz: Level");
        configParam(FUZZ_BYPASS_PARAM, 0.f, 1.f, 0.f, "Fuzz: Manual Bypass Switch");

        // Delay
        configParam(DELAY_ACTIVE_PARAM,   0.f, 1.f, 0.f, "Delay: Active");
        configParam(DELAY_TIME_PARAM,     0.001f, 1.0f, 0.35f, "Delay: Time", " s");
        configParam(DELAY_FEEDBACK_PARAM, 0.f, 0.95f, 0.4f, "Delay: Feedback");
        configParam(DELAY_MIX_PARAM,      0.f, 1.f, 0.4f, "Delay: Mix");
        configParam(DELAY_TAP_PARAM,      0.f, 1.f, 0.f, "Delay: Tap Tempo");

        // Chorus
        configParam(CHORUS_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Chorus: Active");
        configParam(CHORUS_RATE_PARAM,   0.1f, 5.0f, 1.0f, "Chorus: Rate", " Hz");
        configParam(CHORUS_DEPTH_PARAM,  0.f, 0.005f, 0.002f, "Chorus: Depth", " s");
        configParam(CHORUS_MIX_PARAM,    0.f, 1.f, 0.5f, "Chorus: Mix");
        configParam(CHORUS_MODE_PARAM,   0.f, 1.f, 0.f, "Chorus: Mode (0=Chorus, 1=Vibrato)");

        // Reverb
        configParam(REVERB_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Reverb: Active");
        configParam(REVERB_DECAY_PARAM,  0.1f, 0.98f, 0.5f, "Reverb: Decay");
        configParam(REVERB_MIX_PARAM,    0.f, 1.f, 0.4f, "Reverb: Mix");
        configParam(REVERB_TYPE_PARAM,   0.f, 2.f, 0.f, "Reverb: Type (0=Hall, 1=Plate, 2=Room)");

        // Boost
        configParam(BOOST_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Boost: Active");
        configParam(BOOST_GAIN_PARAM,   0.f, 1.f, 0.2f, "Boost: Gain");
        configParam(BOOST_BASS_PARAM,   -1.f, 1.f, 0.f, "Boost: Bass");
        configParam(BOOST_TREBLE_PARAM, -1.f, 1.f, 0.f, "Boost: Treble");
        configParam(BOOST_FAT_PARAM,    0.f, 1.f, 0.f, "Boost: Fat switch");

        // Input/Output mappings
        configInput(FUZZ_IN_INPUT, "Fuzz Input (Line Level)");
        configInput(FUZZ_DRIVE_CV_INPUT, "Fuzz Drive CV");
        configInput(FUZZ_LEVEL_CV_INPUT, "Fuzz Level CV");
        configInput(DELAY_IN_INPUT, "Delay Input (Line Level)");
        configInput(DELAY_TIME_CV_INPUT, "Delay Time CV");
        configInput(DELAY_FDBK_CV_INPUT, "Delay Feedback CV");
        configInput(CHORUS_IN_L_INPUT, "Chorus Left Input (Line Level)");
        configInput(CHORUS_IN_R_INPUT, "Chorus Right Input (Line Level)");
        configInput(CHORUS_RATE_CV_INPUT, "Chorus Rate CV");
        configInput(CHORUS_DEPTH_CV_INPUT, "Chorus Depth CV");
        configInput(REVERB_IN_L_INPUT, "Reverb Left Input (Line Level)");
        configInput(REVERB_IN_R_INPUT, "Reverb Right Input (Line Level)");
        configInput(REVERB_DECAY_CV_INPUT, "Reverb Decay CV");
        configInput(REVERB_MIX_CV_INPUT, "Reverb Mix CV");
        configInput(BOOST_IN_L_INPUT, "Boost Left Input (Line Level)");
        configInput(BOOST_IN_R_INPUT, "Boost Right Input (Line Level)");
        configInput(BOOST_CV_INPUT, "Boost CV");

        // Level Converter (top row)
        configInput(EURO_TO_LINE_IN_INPUT, "Eurorack to Line Input (Eurorack Level)");
        configInput(LINE_TO_EURO_IN_INPUT, "Line to Eurorack Input (Line Level)");

        configOutput(FUZZ_OUT_L_OUTPUT, "Fuzz Left Output (Line Level)");
        configOutput(FUZZ_OUT_R_OUTPUT, "Fuzz Right Output (Line Level)");
        configOutput(DELAY_OUT_L_OUTPUT, "Delay Left Output (Line Level)");
        configOutput(DELAY_OUT_R_OUTPUT, "Delay Right Output (Line Level)");
        configOutput(CHORUS_OUT_L_OUTPUT, "Chorus Left Output (Line Level)");
        configOutput(CHORUS_OUT_R_OUTPUT, "Chorus Right Output (Line Level)");
        configOutput(REVERB_OUT_L_OUTPUT, "Reverb Left Output (Line Level)");
        configOutput(REVERB_OUT_R_OUTPUT, "Reverb Right Output (Line Level)");
        configOutput(BOOST_OUT_L_OUTPUT, "Boost Left/Mono Output (Line Level)");

        // Level Converter (top row)
        configOutput(EURO_TO_LINE_OUT_OUTPUT, "Eurorack to Line Output (Line Level)");
        configOutput(LINE_TO_EURO_OUT_OUTPUT, "Line to Eurorack Output (Eurorack Level)");

        onSampleRateChange();
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        if (sr < 1000.f) sr = 44100.f;

        chorusDelay_L.resize((int)(0.1f * sr));
        chorusDelay_R.resize((int)(0.1f * sr));
        feedbackDelay_L.resize((int)(1.2f * sr));
        feedbackDelay_R.resize((int)(1.2f * sr));
        delayLPF_L.reset(); delayLPF_R.reset();

        float ratio = sr / 44100.f;
        const int cL[8] = {1116,1188,1277,1350,1422,1496,1557,1617};
        const int cR[8] = {1139,1211,1300,1373,1445,1519,1580,1640};
        const int aL[4] = {556,441,341,225};
        const int aR[4] = {579,464,364,248};
        reverb_L.init(cL, aL, ratio);
        reverb_R.init(cR, aR, ratio);

        distLPF_L.reset(); distLPF_R.reset();
        boost_L.reset(); boost_R.reset();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Per-Effect DSP Process Blocks (Operate at internal EURORACK Level)
    // ─────────────────────────────────────────────────────────────────────────────
    void processFuzz(float& L, float& R, float sr) {
        bool manuallyBypassed = params[FUZZ_BYPASS_PARAM].getValue() > 0.5f;
        bool pedalActive = params[FUZZ_ACTIVE_PARAM].getValue() > 0.5f;
        
        bool active = pedalActive && !manuallyBypassed;
        lights[FUZZ_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);
        
        if (!active) return;

        float driveMod = inputs[FUZZ_DRIVE_CV_INPUT].getVoltage() * 10.f;
        float drive = std::max(0.f, std::min(100.f, params[FUZZ_DRIVE_PARAM].getValue() + driveMod));
        
        float levelMod = inputs[FUZZ_LEVEL_CV_INPUT].getVoltage() * 0.2f;
        float level = std::max(0.f, std::min(2.f, params[FUZZ_LEVEL_PARAM].getValue() + levelMod));
        
        float tone = params[FUZZ_TONE_PARAM].getValue();

        float pi = (float)M_PI;
        float xL = L / 5.f, xR = R / 5.f;
        float yL = (xL * (pi + drive)) / (pi + drive * fabsf(xL));
        float yR = (xR * (pi + drive)) / (pi + drive * fabsf(xR));
        float wL = yL * 5.f * level, wR = yR * 5.f * level;
        
        distLPF_L.process(wL, tone, sr, L);
        distLPF_R.process(wR, tone, sr, R);
    }

    void processDelay(float& L, float& R, float sr, float dt) {
        bool active = params[DELAY_ACTIVE_PARAM].getValue() > 0.5f;
        lights[DELAY_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        // Tap Tempo Detector
        timeSinceLastTap += dt;
        float currentKnob = params[DELAY_TIME_PARAM].getValue();
        if (lastKnobValue < 0.f) {
            lastKnobValue = currentKnob;
        }
        if (fabsf(currentKnob - lastKnobValue) > 0.0005f) {
            targetDelayTime = currentKnob;
            lastKnobValue = currentKnob;
        }
        if (tapTrigger.process(params[DELAY_TAP_PARAM].getValue())) {
            if (timeSinceLastTap > 0.1f && timeSinceLastTap < 2.0f) {
                targetDelayTime = timeSinceLastTap;
                lastKnobValue = currentKnob;
            }
            timeSinceLastTap = 0.f;
        }

        // Tap LED blinking
        tapLfoPhase += dt / targetDelayTime;
        if (tapLfoPhase >= 1.f) tapLfoPhase -= 1.f;
        lights[DELAY_TAP_LIGHT].setBrightness(tapLfoPhase < 0.15f ? 1.f : 0.f);

        if (!active) {
            feedbackDelay_L.write(L);
            feedbackDelay_R.write(R);
            return;
        }

        float timeMod = inputs[DELAY_TIME_CV_INPUT].getVoltage() * 0.1f;
        float time = std::max(0.001f, std::min(1.0f, targetDelayTime + timeMod));
        
        float fbMod = inputs[DELAY_FDBK_CV_INPUT].getVoltage() * 0.1f;
        float fb = std::max(0.f, std::min(0.95f, params[DELAY_FEEDBACK_PARAM].getValue() + fbMod));
        
        float mix = params[DELAY_MIX_PARAM].getValue();

        float ds = time * sr;
        float wL = delayLPF_L.processLowpass(feedbackDelay_L.read(ds), 2000.f, 0.707f, sr);
        float wR = delayLPF_R.processLowpass(feedbackDelay_R.read(ds), 2000.f, 0.707f, sr);
        
        feedbackDelay_L.write(L + wL * fb);
        feedbackDelay_R.write(R + wR * fb);
        
        L += mix * (wL - L);
        R += mix * (wR - R);
    }

    void processChorus(float& L, float& R, float sr, float dt) {
        bool active = params[CHORUS_ACTIVE_PARAM].getValue() > 0.5f;
        lights[CHORUS_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        float rateMod = inputs[CHORUS_RATE_CV_INPUT].getVoltage() * 0.5f;
        float rate = std::max(0.1f, std::min(5.0f, params[CHORUS_RATE_PARAM].getValue() + rateMod));

        chorusPhase += 2.f * (float)M_PI * rate * dt;
        if (chorusPhase >= 2.f * (float)M_PI) chorusPhase -= 2.f * (float)M_PI;

        chorusDelay_L.write(L);
        chorusDelay_R.write(R);

        if (!active) return;

        float depthMod = inputs[CHORUS_DEPTH_CV_INPUT].getVoltage() * 0.0005f;
        float depth = std::max(0.f, std::min(0.005f, params[CHORUS_DEPTH_PARAM].getValue() + depthMod));
        
        float mix = params[CHORUS_MIX_PARAM].getValue();
        int mode = (int)params[CHORUS_MODE_PARAM].getValue();

        float lfoL = 0.f, lfoR = 0.f;
        if (mode == 1) {
            // Triangle LFO
            lfoL = 2.f * fabsf(chorusPhase / (float)M_PI - 1.f) - 1.f;
            lfoR = -lfoL;
            depth *= 1.25f; // slightly deeper vibrato
        } else {
            // Sine LFO
            lfoL = sinf(chorusPhase);
            lfoR = -lfoL;
        }

        L += mix * (chorusDelay_L.read((0.025f + lfoL * depth) * sr) - L);
        R += mix * (chorusDelay_R.read((0.025f + lfoR * depth) * sr) - R);
    }

    void processReverb(float& L, float& R, float sr) {
        bool active = params[REVERB_ACTIVE_PARAM].getValue() > 0.5f;
        lights[REVERB_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        if (!active) return;

        float decayMod = inputs[REVERB_DECAY_CV_INPUT].getVoltage() * 0.1f;
        float decay = std::max(0.1f, std::min(0.98f, params[REVERB_DECAY_PARAM].getValue() + decayMod));
        
        float mixMod = inputs[REVERB_MIX_CV_INPUT].getVoltage() * 0.2f;
        float mix = std::max(0.f, std::min(1.f, params[REVERB_MIX_PARAM].getValue() + mixMod));
        
        int type = (int)params[REVERB_TYPE_PARAM].getValue();

        float room = decay;
        float damp = 0.35f;

        if (type == 0) {
            room = room * 0.15f + 0.82f;
            damp = 0.6f;
        } else if (type == 1) {
            room = room * 0.2f + 0.74f;
            damp = 0.2f;
        } else {
            room = room * 0.3f + 0.45f;
            damp = 0.4f;
        }

        float wL = reverb_L.process(L, room, damp) * 2.f;
        float wR = reverb_R.process(R, room, damp) * 2.f;
        
        L += mix * (wL - L);
        R += mix * (wR - R);
    }

    void processBoost(float& L, float& R, float sr) {
        bool active = params[BOOST_ACTIVE_PARAM].getValue() > 0.5f;
        lights[BOOST_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        if (!active) return;

        float gainMod = inputs[BOOST_CV_INPUT].getVoltage() * 0.2f;
        float gain = std::max(0.f, std::min(1.f, params[BOOST_GAIN_PARAM].getValue() + gainMod));
        
        float bass = params[BOOST_BASS_PARAM].getValue();
        float treble = params[BOOST_TREBLE_PARAM].getValue();
        bool fat = params[BOOST_FAT_PARAM].getValue() > 0.5f;

        L = boost_L.process(L, gain, bass, treble, fat, sr);
        R = boost_R.process(R, gain, bass, treble, fat, sr);
    }

    void process(const ProcessArgs& args) override {
        float sr = args.sampleRate;
        float dt = args.sampleTime;

        // ── 1. Level Converter Processing ────────────────────────────────────
        // Eurorack to Line (0.2x attenuation: +/- 10V Eurorack in ➜ +/- 2V Line out)
        if (inputs[EURO_TO_LINE_IN_INPUT].isConnected()) {
            float inVal = inputs[EURO_TO_LINE_IN_INPUT].getVoltage();
            outputs[EURO_TO_LINE_OUT_OUTPUT].setVoltage(inVal * 0.2f);
        } else {
            outputs[EURO_TO_LINE_OUT_OUTPUT].setVoltage(0.f);
        }

        // Line to Eurorack (5.0x amplification: +/- 2V Line in ➜ +/- 10V Eurorack out)
        if (inputs[LINE_TO_EURO_IN_INPUT].isConnected()) {
            float inVal = inputs[LINE_TO_EURO_IN_INPUT].getVoltage();
            outputs[LINE_TO_EURO_OUT_OUTPUT].setVoltage(inVal * 5.f);
        } else {
            outputs[LINE_TO_EURO_OUT_OUTPUT].setVoltage(0.f);
        }

        // ── 2. Pedals Normaled Processing Chain ──────────────────────────────
        // All pedal internal signals are evaluated at Eurorack level (+/-10V)
        // for maximum DSP resolution and compatibility. Physical jacks scale
        // voltages according to the pedalMode:
        // - In Pedal Level Mode (pedalMode = true):
        //   Physical inputs scale by 5.0x (Line to Euro), outputs scale by 0.2x (Euro to Line).
        // - In Eurorack Level Mode (pedalMode = false):
        //   No scaling is applied (1.0x).
        float inputScale = pedalMode ? 5.0f : 1.0f;
        float outputScale = pedalMode ? 0.2f : 1.0f;

        float pedalInL[5] = {0.f};
        float pedalInR[5] = {0.f};
        float pedalOutL[5] = {0.f};
        float pedalOutR[5] = {0.f};

        for (int i = 0; i < 5; i++) {
            int id = pedalOrder[i];
            
            float L_in = 0.f;
            float R_in = 0.f;
            bool patched = false;

            // Check if inputs are physically patched, scaling according to pedalMode
            if (id == PEDAL_FUZZ) {
                patched = inputs[FUZZ_IN_INPUT].isConnected();
                if (patched) {
                    L_in = inputs[FUZZ_IN_INPUT].getVoltage() * inputScale;
                    R_in = L_in;
                }
            } else if (id == PEDAL_DELAY) {
                patched = inputs[DELAY_IN_INPUT].isConnected();
                if (patched) {
                    L_in = inputs[DELAY_IN_INPUT].getVoltage() * inputScale;
                    R_in = L_in;
                }
            } else if (id == PEDAL_CHORUS) {
                patched = inputs[CHORUS_IN_L_INPUT].isConnected() || inputs[CHORUS_IN_R_INPUT].isConnected();
                if (patched) {
                    L_in = inputs[CHORUS_IN_L_INPUT].isConnected() ? inputs[CHORUS_IN_L_INPUT].getVoltage() * inputScale : 0.f;
                    R_in = inputs[CHORUS_IN_R_INPUT].isConnected() ? inputs[CHORUS_IN_R_INPUT].getVoltage() * inputScale : L_in;
                    if (!inputs[CHORUS_IN_L_INPUT].isConnected()) L_in = R_in;
                }
            } else if (id == PEDAL_REVERB) {
                patched = inputs[REVERB_IN_L_INPUT].isConnected() || inputs[REVERB_IN_R_INPUT].isConnected();
                if (patched) {
                    L_in = inputs[REVERB_IN_L_INPUT].isConnected() ? inputs[REVERB_IN_L_INPUT].getVoltage() * inputScale : 0.f;
                    R_in = inputs[REVERB_IN_R_INPUT].isConnected() ? inputs[REVERB_IN_R_INPUT].getVoltage() * inputScale : L_in;
                    if (!inputs[REVERB_IN_L_INPUT].isConnected()) L_in = R_in;
                }
            } else if (id == PEDAL_BOOST) {
                patched = inputs[BOOST_IN_L_INPUT].isConnected() || inputs[BOOST_IN_R_INPUT].isConnected();
                if (patched) {
                    L_in = inputs[BOOST_IN_L_INPUT].isConnected() ? inputs[BOOST_IN_L_INPUT].getVoltage() * inputScale : 0.f;
                    R_in = inputs[BOOST_IN_R_INPUT].isConnected() ? inputs[BOOST_IN_R_INPUT].getVoltage() * inputScale : L_in;
                    if (!inputs[BOOST_IN_L_INPUT].isConnected()) L_in = R_in;
                }
            }

            // Normaled fallback routing (already at Eurorack level internally)
            if (!patched) {
                if (i > 0) {
                    int prevId = pedalOrder[i - 1];
                    L_in = pedalOutL[prevId];
                    R_in = pedalOutR[prevId];
                } else {
                    L_in = 0.f;
                    R_in = 0.f;
                }
            }

            pedalInL[id] = L_in;
            pedalInR[id] = R_in;

            // Process effect
            if (id == PEDAL_FUZZ) {
                processFuzz(L_in, R_in, sr);
            } else if (id == PEDAL_DELAY) {
                processDelay(L_in, R_in, sr, dt);
            } else if (id == PEDAL_CHORUS) {
                processChorus(L_in, R_in, sr, dt);
            } else if (id == PEDAL_REVERB) {
                processReverb(L_in, R_in, sr);
            } else if (id == PEDAL_BOOST) {
                processBoost(L_in, R_in, sr);
            }

            // Bound internal signal to safe levels
            pedalOutL[id] = std::max(-12.f, std::min(12.f, L_in));
            pedalOutR[id] = std::max(-12.f, std::min(12.f, R_in));
        }

        // Set output voltages on physical jacks (converted back to outputScale)
        outputs[FUZZ_OUT_L_OUTPUT].setVoltage(pedalOutL[PEDAL_FUZZ] * outputScale);
        outputs[FUZZ_OUT_R_OUTPUT].setVoltage(pedalOutR[PEDAL_FUZZ] * outputScale);
        outputs[DELAY_OUT_L_OUTPUT].setVoltage(pedalOutL[PEDAL_DELAY] * outputScale);
        outputs[DELAY_OUT_R_OUTPUT].setVoltage(pedalOutR[PEDAL_DELAY] * outputScale);
        outputs[CHORUS_OUT_L_OUTPUT].setVoltage(pedalOutL[PEDAL_CHORUS] * outputScale);
        outputs[CHORUS_OUT_R_OUTPUT].setVoltage(pedalOutR[PEDAL_CHORUS] * outputScale);
        outputs[REVERB_OUT_L_OUTPUT].setVoltage(pedalOutL[PEDAL_REVERB] * outputScale);
        outputs[REVERB_OUT_R_OUTPUT].setVoltage(pedalOutR[PEDAL_REVERB] * outputScale);
        outputs[BOOST_OUT_L_OUTPUT].setVoltage(pedalOutL[PEDAL_BOOST] * outputScale);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget Layout (42HP — 5 columns of 126px width)
// ─────────────────────────────────────────────────────────────────────────────
struct WorkshopPedalboardWidget : ModuleWidget {

    static const char* pedalName(int id) {
        switch (id) {
            case Pedalboard::PEDAL_FUZZ:   return "FUZZ";
            case Pedalboard::PEDAL_DELAY:  return "DELAY";
            case Pedalboard::PEDAL_CHORUS: return "CHORUS";
            case Pedalboard::PEDAL_REVERB: return "REVERB";
            case Pedalboard::PEDAL_BOOST:  return "BOOST";
            default: return "UNKNOWN";
        }
    }

    WorkshopPedalboardWidget(Pedalboard* module) {
        setModule(module);
        setPanel(Svg::load(asset::plugin(pluginInstance, "res/Pedalboard.svg")));

        // ═══════════════════════════════════════════════════════
        // LEVEL CONVERTER (Bunched in center top row at y = 22)
        // ═══════════════════════════════════════════════════════
        addInput(createInputCentered<DarkPJ301MPort>(Vec(280.f, 22.f), module, Pedalboard::EURO_TO_LINE_IN_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(300.f, 22.f), module, Pedalboard::EURO_TO_LINE_OUT_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(330.f, 22.f), module, Pedalboard::LINE_TO_EURO_IN_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(350.f, 22.f), module, Pedalboard::LINE_TO_EURO_OUT_OUTPUT));

        // ═══════════════════════════════════════════════════════
        // COLUMN 1: FUZZ (center x = 63)
        // ═══════════════════════════════════════════════════════
        addParam(createParamCentered<WorkshopLargeKnobRed>(Vec(63.f, 95.f), module, Pedalboard::FUZZ_DRIVE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobRed>(Vec(33.f, 165.f), module, Pedalboard::FUZZ_TONE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobRed>(Vec(93.f, 165.f), module, Pedalboard::FUZZ_LEVEL_PARAM));
        addParam(createParamCentered<WorkshopToggleSwitch2way>(Vec(63.f, 165.f), module, Pedalboard::FUZZ_BYPASS_PARAM));
        
        // Footswitch stomp
        addParam(createParamCentered<TransparentSwitch>(Vec(63.f, 356.f), module, Pedalboard::FUZZ_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(63.f, 340.f), module, Pedalboard::FUZZ_ACTIVE_LIGHT));

        // Jacks (IN, OUT L, OUT R, DRIVE CV, LEVEL CV)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(25.f, 265.f), module, Pedalboard::FUZZ_IN_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(25.f, 305.f), module, Pedalboard::FUZZ_OUT_L_OUTPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(48.f, 305.f), module, Pedalboard::FUZZ_OUT_R_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(78.f, 305.f), module, Pedalboard::FUZZ_DRIVE_CV_INPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(101.f, 305.f), module, Pedalboard::FUZZ_LEVEL_CV_INPUT));

        // ═══════════════════════════════════════════════════════
        // COLUMN 2: DELAY (center x = 189)
        // ═══════════════════════════════════════════════════════
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(189.f, 95.f), module, Pedalboard::DELAY_TIME_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(159.f, 165.f), module, Pedalboard::DELAY_FEEDBACK_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(219.f, 165.f), module, Pedalboard::DELAY_MIX_PARAM));
        
        // Tap button & light
        addParam(createParamCentered<componentlibrary::LEDButton>(Vec(189.f, 212.f), module, Pedalboard::DELAY_TAP_PARAM));
        addChild(createLightCentered<MediumLight<PurpleLight>>(Vec(189.f, 212.f), module, Pedalboard::DELAY_TAP_LIGHT));

        // Footswitch stomp
        addParam(createParamCentered<TransparentSwitch>(Vec(189.f, 356.f), module, Pedalboard::DELAY_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<PurpleLight>>(Vec(189.f, 340.f), module, Pedalboard::DELAY_ACTIVE_LIGHT));

        // Jacks (IN, OUT L, OUT R, TIME CV, FDBK CV)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(151.f, 265.f), module, Pedalboard::DELAY_IN_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(151.f, 305.f), module, Pedalboard::DELAY_OUT_L_OUTPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(174.f, 305.f), module, Pedalboard::DELAY_OUT_R_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(204.f, 305.f), module, Pedalboard::DELAY_TIME_CV_INPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(227.f, 305.f), module, Pedalboard::DELAY_FDBK_CV_INPUT));

        // ═══════════════════════════════════════════════════════
        // COLUMN 3: CHORUS (center x = 315)
        // ═══════════════════════════════════════════════════════
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(315.f, 95.f), module, Pedalboard::CHORUS_RATE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(285.f, 165.f), module, Pedalboard::CHORUS_DEPTH_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(345.f, 165.f), module, Pedalboard::CHORUS_MIX_PARAM));
        addParam(createParamCentered<WorkshopToggleSwitch2way>(Vec(315.f, 212.f), module, Pedalboard::CHORUS_MODE_PARAM));

        // Footswitch stomp
        addParam(createParamCentered<TransparentSwitch>(Vec(315.f, 356.f), module, Pedalboard::CHORUS_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<CyanLight>>(Vec(315.f, 340.f), module, Pedalboard::CHORUS_ACTIVE_LIGHT));

        // Jacks (IN, OUT, IN L, L/R, RATE CV, DEPTH CV)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(277.f, 265.f), module, Pedalboard::CHORUS_IN_L_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(300.f, 265.f), module, Pedalboard::CHORUS_OUT_L_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(277.f, 305.f), module, Pedalboard::CHORUS_IN_R_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(300.f, 305.f), module, Pedalboard::CHORUS_OUT_R_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(330.f, 305.f), module, Pedalboard::CHORUS_RATE_CV_INPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(353.f, 305.f), module, Pedalboard::CHORUS_DEPTH_CV_INPUT));

        // ═══════════════════════════════════════════════════════
        // COLUMN 4: REVERB (center x = 441)
        // ═══════════════════════════════════════════════════════
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(441.f, 95.f), module, Pedalboard::REVERB_DECAY_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(411.f, 165.f), module, Pedalboard::REVERB_MIX_PARAM));
        addParam(createParamCentered<WorkshopToggleSwitch3way>(Vec(471.f, 165.f), module, Pedalboard::REVERB_TYPE_PARAM));

        // Footswitch stomp
        addParam(createParamCentered<TransparentSwitch>(Vec(441.f, 356.f), module, Pedalboard::REVERB_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<NeonGreenLight>>(Vec(441.f, 340.f), module, Pedalboard::REVERB_ACTIVE_LIGHT));

        // Jacks (IN L, OUT L, IN R, OUT R, DECAY CV, MIX CV)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(403.f, 265.f), module, Pedalboard::REVERB_IN_L_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(426.f, 265.f), module, Pedalboard::REVERB_OUT_L_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(403.f, 305.f), module, Pedalboard::REVERB_IN_R_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(426.f, 305.f), module, Pedalboard::REVERB_OUT_R_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(456.f, 305.f), module, Pedalboard::REVERB_DECAY_CV_INPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(479.f, 305.f), module, Pedalboard::REVERB_MIX_CV_INPUT));

        // ═══════════════════════════════════════════════════════
        // COLUMN 5: BOOST (center x = 567)
        // ═══════════════════════════════════════════════════════
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(567.f, 95.f), module, Pedalboard::BOOST_GAIN_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(537.f, 165.f), module, Pedalboard::BOOST_BASS_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(597.f, 165.f), module, Pedalboard::BOOST_TREBLE_PARAM));
        addParam(createParamCentered<WorkshopToggleSwitch2way>(Vec(567.f, 165.f), module, Pedalboard::BOOST_FAT_PARAM));

        // Footswitch stomp
        addParam(createParamCentered<TransparentSwitch>(Vec(567.f, 356.f), module, Pedalboard::BOOST_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<OrangeLight>>(Vec(567.f, 340.f), module, Pedalboard::BOOST_ACTIVE_LIGHT));

        // Jacks (IN L, IN R, OUT L, BOOST CV)
        addInput(createInputCentered<DarkPJ301MPort>(Vec(529.f, 265.f), module, Pedalboard::BOOST_IN_L_INPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(529.f, 305.f), module, Pedalboard::BOOST_IN_R_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(552.f, 305.f), module, Pedalboard::BOOST_OUT_L_OUTPUT));
        addInput(createInputCentered<DarkPJ301MPort>(Vec(594.f, 305.f), module, Pedalboard::BOOST_CV_INPUT));
    }

    // ── Direct widget text rendering override ────────────────────────────────
    void draw(const DrawArgs& args) override {
        // Render panel SVG and children first
        ModuleWidget::draw(args);
        
        // Render text on top
        std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/Roboto-Bold.ttf"));
        if (!font) {
            font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        }
        
        nvgSave(args.vg);
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
        }
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        
        // ── 1. Header titles & Level Converter labels ────────────────────────
        nvgFontSize(args.vg, 12.f);
        nvgFillColor(args.vg, nvgRGBA(230, 230, 230, 255));
        nvgText(args.vg, 70.f, 22.f, "PEDALBOARD 5x", NULL);
        
        nvgFontSize(args.vg, 8.5f);
        nvgFillColor(args.vg, nvgRGBA(230, 230, 230, 255));
        nvgText(args.vg, 500.f, 22.f, "WORKSHOP SERIES", NULL);

        nvgFontSize(args.vg, 5.0f);
        nvgFillColor(args.vg, nvgRGBA(140, 140, 140, 255));
        nvgText(args.vg, 280.f, 6.f, "EURO IN", NULL);
        nvgText(args.vg, 300.f, 6.f, "LINE OUT", NULL);
        nvgText(args.vg, 330.f, 6.f, "LINE IN", NULL);
        nvgText(args.vg, 350.f, 6.f, "EURO OUT", NULL);
        
        nvgFontSize(args.vg, 5.2f);
        nvgFillColor(args.vg, nvgRGBA(170, 170, 170, 255));
        nvgText(args.vg, 290.f, 39.f, "EURO ➜ LINE", NULL);
        nvgText(args.vg, 340.f, 39.f, "LINE ➜ EURO", NULL);
        
        // ── 2. Pedal Name & Knob Labels ──────────────────────────────────────
        // FUZZ (col 1, center 63)
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, nvgRGBA(245, 245, 245, 255));
        nvgText(args.vg, 63.f, 58.f, "FUZZ", NULL);
        
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(160, 160, 160, 255));
        nvgText(args.vg, 63.f, 123.f, "FUZZ", NULL);
        nvgText(args.vg, 33.f, 196.f, "TONE", NULL);
        nvgText(args.vg, 93.f, 196.f, "VOL", NULL);
        nvgText(args.vg, 63.f, 210.f, "BYPASS", NULL);
        
        // DELAY (col 2, center 189)
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, nvgRGBA(245, 245, 245, 255));
        nvgText(args.vg, 189.f, 58.f, "DELAY", NULL);
        
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(160, 160, 160, 255));
        nvgText(args.vg, 159.f, 116.f, "TIME", NULL);
        nvgText(args.vg, 219.f, 116.f, "FEEDBACK", NULL);
        nvgText(args.vg, 189.f, 181.f, "MIX", NULL);
        nvgText(args.vg, 189.f, 224.f, "TAP", NULL);
        
        // CHORUS (col 3, center 315)
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, nvgRGBA(245, 245, 245, 255));
        nvgText(args.vg, 315.f, 58.f, "CHORUS", NULL);
        
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(160, 160, 160, 255));
        nvgText(args.vg, 285.f, 116.f, "RATE", NULL);
        nvgText(args.vg, 345.f, 116.f, "DEPTH", NULL);
        nvgText(args.vg, 315.f, 181.f, "MIX", NULL);
        nvgText(args.vg, 315.f, 224.f, "MODE", NULL);
        
        // REVERB (col 4, center 441)
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, nvgRGBA(245, 245, 245, 255));
        nvgText(args.vg, 441.f, 58.f, "REVERB", NULL);
        
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(160, 160, 160, 255));
        nvgText(args.vg, 411.f, 116.f, "DECAY", NULL);
        nvgText(args.vg, 441.f, 116.f, "DAMP", NULL);
        nvgText(args.vg, 471.f, 116.f, "MIX", NULL);
        nvgText(args.vg, 441.f, 181.f, "TYPE", NULL);
        
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 6.2f);
        nvgText(args.vg, 453.f, 150.f, "Hall", NULL);
        nvgText(args.vg, 453.f, 160.f, "Plate", NULL);
        nvgText(args.vg, 453.f, 170.f, "Room", NULL);
        
        // BOOST (col 5, center 567)
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFontSize(args.vg, 13.f);
        nvgFillColor(args.vg, nvgRGBA(245, 245, 245, 255));
        nvgText(args.vg, 567.f, 58.f, "BOOST", NULL);
        
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(160, 160, 160, 255));
        nvgText(args.vg, 567.f, 123.f, "BOOST", NULL);
        nvgText(args.vg, 537.f, 196.f, "BASS", NULL);
        nvgText(args.vg, 597.f, 196.f, "TREBLE", NULL);
        nvgText(args.vg, 567.f, 210.f, "FAT", NULL);
        
        // ── 3. Jacks Labels ──────────────────────────────────────────────────
        nvgFontSize(args.vg, 6.2f);
        nvgFillColor(args.vg, nvgRGBA(100, 100, 100, 255));
        
        // Col 1 (Fuzz)
        nvgText(args.vg, 25.f, 254.f, "IN", NULL);
        nvgText(args.vg, 25.f, 322.f, "OUT(L)", NULL);
        nvgText(args.vg, 48.f, 322.f, "OUT(R)", NULL);
        nvgText(args.vg, 78.f, 254.f, "DRIVE", NULL);
        nvgText(args.vg, 101.f, 254.f, "LEVEL", NULL);
        nvgText(args.vg, 78.f, 322.f, "CV", NULL);
        nvgText(args.vg, 101.f, 322.f, "CV", NULL);
        
        // Col 2 (Delay)
        nvgText(args.vg, 151.f, 254.f, "IN", NULL);
        nvgText(args.vg, 151.f, 322.f, "OUT(L)", NULL);
        nvgText(args.vg, 174.f, 322.f, "OUT(R)", NULL);
        nvgText(args.vg, 204.f, 254.f, "TIME", NULL);
        nvgText(args.vg, 227.f, 254.f, "FDBK", NULL);
        nvgText(args.vg, 204.f, 322.f, "CV", NULL);
        nvgText(args.vg, 227.f, 322.f, "CV", NULL);
        
        // Col 3 (Chorus)
        nvgText(args.vg, 277.f, 254.f, "IN", NULL);
        nvgText(args.vg, 300.f, 254.f, "OUT", NULL);
        nvgText(args.vg, 277.f, 322.f, "IN(L)", NULL);
        nvgText(args.vg, 300.f, 322.f, "L/R", NULL);
        nvgText(args.vg, 330.f, 254.f, "RATE", NULL);
        nvgText(args.vg, 353.f, 254.f, "DEPTH", NULL);
        nvgText(args.vg, 330.f, 322.f, "CV", NULL);
        nvgText(args.vg, 353.f, 322.f, "CV", NULL);
        
        // Col 4 (Reverb)
        nvgText(args.vg, 403.f, 254.f, "IN", NULL);
        nvgText(args.vg, 426.f, 254.f, "OUT", NULL);
        nvgText(args.vg, 403.f, 322.f, "L/R", NULL);
        nvgText(args.vg, 426.f, 322.f, "L/R", NULL);
        nvgText(args.vg, 456.f, 254.f, "DECAY", NULL);
        nvgText(args.vg, 479.f, 254.f, "MIX", NULL);
        nvgText(args.vg, 456.f, 322.f, "CV", NULL);
        nvgText(args.vg, 479.f, 322.f, "CV", NULL);
        
        // Col 5 (Boost)
        nvgText(args.vg, 529.f, 254.f, "IN", NULL);
        nvgText(args.vg, 529.f, 322.f, "IN(L)", NULL);
        nvgText(args.vg, 552.f, 322.f, "OUT(L)", NULL);
        nvgText(args.vg, 594.f, 254.f, "BOOST", NULL);
        nvgText(args.vg, 594.f, 322.f, "CV", NULL);
        
        // ── 4. Footer ────────────────────────────────────────────────────────
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(60, 60, 60, 255));
        nvgText(args.vg, 315.f, 372.f, "VCV COMPATIBLE | EURORACK MODULE", NULL);
        
        nvgRestore(args.vg);
    }

    // ── Context menu for chain order and pedal level mode ────────────────────
    void appendContextMenu(Menu* menu) override {
        Pedalboard* mod = dynamic_cast<Pedalboard*>(this->module);
        if (!mod) return;

        menu->addChild(new MenuSeparator);
        
        // Option to toggle Pedal Level vs Eurorack Level Mode
        menu->addChild(createBoolPtrMenuItem("Pedal Level Mode (Attenuate input / Boost output)", "", &mod->pedalMode));
        
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("── Pedal Chain Order ──"));
        menu->addChild(createMenuLabel("  (right-click to reorder, saved with patch)"));

        for (int pos = 0; pos < 5; pos++) {
            int id = mod->pedalOrder[pos];

            std::string label = std::string("  ") + std::to_string(pos + 1) + ".  " + pedalName(id);
            menu->addChild(createMenuLabel(label.c_str()));

            // Move earlier
            if (pos > 0) {
                int captPos = pos;
                menu->addChild(createMenuItem(
                    std::string("    ↑  Move ") + pedalName(id) + " earlier",
                    "",
                    [mod, captPos]() {
                        std::swap(mod->pedalOrder[captPos], mod->pedalOrder[captPos - 1]);
                    }
                ));
            }

            // Move later
            if (pos < 4) {
                int captPos = pos;
                menu->addChild(createMenuItem(
                    std::string("    ↓  Move ") + pedalName(id) + " later",
                    "",
                    [mod, captPos]() {
                        std::swap(mod->pedalOrder[captPos], mod->pedalOrder[captPos + 1]);
                    }
                ));
            }
        }

        // Reset order
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuItem(
            "Reset chain to default order",
            "",
            [mod]() {
                mod->pedalOrder[0] = Pedalboard::PEDAL_FUZZ;
                mod->pedalOrder[1] = Pedalboard::PEDAL_DELAY;
                mod->pedalOrder[2] = Pedalboard::PEDAL_CHORUS;
                mod->pedalOrder[3] = Pedalboard::PEDAL_REVERB;
                mod->pedalOrder[4] = Pedalboard::PEDAL_BOOST;
            }
        ));
    }
};

Model* modelPedalboard = createModel<Pedalboard, WorkshopPedalboardWidget>("Pedalboard");
