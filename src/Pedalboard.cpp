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
struct TransparentSwitch : ParamWidget {
    TransparentSwitch() {
        box.size = Vec(30.f, 30.f);
    }
    void onButton(const event::Button& e) override {
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
            auto* pq = getParamQuantity();
            if (pq) {
                float val = pq->getValue();
                pq->setValue(val > 0.5f ? 0.f : 1.f);
            }
            e.consume(this);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Pedalboard Module
// ─────────────────────────────────────────────────────────────────────────────
struct Pedalboard : Module {

    enum PedalId {
        PEDAL_DISTORTION = 0,
        PEDAL_DELAY = 1,
        PEDAL_CHORUS = 2,
        PEDAL_REVERB = 3,
        PEDAL_PHASER = 4
    };

    enum ParamIds {
        // Reverb
        REVERB_ACTIVE_PARAM,
        REVERB_DECAY_PARAM,
        REVERB_MIX_PARAM,

        // Delay
        DELAY_ACTIVE_PARAM,
        DELAY_TIME_PARAM,
        DELAY_FEEDBACK_PARAM,
        DELAY_MIX_PARAM,

        // Chorus
        CHORUS_ACTIVE_PARAM,
        CHORUS_RATE_PARAM,
        CHORUS_DEPTH_PARAM,
        CHORUS_MIX_PARAM,

        // Phaser
        PHASER_ACTIVE_PARAM,
        PHASER_RATE_PARAM,
        PHASER_DEPTH_PARAM,
        PHASER_MIX_PARAM,

        // Distortion
        DISTORTION_ACTIVE_PARAM,
        DISTORTION_DRIVE_PARAM,
        DISTORTION_TONE_PARAM,
        DISTORTION_LEVEL_PARAM,

        NUM_PARAMS
    };

    enum InputIds  {
        IN_INPUT,
        // Dummy inputs to prevent out-of-bounds crashes when loading old patches
        PORT_DUMMY_IN_1,
        PORT_DUMMY_IN_2,
        PORT_DUMMY_IN_3,
        PORT_DUMMY_IN_4,
        PORT_DUMMY_IN_5,
        PORT_DUMMY_IN_6,
        PORT_DUMMY_IN_7,
        PORT_DUMMY_IN_8,
        PORT_DUMMY_IN_9,
        PORT_DUMMY_IN_10,
        PORT_DUMMY_IN_11,
        PORT_DUMMY_IN_12,
        PORT_DUMMY_IN_13,
        PORT_DUMMY_IN_14,
        PORT_DUMMY_IN_15,
        PORT_DUMMY_IN_16,
        PORT_DUMMY_IN_17,
        PORT_DUMMY_IN_18,
        NUM_INPUTS
    };

    enum OutputIds {
        OUT_OUTPUT,
        // Dummy outputs to prevent out-of-bounds crashes when loading old patches
        PORT_DUMMY_OUT_1,
        PORT_DUMMY_OUT_2,
        PORT_DUMMY_OUT_3,
        PORT_DUMMY_OUT_4,
        PORT_DUMMY_OUT_5,
        PORT_DUMMY_OUT_6,
        PORT_DUMMY_OUT_7,
        PORT_DUMMY_OUT_8,
        PORT_DUMMY_OUT_9,
        PORT_DUMMY_OUT_10,
        PORT_DUMMY_OUT_11,
        NUM_OUTPUTS
    };

    enum LightIds  {
        REVERB_ACTIVE_LIGHT,
        DELAY_ACTIVE_LIGHT,
        CHORUS_ACTIVE_LIGHT,
        PHASER_ACTIVE_LIGHT,
        DISTORTION_ACTIVE_LIGHT,
        NUM_LIGHTS
    };

    int pedalOrder[5] = {PEDAL_DISTORTION, PEDAL_PHASER, PEDAL_CHORUS, PEDAL_DELAY, PEDAL_REVERB};
    bool pedalMode = false; // Kept for serialization compatibility

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

    struct PhaserAllpass {
        float x1 = 0.f;
        float y1 = 0.f;
        float process(float x, float fc, float sr) {
            float th = (float)M_PI * fc / sr;
            float g = tanf(th);
            float c = (g - 1.f) / (g + 1.f);
            float y = c * x + x1 - c * y1;
            x1 = x;
            y1 = y;
            return y;
        }
        void reset() {
            x1 = 0.f;
            y1 = 0.f;
        }
    };

    // ── DSP state ────────────────────────────────────────────────────────────
    OnePoleLPF distLPF;
    float chorusPhase = 0.f;
    DelayBuffer chorusDelay;
    DelayBuffer feedbackDelay;
    SVF delayLPF;
    ReverbEffect reverb;
    PhaserAllpass phaserAP[4];
    float phaserPhase = 0.f;
    float phaserFeedback = 0.f;

    // ── Constructor ──────────────────────────────────────────────────────────
    Pedalboard() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

        // Reverb
        configParam(REVERB_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Reverb: Active");
        configParam(REVERB_DECAY_PARAM,  0.1f, 0.98f, 0.5f, "Reverb: Size");
        configParam(REVERB_MIX_PARAM,    0.f, 1.f, 0.4f, "Reverb: Mix");

        // Delay
        configParam(DELAY_ACTIVE_PARAM,   0.f, 1.f, 0.f, "Delay: Active");
        configParam(DELAY_TIME_PARAM,     0.001f, 1.0f, 0.35f, "Delay: Time", " s");
        configParam(DELAY_FEEDBACK_PARAM, 0.f, 0.95f, 0.4f, "Delay: Feedback");
        configParam(DELAY_MIX_PARAM,      0.f, 1.f, 0.4f, "Delay: Mix");

        // Chorus
        configParam(CHORUS_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Chorus: Active");
        configParam(CHORUS_RATE_PARAM,   0.1f, 5.0f, 1.0f, "Chorus: Rate", " Hz");
        configParam(CHORUS_DEPTH_PARAM,  0.f, 0.005f, 0.002f, "Chorus: Depth", " s");
        configParam(CHORUS_MIX_PARAM,    0.f, 1.f, 0.5f, "Chorus: Mix");

        // Phaser
        configParam(PHASER_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Phaser: Active");
        configParam(PHASER_RATE_PARAM,   0.1f, 5.0f, 1.0f, "Phaser: Rate", " Hz");
        configParam(PHASER_DEPTH_PARAM,  0.f, 1.f, 0.5f, "Phaser: Depth");
        configParam(PHASER_MIX_PARAM,    0.f, 1.f, 0.5f, "Phaser: Mix");

        // Distortion
        configParam(DISTORTION_ACTIVE_PARAM, 0.f, 1.f, 0.f, "Distortion: Active");
        configParam(DISTORTION_DRIVE_PARAM,  0.f, 100.f, 30.f, "Distortion: Drive");
        configParam(DISTORTION_TONE_PARAM,   500.f, 8000.f, 4000.f, "Distortion: Tone", " Hz");
        configParam(DISTORTION_LEVEL_PARAM,  0.f, 2.f, 1.f, "Distortion: Level");

        configInput(IN_INPUT, "Input");
        configOutput(OUT_OUTPUT, "Output");

        onSampleRateChange();
    }

    void onSampleRateChange() override {
        float sr = APP->engine->getSampleRate();
        if (sr < 1000.f) sr = 44100.f;

        chorusDelay.resize((int)(0.1f * sr));
        feedbackDelay.resize((int)(1.2f * sr));
        delayLPF.reset();

        float ratio = sr / 44100.f;
        const int cL[8] = {1116,1188,1277,1350,1422,1496,1557,1617};
        const int aL[4] = {556,441,341,225};
        reverb.init(cL, aL, ratio);

        distLPF.reset();
        for (int i = 0; i < 4; i++) {
            phaserAP[i].reset();
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Per-Effect DSP Process Blocks (Operate at internal EURORACK Level)
    // ─────────────────────────────────────────────────────────────────────────────
    void processDistortion(float& s, float sr) {
        bool active = params[DISTORTION_ACTIVE_PARAM].getValue() > 0.5f;
        lights[DISTORTION_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);
        
        if (!active) return;

        float drive = params[DISTORTION_DRIVE_PARAM].getValue();
        float tone = params[DISTORTION_TONE_PARAM].getValue();
        float level = params[DISTORTION_LEVEL_PARAM].getValue();

        float pi = (float)M_PI;
        float x = s / 5.f;
        float y = (x * (pi + drive)) / (pi + drive * fabsf(x));
        float w = y * 5.f * level;
        
        distLPF.process(w, tone, sr, s);
    }

    void processDelay(float& s, float sr, float dt) {
        bool active = params[DELAY_ACTIVE_PARAM].getValue() > 0.5f;
        lights[DELAY_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        if (!active) {
            feedbackDelay.write(s);
            return;
        }

        float time = params[DELAY_TIME_PARAM].getValue();
        float fb = params[DELAY_FEEDBACK_PARAM].getValue();
        float mix = params[DELAY_MIX_PARAM].getValue();

        float ds = time * sr;
        float wet = delayLPF.processLowpass(feedbackDelay.read(ds), 2000.f, 0.707f, sr);
        
        feedbackDelay.write(s + wet * fb);
        
        s += mix * (wet - s);
    }

    void processChorus(float& s, float sr, float dt) {
        bool active = params[CHORUS_ACTIVE_PARAM].getValue() > 0.5f;
        lights[CHORUS_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        float rate = params[CHORUS_RATE_PARAM].getValue();

        chorusPhase += 2.f * (float)M_PI * rate * dt;
        if (chorusPhase >= 2.f * (float)M_PI) chorusPhase -= 2.f * (float)M_PI;

        chorusDelay.write(s);

        if (!active) return;

        float depth = params[CHORUS_DEPTH_PARAM].getValue();
        float mix = params[CHORUS_MIX_PARAM].getValue();

        float lfo = sinf(chorusPhase);
        s += mix * (chorusDelay.read((0.025f + lfo * depth) * sr) - s);
    }

    void processReverb(float& s, float sr) {
        bool active = params[REVERB_ACTIVE_PARAM].getValue() > 0.5f;
        lights[REVERB_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        if (!active) return;

        float decay = params[REVERB_DECAY_PARAM].getValue();
        float mix = params[REVERB_MIX_PARAM].getValue();

        float room = decay * 0.2f + 0.74f;
        float damp = 0.35f;

        float wet = reverb.process(s, room, damp) * 2.f;
        s += mix * (wet - s);
    }

    void processPhaser(float& s, float sr, float dt) {
        bool active = params[PHASER_ACTIVE_PARAM].getValue() > 0.5f;
        lights[PHASER_ACTIVE_LIGHT].setBrightness(active ? 1.f : 0.f);

        if (!active) return;

        float rate = params[PHASER_RATE_PARAM].getValue();
        float depth = params[PHASER_DEPTH_PARAM].getValue();
        float mix = params[PHASER_MIX_PARAM].getValue();

        phaserPhase += 2.f * (float)M_PI * rate * dt;
        if (phaserPhase >= 2.f * (float)M_PI) phaserPhase -= 2.f * (float)M_PI;

        float lfo = 0.5f + 0.5f * sinf(phaserPhase);
        float fc = 200.f * powf(20.0f, lfo * depth);

        float fbAmt = 0.5f;
        float x = s + phaserFeedback * fbAmt;

        float y = x;
        for (int i = 0; i < 4; i++) {
            y = phaserAP[i].process(y, fc, sr);
        }

        phaserFeedback = y;
        s += mix * (y - s);
    }

    void process(const ProcessArgs& args) override {
        float sr = args.sampleRate;
        float dt = args.sampleTime;

        float s = 0.f;
        if (inputs[IN_INPUT].isConnected()) {
            s = inputs[IN_INPUT].getVoltage();
        }

        for (int i = 0; i < 5; i++) {
            int id = pedalOrder[i];
            if (id == PEDAL_DISTORTION) {
                processDistortion(s, sr);
            } else if (id == PEDAL_DELAY) {
                processDelay(s, sr, dt);
            } else if (id == PEDAL_CHORUS) {
                processChorus(s, sr, dt);
            } else if (id == PEDAL_REVERB) {
                processReverb(s, sr);
            } else if (id == PEDAL_PHASER) {
                processPhaser(s, sr, dt);
            }
        }

        outputs[OUT_OUTPUT].setVoltage(s);
    }
};

struct WorkshopPedalboardWidget : ModuleWidget {

    static const char* pedalName(int id) {
        switch (id) {
            case Pedalboard::PEDAL_DISTORTION: return "DISTORTION";
            case Pedalboard::PEDAL_DELAY:      return "DELAY";
            case Pedalboard::PEDAL_CHORUS:     return "CHORUS";
            case Pedalboard::PEDAL_REVERB:     return "REVERB";
            case Pedalboard::PEDAL_PHASER:     return "PHASER";
            default: return "UNKNOWN";
        }
    }

    WorkshopPedalboardWidget(Pedalboard* module) {
        setModule(module);
        setPanel(Svg::load(asset::plugin(pluginInstance, "res/Pedalboard.svg")));

        // Input and Output
        addInput(createInputCentered<DarkPJ301MPort>(Vec(285.f, 175.f), module, Pedalboard::IN_INPUT));
        addOutput(createOutputCentered<DarkPJ301MPort>(Vec(15.f, 175.f), module, Pedalboard::OUT_OUTPUT));

        // Pedal 1: Reverb (Size, Mix, Active stomp, LED)
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(50.f, 115.f), module, Pedalboard::REVERB_DECAY_PARAM));
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(50.f, 195.f), module, Pedalboard::REVERB_MIX_PARAM));
        addParam(createParamCentered<TransparentSwitch>(Vec(50.f, 321.f), module, Pedalboard::REVERB_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<NeonGreenLight>>(Vec(50.f, 270.f), module, Pedalboard::REVERB_ACTIVE_LIGHT));

        // Pedal 2: Delay (Time, Feedback, Mix, Active stomp, LED)
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(100.f, 100.f), module, Pedalboard::DELAY_TIME_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(100.f, 160.f), module, Pedalboard::DELAY_FEEDBACK_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(100.f, 220.f), module, Pedalboard::DELAY_MIX_PARAM));
        addParam(createParamCentered<TransparentSwitch>(Vec(100.f, 321.f), module, Pedalboard::DELAY_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<PurpleLight>>(Vec(100.f, 270.f), module, Pedalboard::DELAY_ACTIVE_LIGHT));

        // Pedal 3: Chorus (Rate, Depth, Mix, Active stomp, LED)
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(150.f, 100.f), module, Pedalboard::CHORUS_RATE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(150.f, 160.f), module, Pedalboard::CHORUS_DEPTH_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(150.f, 220.f), module, Pedalboard::CHORUS_MIX_PARAM));
        addParam(createParamCentered<TransparentSwitch>(Vec(150.f, 321.f), module, Pedalboard::CHORUS_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<CyanLight>>(Vec(150.f, 270.f), module, Pedalboard::CHORUS_ACTIVE_LIGHT));

        // Pedal 4: Phaser (Rate, Depth, Mix, Active stomp, LED)
        addParam(createParamCentered<WorkshopLargeKnobGrey>(Vec(200.f, 100.f), module, Pedalboard::PHASER_RATE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(200.f, 160.f), module, Pedalboard::PHASER_DEPTH_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobGrey>(Vec(200.f, 220.f), module, Pedalboard::PHASER_MIX_PARAM));
        addParam(createParamCentered<TransparentSwitch>(Vec(200.f, 321.f), module, Pedalboard::PHASER_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<OrangeLight>>(Vec(200.f, 270.f), module, Pedalboard::PHASER_ACTIVE_LIGHT));

        // Pedal 5: Distortion (Drive, Tone, Level, Active stomp, LED)
        addParam(createParamCentered<WorkshopLargeKnobRed>(Vec(250.f, 100.f), module, Pedalboard::DISTORTION_DRIVE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobRed>(Vec(250.f, 160.f), module, Pedalboard::DISTORTION_TONE_PARAM));
        addParam(createParamCentered<WorkshopSmallKnobRed>(Vec(250.f, 220.f), module, Pedalboard::DISTORTION_LEVEL_PARAM));
        addParam(createParamCentered<TransparentSwitch>(Vec(250.f, 321.f), module, Pedalboard::DISTORTION_ACTIVE_PARAM));
        addChild(createLightCentered<MediumLight<RedLight>>(Vec(250.f, 270.f), module, Pedalboard::DISTORTION_ACTIVE_LIGHT));
    }

    void draw(const DrawArgs& args) override {
        // Render panel SVG first
        ModuleWidget::draw(args);
        
        // Render text on top
        std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
        if (!font) {
            font = APP->window->loadFont(asset::system("res/fonts/Roboto-Bold.ttf"));
        }
        
        nvgSave(args.vg);
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
        }
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        
        // ── 2. Jack labels
        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, nvgRGBA(170, 170, 170, 255));
        nvgText(args.vg, 15.f, 140.f, "OUT", NULL);
        nvgText(args.vg, 285.f, 140.f, "IN", NULL);
        
        // ── 3. Pedal Name & Knob Labels
        // Pedal 1: REVERB (center 50)
        nvgFontSize(args.vg, 8.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
        nvgText(args.vg, 50.f, 55.f, "REVERB", NULL);
        
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, nvgRGBA(170, 170, 170, 255));
        nvgText(args.vg, 50.f, 140.f, "SIZE", NULL);
        nvgText(args.vg, 50.f, 220.f, "MIX", NULL);
        
        // Pedal 2: DELAY (center 100)
        nvgFontSize(args.vg, 8.5f);
        nvgFillColor(args.vg, nvgRGBA(51, 51, 51, 255)); // dark text on light background
        nvgText(args.vg, 100.f, 55.f, "DELAY", NULL);
        
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, nvgRGBA(85, 85, 85, 255));
        nvgText(args.vg, 100.f, 125.f, "TIME", NULL);
        nvgText(args.vg, 100.f, 180.f, "F.BACK", NULL);
        nvgText(args.vg, 100.f, 240.f, "MIX", NULL);
        
        // Pedal 3: CHORUS (center 150)
        nvgFontSize(args.vg, 8.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
        nvgText(args.vg, 150.f, 55.f, "CHORUS", NULL);
        
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, nvgRGBA(204, 204, 204, 255));
        nvgText(args.vg, 150.f, 125.f, "RATE", NULL);
        nvgText(args.vg, 150.f, 180.f, "DEPTH", NULL);
        nvgText(args.vg, 150.f, 240.f, "MIX", NULL);
        
        // Pedal 4: PHASER (center 200)
        nvgFontSize(args.vg, 8.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
        nvgText(args.vg, 200.f, 55.f, "PHASER", NULL);
        
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, nvgRGBA(238, 238, 238, 255));
        nvgText(args.vg, 200.f, 125.f, "RATE", NULL);
        nvgText(args.vg, 200.f, 180.f, "DEPTH", NULL);
        nvgText(args.vg, 200.f, 240.f, "MIX", NULL);
        
        // Pedal 5: DISTORTION (center 250)
        nvgFontSize(args.vg, 7.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
        nvgText(args.vg, 250.f, 55.f, "DISTORT", NULL);
        
        nvgFontSize(args.vg, 5.5f);
        nvgFillColor(args.vg, nvgRGBA(255, 204, 204, 255));
        nvgText(args.vg, 250.f, 125.f, "DRIVE", NULL);
        nvgText(args.vg, 250.f, 180.f, "TONE", NULL);
        nvgText(args.vg, 250.f, 240.f, "LEVEL", NULL);
        
        // ── 4. Footer
        nvgFontSize(args.vg, 6.0f);
        nvgFillColor(args.vg, nvgRGBA(60, 60, 60, 255));
        nvgText(args.vg, 150.f, 372.f, "VCV COMPATIBLE", NULL);
        
        nvgRestore(args.vg);
    }

    void appendContextMenu(Menu* menu) override {
        Pedalboard* mod = dynamic_cast<Pedalboard*>(this->module);
        if (!mod) return;

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
                mod->pedalOrder[0] = Pedalboard::PEDAL_DISTORTION;
                mod->pedalOrder[1] = Pedalboard::PEDAL_PHASER;
                mod->pedalOrder[2] = Pedalboard::PEDAL_CHORUS;
                mod->pedalOrder[3] = Pedalboard::PEDAL_DELAY;
                mod->pedalOrder[4] = Pedalboard::PEDAL_REVERB;
            }
        ));
    }
};

Model* modelPedalboard = createModel<Pedalboard, WorkshopPedalboardWidget>("Pedalboard");
