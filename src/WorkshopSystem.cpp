#include "dsp/Amplifier.hpp"
#include "dsp/HumpbackFilter.hpp"
#include "dsp/Mixer.hpp"
#include "dsp/RingMod.hpp"
#include "dsp/Slopes.hpp"
#include "dsp/Stomp.hpp"
#include "dsp/VCO.hpp"
#include "dsp/Voltages.hpp"
#include "plugin_local.hpp"
#include "shared/ComputerWidgets.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <dlfcn.h>
#include <unistd.h>
#else
#include <direct.h>
#include <windows.h>
#endif
#include "ComputerCard.h"
#include "cards/CardRegistry.hpp"
#include "piezo_samples.h"

#include "monome_bridge.hpp"
#include <app/MidiDisplay.hpp>
#include <app/AudioDisplay.hpp>
#include <osdialog.h>

// Monome grid translation helpers
void translate_midi_to_monome_grid(const uint8_t* msg_bytes, size_t msg_size);

struct MonomeSerialParser {
    std::vector<uint8_t> buf;
    int expected_len = 0;
    uint8_t grid_leds[16][16] = {};
    bool grid_dirty[4] = {};

    Grid* target_grid = nullptr;

    void parse_byte(uint8_t b, std::vector<rack::midi::Message>& out_messages) {
        if (expected_len == 0) {
            buf.clear();
            buf.push_back(b);
            expected_len = get_cmd_len(b);
            if (expected_len == 1) {
                process_packet(buf, out_messages);
                expected_len = 0;
            }
        } else {
            buf.push_back(b);
            if (buf.size() >= (size_t)expected_len) {
                process_packet(buf, out_messages);
                expected_len = 0;
            }
        }
    }

    int get_cmd_len(uint8_t header) {
        uint8_t addr = header >> 4;
        uint8_t cmd  = header & 0x0F;

        if (addr == 0x1) {
            switch (cmd) {
                case 0x0: return 4;  // set single LED
                case 0x1: return 2;  // set all
                case 0x2: return 1;  // clear all
                case 0x3: return 4;  // map 8x8 binary
                case 0x4: return 11; // map 8x8 binary rows
                case 0x5: return 67; // map 8x8 intensity
                case 0x6: return 11; // row intensity
                case 0x7: return 2;  // brightness
                case 0x8: return 11; // col intensity
                case 0x9: return 11; // row map intensity
                case 0xA: return 35; // 8x8 block intensity map
                case 0xB: return 11; // col map intensity
                default: return 1;
            }
        }
        
        if ((header & 0xF0) == 0x80) return 9; // series quadrant map
        if ((header & 0xF0) == 0x70) return 2; // 40h row map
        if ((header & 0xF0) == 0x90) return 2; // 40h col map
        if ((header & 0xF0) == 0xA0) return 1; // series brightness
        if ((header & 0xF0) == 0x20) return 1; // 40h brightness
        
        return 1;
    }

    void process_packet(const std::vector<uint8_t>& packet, std::vector<rack::midi::Message>& out_messages) {
        if (packet.empty()) return;
        uint8_t header = packet[0];
        uint8_t addr = header >> 4;
        uint8_t cmd = header & 0x0F;

        if (addr == 0x1) {
            if (cmd == 0x2) { // clear all
                if (target_grid) target_grid->clearAll();
                for (int y = 0; y < 16; y++)
                    for (int x = 0; x < 16; x++) {
                        grid_leds[y][x] = 0;
                    }
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        send_led_midi(x, y, 0, out_messages);
            }
            else if (cmd == 0x0 && packet.size() >= 4) { // single LED set
                uint8_t x = packet[1], y = packet[2], level = packet[3];
                if (target_grid) {
                    if (x < 16 && y < 16) grid_leds[y][x] = level;
                    int xo = (x >= 8) ? 8 : 0;
                    int yo = (y >= 8) ? 8 : 0;
                    uint8_t quad[64];
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c++)
                            quad[r * 8 + c] = grid_leds[yo + r][xo + c];
                    target_grid->updateQuadrant(xo, yo, quad);
                }
                send_led_midi(x, y, level, out_messages);
            }
            else if (cmd == 0x1 && packet.size() >= 2) { // set all
                uint8_t level = packet[1];
                if (target_grid) {
                    for (int y = 0; y < 16; y++)
                        for (int x = 0; x < 16; x++)
                            grid_leds[y][x] = level;
                    for (int q = 0; q < 4; q++) {
                        int xo = (q & 1) ? 8 : 0;
                        int yo = (q & 2) ? 8 : 0;
                        uint8_t quad[64];
                        for (int r = 0; r < 8; r++)
                            for (int c = 0; c < 8; c++)
                                quad[r * 8 + c] = level;
                        target_grid->updateQuadrant(xo, yo, quad);
                    }
                } else {
                    for (int y = 0; y < 16; y++)
                        for (int x = 0; x < 16; x++)
                            grid_leds[y][x] = level;
                }
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x++)
                        send_led_midi(x, y, level, out_messages);
            }
            else if (cmd == 0x4 && packet.size() >= 11) { // map 8x8 binary rows
                uint8_t xo = packet[1];
                uint8_t yo = packet[2];
                if (target_grid) {
                    uint8_t quad[64] = {};
                    for (int r = 0; r < 8; r++) {
                        uint8_t mask = packet[3 + r];
                        for (int c = 0; c < 8; c++) {
                            uint8_t level = (mask & (1 << c)) ? 15 : 0;
                            quad[r * 8 + c] = level;
                            if (xo < 16 && yo + r < 16) grid_leds[yo + r][xo + c] = level;
                        }
                    }
                    target_grid->updateQuadrant(xo, yo, quad);
                }
                for (int r = 0; r < 8; r++) {
                    uint8_t mask = packet[3 + r];
                    for (int c = 0; c < 8; c++)
                        send_led_midi(xo + c, yo + r, (mask & (1 << c)) ? 15 : 0, out_messages);
                }
            }
            else if (cmd == 0xA && packet.size() >= 35) { // 8x8 block varibright (MEXT)
                uint8_t xo = packet[1];
                uint8_t yo = packet[2];
                if (target_grid) {
                    uint8_t quad[64];
                    int p = 3;
                    for (int r = 0; r < 8; r++) {
                        for (int c = 0; c < 8; c += 2) {
                            uint8_t val = packet[p++];
                            quad[r * 8 + c]     = val >> 4;
                            quad[r * 8 + c + 1] = val & 0x0F;
                            if (xo + c     < 16 && yo + r < 16) grid_leds[yo + r][xo + c]     = val >> 4;
                            if (xo + c + 1 < 16 && yo + r < 16) grid_leds[yo + r][xo + c + 1] = val & 0x0F;
                        }
                    }
                    target_grid->updateQuadrant(xo, yo, quad);
                } else {
                    int p = 3;
                    for (int r = 0; r < 8; r++)
                        for (int c = 0; c < 8; c += 2) {
                            uint8_t val = packet[p++];
                            send_led_midi(xo + c, yo + r, val >> 4, out_messages);
                            send_led_midi(xo + c + 1, yo + r, val & 0x0F, out_messages);
                        }
                }
            }
        }
        else if ((header & 0xF0) == 0x80 && packet.size() >= 9) {
            uint8_t q = header & 0x03;
            uint8_t xo = (q & 1) ? 8 : 0;
            uint8_t yo = (q & 2) ? 8 : 0;
            if (target_grid) {
                uint8_t quad[64];
                for (int r = 0; r < 8; r++) {
                    uint8_t mask = packet[1 + r];
                    for (int c = 0; c < 8; c++) {
                        uint8_t level = (mask & (1 << c)) ? 15 : 0;
                        quad[r * 8 + c] = level;
                        grid_leds[yo + r][xo + c] = level;
                    }
                }
                target_grid->updateQuadrant(xo, yo, quad);
            }
            for (int r = 0; r < 8; r++) {
                uint8_t mask = packet[1 + r];
                for (int c = 0; c < 8; c++)
                    send_led_midi(xo + c, yo + r, (mask & (1 << c)) ? 15 : 0, out_messages);
            }
        }
        else if ((header & 0xF0) == 0x70 && packet.size() >= 2) {
            uint8_t y = header & 0x07;
            uint8_t mask = packet[1];
            if (target_grid) {
                target_grid->updateRow(0, y, mask);
                for (int x = 0; x < 8; x++)
                    grid_leds[y][x] = (mask & (1 << x)) ? 15 : 0;
            }
            for (int x = 0; x < 8; x++)
                send_led_midi(x, y, (mask & (1 << x)) ? 15 : 0, out_messages);
        }
    }

    void send_led_midi(int x, int y, uint8_t level, std::vector<rack::midi::Message>& out_messages) {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) return;

        if (grid_leds[y][x] != level) {
            grid_leds[y][x] = level;
            int q = ((y >= 8) << 1) | (x >= 8);
            grid_dirty[q] = true;
        }

        uint8_t vel = level * 8;
        if (vel > 127) vel = 127;

        uint8_t lp_note = (8 - y) * 10 + x + 1;
        push_note_message(lp_note, vel, out_messages);

        uint8_t drum_note = 36 + y * 8 + x;
        push_note_message(drum_note, vel, out_messages);

        uint8_t gen_note = y * 8 + x;
        push_note_message(gen_note, vel, out_messages);
    }

    void push_note_message(uint8_t note, uint8_t vel, std::vector<rack::midi::Message>& out_messages) {
        rack::midi::Message msg;
        msg.bytes.resize(3);
        if (vel > 0) {
            msg.bytes[0] = 0x90; // Note On
            msg.bytes[1] = note;
            msg.bytes[2] = vel;
        } else {
            msg.bytes[0] = 0x80; // Note Off
            msg.bytes[1] = note;
            msg.bytes[2] = 0;
        }
        out_messages.push_back(msg);
    }
};

void translate_midi_to_monome_grid(const uint8_t* msg_bytes, size_t msg_size) {
    if (t_instance && t_instance->sample_mgr_active) return;
    if (msg_size < 3) return;
    uint8_t status = msg_bytes[0];
    uint8_t type = status & 0xF0;
    uint8_t note = msg_bytes[1];
    uint8_t vel = msg_bytes[2];

    bool is_note_on = (type == 0x90 && vel > 0);
    bool is_note_off = (type == 0x80 || (type == 0x90 && vel == 0));

    if (!is_note_on && !is_note_off) return;

    int x = -1, y = -1;

    if (note >= 11 && note <= 88 && (note % 10) >= 1 && (note % 10) <= 8) {
        y = 8 - (note / 10);
        x = (note % 10) - 1;
    }
    else if (note >= 36 && note <= 99) {
        y = (note - 36) / 8;
        x = (note - 36) % 8;
    }
    else if (note >= 0 && note <= 63) {
        y = note / 8;
        x = note % 8;
    }

    if (x >= 0 && x < 16 && y >= 0 && y < 16) {
        uint8_t mext_pkt[3];
        mext_pkt[0] = is_note_on ? 0x21 : 0x20;
        mext_pkt[1] = (uint8_t)x;
        mext_pkt[2] = (uint8_t)y;
        g_serial_rx_byte_queue.push(mext_pkt, 3);
    }
}


thread_local CardGlobals *t_instance = nullptr;
thread_local ComputerCard *g_current_card_ptr = nullptr;
thread_local bool is_core1_thread = false;
thread_local ComputerCard *ComputerCard::thisptr = nullptr;

#include "WebServer.hpp"

// Web Server globals for WorkshopSystem
std::mutex g_instances_mutex;
std::set<void*> g_instances;
int g_web_server_port = 0;
std::atomic<bool> g_web_server_running{false};
socket_t g_server_fd = INVALID_SOCKET;
std::thread g_server_thread;

static std::string hex_encode(const uint8_t* data, size_t size) {
  std::string s;
  s.reserve(size * 2);
  static const char hex_chars[] = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i) {
    s.push_back(hex_chars[data[i] >> 4]);
    s.push_back(hex_chars[data[i] & 0xf]);
  }
  return s;
}

static void hex_decode(const std::string& s, uint8_t* data, size_t max_size) {
  size_t len = s.length();
  size_t limit = std::min(len / 2, max_size);
  for (size_t i = 0; i < limit; ++i) {
    char c1 = s[i * 2];
    char c2 = s[i * 2 + 1];
    uint8_t b = 0;
    if (c1 >= '0' && c1 <= '9') b |= (c1 - '0') << 4;
    else if (c1 >= 'a' && c1 <= 'f') b |= (c1 - 'a' + 10) << 4;
    else if (c1 >= 'A' && c1 <= 'F') b |= (c1 - 'A' + 10) << 4;
    if (c2 >= '0' && c2 <= '9') b |= (c2 - '0');
    else if (c2 >= 'a' && c2 <= 'f') b |= (c2 - 'a' + 10);
    else if (c2 >= 'A' && c2 <= 'F') b |= (c2 - 'A' + 10);
    data[i] = b;
  }
}

void host_save_flash_to_disk() {
  CardGlobals *inst = t_instance;
  if (!inst)
    return;
  std::string filename = "/Users/vmaurer/Music/Workshop_Computer_VCV/flash_" +
                         g_active_card_id + ".bin";
  FILE *f = fopen(filename.c_str(), "wb");
  if (f) {
    fwrite(g_flash_memory, 1, PICO_FLASH_SIZE_BYTES, f);
    fclose(f);
  }
}

void host_multicore_launch_core1(void (*entry)()) {
  CardGlobals *inst = t_instance;
  if (!inst)
    return;
  ComputerCard *card = inst->card_ptr;

  if (inst->g_core1_thread_val.joinable())
    inst->g_core1_thread_val.join();

  inst->g_core1_thread_val = std::thread([entry, inst, card]() {
    t_instance = inst;
    is_core1_thread = true;
    g_current_card_ptr = card;
    ComputerCard::thisptr = card;
    if (inst->set_thread_globals_fn) {
      inst->set_thread_globals_fn(inst);
    }
    if (inst->set_core1_thread_fn) {
      inst->set_core1_thread_fn(true);
    }
    try {
      entry();
    } catch (const ThreadExitException &) {
    }
  });
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Card metadata is loaded dynamically from CardRegistry

struct StereoFrame {
  float l;
  float r;
};

struct StereoDeviceInputPort : audio::Port {
  dsp::RingBuffer<StereoFrame, 8192> inputBuffer;

  StereoDeviceInputPort() {
    maxInputs = 2;
    maxOutputs = 0;
  }

  void processBuffer(const float *input, int inputStride, float *output,
                     int outputStride, int frames) override {
    if (!input || frames <= 0)
      return;
    int n = getNumInputs();
    for (int i = 0; i < frames; ++i) {
      float l = (n >= 1) ? input[i * inputStride + 0] : 0.0f;
      float r = (n >= 2) ? input[i * inputStride + 1] : 0.0f;
      if (!inputBuffer.full()) {
        inputBuffer.push({l, r});
      }
    }
  }

  void onStartStream() override { inputBuffer.clear(); }
  void onStopStream() override { inputBuffer.clear(); }
};

struct StereoDeviceOutputPort : audio::Port {
  dsp::RingBuffer<StereoFrame, 8192> outputBuffer;

  StereoDeviceOutputPort() {
    maxInputs = 0;
    maxOutputs = 2;
  }

  void processBuffer(const float *input, int inputStride, float *output,
                     int outputStride, int frames) override {
    if (!output || frames <= 0)
      return;
    int n = getNumOutputs();
    for (int i = 0; i < frames; ++i) {
      StereoFrame frame = {0.0f, 0.0f};
      if (!outputBuffer.empty()) {
        frame = outputBuffer.shift();
      }
      if (n >= 1)
        output[i * outputStride + 0] = frame.l;
      if (n >= 2)
        output[i * outputStride + 1] = frame.r;
    }
  }

  void onStartStream() override { outputBuffer.clear(); }
  void onStopStream() override { outputBuffer.clear(); }
};

struct BiquadFilter {
  float b0 = 0.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
  float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;

  void setLowpass(float freq, float q, float sr) {
    if (sr < 1.0f)
      sr = 48000.f;
    float w0 = 2.f * M_PI * freq / sr;
    float alpha = std::sin(w0) / (2.f * q);
    float cosw0 = std::cos(w0);
    float a0 = 1.f + alpha;
    b0 = (1.f - cosw0) / 2.f / a0;
    b1 = (1.f - cosw0) / a0;
    b2 = (1.f - cosw0) / 2.f / a0;
    a1 = -2.f * cosw0 / a0;
    a2 = (1.f - alpha) / a0;
  }

  void setBandpass(float freq, float q, float sr) {
    if (sr < 1.0f)
      sr = 48000.f;
    float w0 = 2.f * M_PI * freq / sr;
    float alpha = std::sin(w0) / (2.f * std::max(0.001f, q));
    float cosw0 = std::cos(w0);
    float a0 = 1.f + alpha;
    b0 = alpha / a0;
    b1 = 0.f;
    b2 = -alpha / a0;
    a1 = -2.f * cosw0 / a0;
    a2 = (1.f - alpha) / a0;
  }

  float process(float in) {
    float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = in;
    y2 = y1;
    y1 = out;
    return out;
  }
};

// --- Contact Mic Player Structures ---

struct SampleGroup {
  const float* data[5];
  int size[5];
  int count = 5;
};

struct TransientVoice {
  const float* data = nullptr;
  int size = 0;
  float playhead = 0.0f;
  float rate = 1.0f;
  float volume = 0.0f;
  bool active = false;
};

class TransientPlayer {
private:
  struct TriggerRequest {
    const SampleGroup* group = nullptr;
    float volume = 0.0f;
    float pitchRange = 0.15f;
  };

  static constexpr int QUEUE_SIZE = 16;
  TriggerRequest queue[QUEUE_SIZE];
  std::atomic<int> queueWrite{0};
  std::atomic<int> queueRead{0};

  static constexpr int MAX_VOICES = 8;
  TransientVoice voices[MAX_VOICES];
  int nextVoice = 0;

public:
  void trigger(const SampleGroup& group, float volume, float pitchRange = 0.15f) {
    int w = queueWrite.load(std::memory_order_relaxed);
    int r = queueRead.load(std::memory_order_acquire);
    int nextW = (w + 1) % QUEUE_SIZE;
    if (nextW == r) {
      return; // Queue full
    }
    queue[w].group = &group;
    queue[w].volume = volume;
    queue[w].pitchRange = pitchRange;
    queueWrite.store(nextW, std::memory_order_release);
  }

  void serviceQueue() {
    int r = queueRead.load(std::memory_order_relaxed);
    int w = queueWrite.load(std::memory_order_acquire);
    while (r != w) {
      const TriggerRequest& req = queue[r];
      if (req.group && req.group->count > 0) {
        int varIdx = std::rand() % req.group->count;
        const float* sampleData = req.group->data[varIdx];
        int sampleSize = req.group->size[varIdx];

        if (sampleData && sampleSize > 0) {
          int voiceIdx = nextVoice;
          nextVoice = (nextVoice + 1) % MAX_VOICES;

          float pitchOffset = (((float)std::rand() / RAND_MAX) * 2.0f - 1.0f) * req.pitchRange;
          float rate = 1.0f + pitchOffset;

          voices[voiceIdx].data = sampleData;
          voices[voiceIdx].size = sampleSize;
          voices[voiceIdx].playhead = 0.0f;
          voices[voiceIdx].rate = rate;
          voices[voiceIdx].volume = req.volume;
          voices[voiceIdx].active = true;
        }
      }
      r = (r + 1) % QUEUE_SIZE;
    }
    queueRead.store(r, std::memory_order_release);
  }

  float process(float sampleRate) {
    serviceQueue();

    float baseRate = 44100.0f / sampleRate;
    float out = 0.0f;
    for (int i = 0; i < MAX_VOICES; i++) {
      if (!voices[i].active) continue;

      float ph = voices[i].playhead;
      int idx1 = (int)ph;
      int idx2 = idx1 + 1;

      if (idx1 >= voices[i].size - 1) {
        voices[i].active = false;
        continue;
      }

      float frac = ph - idx1;
      float s1 = voices[i].data[idx1];
      float s2 = (idx2 < voices[i].size) ? voices[i].data[idx2] : 0.0f;
      float interp = s1 + frac * (s2 - s1);

      out += interp * voices[i].volume;
      voices[i].playhead += voices[i].rate * baseRate;
    }
    return out;
  }

  void stopAll() {
    for (int i = 0; i < MAX_VOICES; i++) {
      voices[i].active = false;
    }
    queueRead.store(queueWrite.load());
  }
};

class GranularLoopPlayer {
private:
  const float* loopData = nullptr;
  int loopSize = 0;

  // Playheads
  float posOld = 0.0f;
  float rateOld = 1.0f;
  float posNew = 0.0f;
  float rateNew = 1.0f;

  // Crossfade state
  bool isCrossfading = false;
  int xfadeCounter = 0;
  int xfadeDuration = 2000;
  int grainTimer = 0;
  int grainDuration = 8000;

  // Volume smoothing
  float currentVolume = 0.0f;
  float targetVolume = 0.0f;
  float volumeSlew = 0.002f;

public:
  void setLoop(const float* data, int size) {
    loopData = data;
    loopSize = size;
    resetPlayheads();
  }

  void resetPlayheads() {
    if (loopSize > 20000) {
      posNew = std::rand() % (loopSize - 20000);
    } else {
      posNew = 0.0f;
    }
    rateNew = 1.0f;
    posOld = 0.0f;
    rateOld = 1.0f;
    isCrossfading = false;
    xfadeCounter = 0;
    grainTimer = 0;
    grainDuration = 6000 + (std::rand() % 8000);
  }

  void setTargetVolume(float vol) {
    targetVolume = vol;
  }

  float process(float sampleRate) {
    if (!loopData || loopSize <= 0) return 0.0f;

    currentVolume += (targetVolume - currentVolume) * volumeSlew;
    if (currentVolume < 1e-4f && targetVolume < 1e-4f) {
      currentVolume = 0.0f;
      return 0.0f;
    }

    float baseRate = 44100.0f / sampleRate;

    grainTimer++;
    if (grainTimer >= grainDuration && !isCrossfading) {
      isCrossfading = true;
      xfadeCounter = 0;
      posOld = posNew;
      rateOld = rateNew;

      if (loopSize > 30000) {
        posNew = std::rand() % (loopSize - 30000);
      } else {
        posNew = 0.0f;
      }
      float rateOffset = (((float)std::rand() / RAND_MAX) * 2.0f - 1.0f) * 0.10f;
      rateNew = 1.0f + rateOffset;

      grainDuration = 6000 + (std::rand() % 9000);
      grainTimer = 0;
    }

    float sample = 0.0f;

    auto getSampleInterp = [this](float pos) -> float {
      int idx1 = (int)pos;
      int idx2 = idx1 + 1;
      if (idx1 >= loopSize - 1) return 0.0f;
      float frac = pos - idx1;
      float s1 = loopData[idx1];
      float s2 = (idx2 < loopSize) ? loopData[idx2] : 0.0f;
      return s1 + frac * (s2 - s1);
    };

    if (isCrossfading) {
      float sOld = getSampleInterp(posOld);
      float sNew = getSampleInterp(posNew);

      float x = (float)xfadeCounter / xfadeDuration;
      float fadeNew = std::sin(x * M_PI / 2.0f);
      float fadeOld = std::cos(x * M_PI / 2.0f);

      sample = sOld * fadeOld + sNew * fadeNew;

      posOld += rateOld * baseRate;
      posNew += rateNew * baseRate;

      xfadeCounter++;
      if (xfadeCounter >= xfadeDuration) {
        isCrossfading = false;
        xfadeCounter = 0;
      }
    } else {
      sample = getSampleInterp(posNew);
      posNew += rateNew * baseRate;
    }

    if (posNew >= loopSize - 1) {
      posNew = 0.0f;
    }
    if (posOld >= loopSize - 1) {
      posOld = 0.0f;
    }

    return sample * currentVolume;
  }
};

static const SampleGroup tapNearGroup = {
  { piezo_tap_near_var1_data, piezo_tap_near_var2_data, piezo_tap_near_var3_data, piezo_tap_near_var4_data, piezo_tap_near_var5_data },
  { piezo_tap_near_var1_size, piezo_tap_near_var2_size, piezo_tap_near_var3_size, piezo_tap_near_var4_size, piezo_tap_near_var5_size },
  5
};

static const SampleGroup tapMidGroup = {
  { piezo_tap_mid_var1_data, piezo_tap_mid_var2_data, piezo_tap_mid_var3_data, piezo_tap_mid_var4_data, piezo_tap_mid_var5_data },
  { piezo_tap_mid_var1_size, piezo_tap_mid_var2_size, piezo_tap_mid_var3_size, piezo_tap_mid_var4_size, piezo_tap_mid_var5_size },
  5
};

static const SampleGroup tapFarGroup = {
  { piezo_tap_far_var1_data, piezo_tap_far_var2_data, piezo_tap_far_var3_data, piezo_tap_far_var4_data, piezo_tap_far_var5_data },
  { piezo_tap_far_var1_size, piezo_tap_far_var2_size, piezo_tap_far_var3_size, piezo_tap_far_var4_size, piezo_tap_far_var5_size },
  5
};

static const SampleGroup bumpChassisGroup = {
  { piezo_bump_chassis_var1_data, piezo_bump_chassis_var2_data, piezo_bump_chassis_var3_data, piezo_bump_chassis_var4_data, piezo_bump_chassis_var5_data },
  { piezo_bump_chassis_var1_size, piezo_bump_chassis_var2_size, piezo_bump_chassis_var3_size, piezo_bump_chassis_var4_size, piezo_bump_chassis_var5_size },
  5
};

static const SampleGroup latchSwitchGroup = {
  { piezo_latch_switch_var1_data, piezo_latch_switch_var2_data, piezo_latch_switch_var3_data, piezo_latch_switch_var4_data, piezo_latch_switch_var5_data },
  { piezo_latch_switch_var1_size, piezo_latch_switch_var2_size, piezo_latch_switch_var3_size, piezo_latch_switch_var4_size, piezo_latch_switch_var5_size },
  5
};

static const SampleGroup momSwitchDownGroup = {
  { piezo_mom_switch_down_var1_data, piezo_mom_switch_down_var2_data, piezo_mom_switch_down_var3_data, piezo_mom_switch_down_var4_data, piezo_mom_switch_down_var5_data },
  { piezo_mom_switch_down_var1_size, piezo_mom_switch_down_var2_size, piezo_mom_switch_down_var3_size, piezo_mom_switch_down_var4_size, piezo_mom_switch_down_var5_size },
  5
};

static const SampleGroup momSwitchSnapGroup = {
  { piezo_mom_switch_snap_var1_data, piezo_mom_switch_snap_var2_data, piezo_mom_switch_snap_var3_data, piezo_mom_switch_snap_var4_data, piezo_mom_switch_snap_var5_data },
  { piezo_mom_switch_snap_var1_size, piezo_mom_switch_snap_var2_size, piezo_mom_switch_snap_var3_size, piezo_mom_switch_snap_var4_size, piezo_mom_switch_snap_var5_size },
  5
};

static const SampleGroup pressButtonGroup = {
  { piezo_press_button_var1_data, piezo_press_button_var2_data, piezo_press_button_var3_data, piezo_press_button_var4_data, piezo_press_button_var5_data },
  { piezo_press_button_var1_size, piezo_press_button_var2_size, piezo_press_button_var3_size, piezo_press_button_var4_size, piezo_press_button_var5_size },
  5
};

static const SampleGroup plugJackInGroup = {
  { piezo_plug_jack_in_var1_data, piezo_plug_jack_in_var2_data, piezo_plug_jack_in_var3_data, piezo_plug_jack_in_var4_data, piezo_plug_jack_in_var5_data },
  { piezo_plug_jack_in_var1_size, piezo_plug_jack_in_var2_size, piezo_plug_jack_in_var3_size, piezo_plug_jack_in_var4_size, piezo_plug_jack_in_var5_size },
  5
};

static const SampleGroup plugJackOutGroup = {
  { piezo_plug_jack_out_var1_data, piezo_plug_jack_out_var2_data, piezo_plug_jack_out_var3_data, piezo_plug_jack_out_var4_data, piezo_plug_jack_out_var5_data },
  { piezo_plug_jack_out_var1_size, piezo_plug_jack_out_var2_size, piezo_plug_jack_out_var3_size, piezo_plug_jack_out_var4_size, piezo_plug_jack_out_var5_size },
  5
};

static const SampleGroup scratchNearGroup = {
  { piezo_scratch_near_var1_data, piezo_scratch_near_var2_data, piezo_scratch_near_var3_data, piezo_scratch_near_var4_data, piezo_scratch_near_var5_data },
  { piezo_scratch_near_var1_size, piezo_scratch_near_var2_size, piezo_scratch_near_var3_size, piezo_scratch_near_var4_size, piezo_scratch_near_var5_size },
  5
};

static const SampleGroup scratchMidGroup = {
  { piezo_scratch_mid_var1_data, piezo_scratch_mid_var2_data, piezo_scratch_mid_var3_data, piezo_scratch_mid_var4_data, piezo_scratch_mid_var5_data },
  { piezo_scratch_mid_var1_size, piezo_scratch_mid_var2_size, piezo_scratch_mid_var3_size, piezo_scratch_mid_var4_size, piezo_scratch_mid_var5_size },
  5
};

static const SampleGroup scratchFarGroup = {
  { piezo_scratch_far_var1_data, piezo_scratch_far_var2_data, piezo_scratch_far_var3_data, piezo_scratch_far_var4_data, piezo_scratch_far_var5_data },
  { piezo_scratch_far_var1_size, piezo_scratch_far_var2_size, piezo_scratch_far_var3_size, piezo_scratch_far_var4_size, piezo_scratch_far_var5_size },
  5
};

static const SampleGroup scratchGratingGroup = {
  { piezo_scratch_grating_var1_data, piezo_scratch_grating_var2_data, piezo_scratch_grating_var3_data, piezo_scratch_grating_var4_data, piezo_scratch_grating_var5_data },
  { piezo_scratch_grating_var1_size, piezo_scratch_grating_var2_size, piezo_scratch_grating_var3_size, piezo_scratch_grating_var4_size, piezo_scratch_grating_var5_size },
  5
};

static const SampleGroup rotateKnobGroup = {
  { piezo_rotate_knob_var1_data, piezo_rotate_knob_var2_data, piezo_rotate_knob_var3_data, piezo_rotate_knob_var4_data, piezo_rotate_knob_var5_data },
  { piezo_rotate_knob_var1_size, piezo_rotate_knob_var2_size, piezo_rotate_knob_var3_size, piezo_rotate_knob_var4_size, piezo_rotate_knob_var5_size },
  5
};


struct WorkshopSystem : Module, IGridConsumer, IComputerModule {
  enum ParamIds {
    // Knobs
    COMPUTER_X_PARAM,
    COMPUTER_MAIN_PARAM,
    COMPUTER_Y_PARAM,
    OSC1_FINE_PARAM,
    OSC1_FREQ_PARAM,
    OSC1_FM_PARAM,
    OSC2_FINE_PARAM,
    OSC2_FREQ_PARAM,
    OSC2_FM_PARAM,
    SLOPES1_RATE_PARAM,
    SLOPES2_RATE_PARAM,
    FILTER1_FM_PARAM,
    FILTER1_CUTOFF_PARAM,
    FILTER1_RES_PARAM,
    FILTER2_FM_PARAM,
    FILTER2_CUTOFF_PARAM,
    FILTER2_RES_PARAM,
    AMP_GAIN_PARAM,
    VOLT_BLEND_PARAM,
    STOMP_FEEDBACK_PARAM,
    STOMP_BLEND_PARAM,
    MIX_CH1_PARAM,
    MIX_CH2_PARAM,
    MIX_CH3_PARAM,
    MIX_CH4_PARAM,
    MIX_PAN1_PARAM,
    MIX_PAN2_PARAM,
    MIX_MAIN_PARAM,

    // Switches
    COMPUTER_SWITCH_PARAM,
    AMP_SWITCH_PARAM,
    FILTER1_SWITCH_PARAM,
    FILTER2_SWITCH_PARAM,
    SLOPES1_SHAPE_PARAM,
    SLOPES2_SHAPE_PARAM,
    SLOPES1_MODE_PARAM,
    SLOPES2_MODE_PARAM,

    // Buttons
    VOLT_BTN1_PARAM,
    VOLT_BTN2_PARAM,
    VOLT_BTN3_PARAM,
    VOLT_BTN4_PARAM,

    NUM_PARAMS
  };

  enum InputIds {
    COMPUTER_AUDIO1_IN,
    COMPUTER_CV1_IN,
    COMPUTER_CV2_IN,
    COMPUTER_PULSE1_IN,
    COMPUTER_AUDIO2_IN,
    COMPUTER_PULSE2_IN,
    OSC1_PITCH_IN,
    OSC2_PITCH_IN,
    OSC1_FM_IN,
    OSC2_FM_IN,
    STEREO_IN_JACK, // Polyphonic jack (ch1 = Left, ch2 = Right)
    RING_IN1,
    RING_IN2,
    STOMP_IN,
    STOMP_RETURN,
    AMP_IN,
    FILTER1_IN,
    FILTER2_IN,
    FILTER1_FM_IN,
    FILTER2_FM_IN,
    SLOPES1_IN,
    SLOPES1_CV_IN,
    SLOPES2_IN,
    SLOPES2_CV_IN,
    MIXER1_IN,
    MIXER2_IN,
    MIXER3_IN,
    MIXER4_IN,

    NUM_INPUTS
  };

  enum OutputIds {
    COMPUTER_AUDIO1_OUT,
    COMPUTER_CV1_OUT,
    COMPUTER_PULSE1_OUT,
    COMPUTER_AUDIO2_OUT,
    COMPUTER_CV2_OUT,
    COMPUTER_PULSE2_OUT,
    OSC1_SQR_OUT,
    OSC2_SQR_OUT,
    OSC1_SIN_OUT,
    OSC2_SIN_OUT,
    STEREO_L_OUT,
    STEREO_R_OUT,
    RING_OUT,
    STOMP_OUT,
    STOMP_SEND,
    AMP_OUT,
    VOLT1_OUT,
    VOLT2_OUT,
    VOLT3_OUT,
    VOLT4_OUT,
    FILTER1_HP_OUT,
    FILTER2_HP_OUT,
    FILTER1_LP_OUT,
    FILTER2_LP_OUT,
    SLOPES1_OUT,
    SLOPES2_OUT,
    MIXER_L_OUT,
    MIXER_R_OUT,
    PHONES1_OUT,
    PHONES2_OUT,

    NUM_OUTPUTS
  };

  enum LightIds {
    AMP_LED1,
    AMP_LED2,
    AMP_LED3,
    AMP_LED4,

    COMP_LED0,
    COMP_LED1,
    COMP_LED2,
    COMP_LED3,
    COMP_LED4,
    COMP_LED5,

    SLOPES1_RISE_LED,
    SLOPES1_FALL_LED,
    SLOPES2_RISE_LED,
    SLOPES2_FALL_LED,

    VOLT_BTN1_LED,
    VOLT_BTN2_LED,
    VOLT_BTN3_LED,
    VOLT_BTN4_LED,

    NUM_LIGHTS
  };

  // DSP Sections
  VCO vco1;
  VCO vco2;
  HumpbackFilter filter1;
  HumpbackFilter filter2;
  Slopes slopes1;
  Slopes slopes2;
  Amplifier amp;
  Stomp stomp;
  Mixer mixer;

  // Voltage button latch states (managed by VoltButton widget via
  // handleVoltButton()) Button 1 is active by default (at least one button
  // always active in radio mode)
  bool voltBtnStates[4] = {true, false, false, false};

  // Normalization cache: sine outputs stored in Volts for next-sample FM
  // normalization
  float osc1SinVolts = 0.0f;
  float osc2SinVolts = 0.0f;

  // Stompbox normalization cache (1-sample feedback delay)
  float stompLastSend = 0.0f;


  // Self patch flags set by widget
  bool isOsc1SelfPatched = false;
  bool isOsc2SelfPatched = false;

  // Computer Card state — internal computer enabled by default
  bool internalComputerEnabled = true;
  int activeCardIdx = -1;
  float internalCompPhase = 0.0f;

  bool lastInputConnected[NUM_INPUTS] = {};
  bool lastOutputConnected[NUM_OUTPUTS] = {};

  CardGlobals card_globals;
  void *card_lib_handle = nullptr;
  std::string loaded_temp_path = "";
  std::thread background_thread;
  int utility_indices[2] = {0, 0};

  int pending_page_direction = 0;
  void change_card(int idx) override { change_card_impl(idx, true); }
  int get_active_card_idx() const override { return activeCardIdx; }
  std::string get_active_card_id() const override { return card_globals.active_card_id_str; }
  int get_utility_index(int slot) const override { return utility_indices[slot & 1]; }
  void set_pending_page_direction(int dir) override { pending_page_direction = dir; }

  // WebSocket MIDI and Serial queues for Web UI
  ThreadSafeMessageQueue websocket_midi_tx_queue;
  ThreadSafeMessageQueue websocket_serial_tx_queue;
  MidiTxParser txParser;

  // MIDI input/output and Monome Grid connectivity
  rack::midi::InputQueue midiInput;
  rack::midi::Output midiOutput;
  MonomeSerialParser serialParser;
  Grid* connected_grid = nullptr;
  std::string last_grid_device_id = "";
  bool consumer_registered = false;

  void ensure_monome_registered() {
    if (!consumer_registered && MonomeBridge::get().is_available()) {
      MonomeBridge::get().register_consumer(this);
      consumer_registered = true;
    }
  }

  void gridConnected(Grid* grid) override {
    connected_grid = grid;
    if (connected_grid) {
      card_globals.grid_connected_flag = true;
      if (card_globals.on_grid_connected_fn) {
        card_globals.on_grid_connected_fn(true);
      }

      if (card_globals.sample_mgr_active) {
        last_grid_device_id = connected_grid->getDevice().id;
        return;
      }

      g_serial_rx_byte_queue.clear();

      const MonomeDevice& dev = connected_grid->getDevice();
      uint8_t grid_cols = (dev.width  > 0 && dev.width  <= 16) ? (uint8_t)dev.width  : 16;
      uint8_t grid_rows = (dev.height > 0 && dev.height <= 16) ? (uint8_t)dev.height :  8;

      uint8_t size_resp[3] = { 0x03, grid_cols, grid_rows };
      g_serial_rx_byte_queue.push(size_resp, 3);

      for (int q = 0; q < 4; q++) {
        int xo = (q & 1) ? 8 : 0;
        int yo = (q & 2) ? 8 : 0;
        uint8_t quad_leds[64];
        for (int r = 0; r < 8; r++) {
          for (int c = 0; c < 8; c++) {
            quad_leds[r * 8 + c] = serialParser.grid_leds[yo + r][xo + c];
          }
        }
        connected_grid->updateQuadrant(xo, yo, quad_leds);
      }
      last_grid_device_id = connected_grid->getDevice().id;
    }
  }

  void gridDisconnected(bool ownerChanged) override {
    card_globals.grid_connected_flag = false;
    card_globals.sample_mgr_active = false;
    if (card_globals.on_grid_connected_fn) {
      card_globals.on_grid_connected_fn(false);
    }
    connected_grid = nullptr;
  }

  std::string gridGetCurrentDeviceId() override {
    if (connected_grid) {
      return connected_grid->getDevice().id;
    }
    return "";
  }

  std::string gridGetLastDeviceId(bool owned) override {
    return last_grid_device_id;
  }

  void setLastDeviceId(std::string id) override {
    last_grid_device_id = id;
  }

  void gridButtonEvent(int x, int y, bool state) override {
    if (card_globals.sample_mgr_active) {
      return;
    }
    if (x >= 0 && x < 16 && y >= 0 && y < 16) {
      uint8_t mext_pkt[3];
      mext_pkt[0] = state ? 0x21 : 0x20;
      mext_pkt[1] = (uint8_t)x;
      mext_pkt[2] = (uint8_t)y;
      g_serial_rx_byte_queue.push(mext_pkt, 3);
    }
  }

  void encDeltaEvent(int n, int d) override {}

  Grid* gridGetDevice() override {
    return connected_grid;
  }

  bool activeCardNeedsMidi() {
      if (card_globals.active_card_id_str.empty()) return false;
      return (card_globals.active_card_id_str == "simple_midi" || 
              card_globals.active_card_id_str == "duo_midi" || 
              card_globals.active_card_id_str == "usb_audio_bridge" ||
              card_globals.active_card_id_str == "blackbird" ||
              card_globals.active_card_id_str == "krell" ||
              card_globals.active_card_id_str == "rompler" ||
              card_globals.active_card_id_str == "computer_grids" ||
              card_globals.active_card_id_str == "reverb" ||
              card_globals.active_card_id_str == "flux" ||
              card_globals.active_card_id_str == "twists" ||
              card_globals.active_card_id_str == "modes");
  }

  bool activeCardNeedsGrid() {
      if (card_globals.active_card_id_str.empty()) return false;
      return (card_globals.active_card_id_str == "blackbird" || 
              card_globals.active_card_id_str == "krell" || 
              card_globals.active_card_id_str == "duo_midi" || 
              card_globals.active_card_id_str == "mlrws" ||
              card_globals.active_card_id_str == "drumdrum");
  }

  bool activeCardNeedsUsbAudio() {
      if (card_globals.active_card_id_str.empty()) return false;
      return (card_globals.active_card_id_str == "usb_audio_bridge");
  }

  bool load_file_to_flash(const std::string& filepath) {
      FILE* f = fopen(filepath.c_str(), "rb");
      if (!f) return false;

      uint32_t header[2];
      if (fread(header, sizeof(uint32_t), 2, f) != 2) {
          fclose(f);
          return false;
      }
      fseek(f, 0, SEEK_SET);

      bool is_uf2 = (header[0] == 0x0A324655 && header[1] == 0x9E5D5157);

      if (is_uf2) {
          memset(card_globals.g_flash_memory_val, 0xFF, PICO_FLASH_SIZE_BYTES);

          uint8_t block[512];
          while (fread(block, 1, 512, f) == 512) {
              uint32_t magic_start_0 = *(uint32_t*)&block[0];
              uint32_t magic_start_1 = *(uint32_t*)&block[4];
              uint32_t magic_end = *(uint32_t*)&block[508];
              if (magic_start_0 != 0x0A324655 || magic_start_1 != 0x9E5D5157 || magic_end != 0x0AB16F30) {
                  continue;
              }
              uint32_t target_addr = *(uint32_t*)&block[12];
              uint32_t payload_size = *(uint32_t*)&block[16];

              if (target_addr >= 0x10000000 && target_addr < 0x10000000 + PICO_FLASH_SIZE_BYTES) {
                  uint32_t offset = target_addr - 0x10000000;
                  if (offset + payload_size <= PICO_FLASH_SIZE_BYTES) {
                      memcpy(card_globals.g_flash_memory_val + offset, &block[32], payload_size);
                  }
              }
          }
      } else {
          size_t n = fread(card_globals.g_flash_memory_val, 1, PICO_FLASH_SIZE_BYTES, f);
          if (n < PICO_FLASH_SIZE_BYTES) {
              memset(card_globals.g_flash_memory_val + n, 0xFF, PICO_FLASH_SIZE_BYTES - n);
          }
      }

      fclose(f);
      
      CardGlobals* old_instance = t_instance;
      t_instance = &card_globals;
      host_save_flash_to_disk();
      t_instance = old_instance;
      return true;
  }

  rack::dsp::SchmittTrigger pulse1Trigger;
  rack::dsp::SchmittTrigger pulse2Trigger;

  void unload_card_library() {
    if (card_lib_handle) {
#ifdef _WIN32
      FreeLibrary((HMODULE)card_lib_handle);
#else
      dlclose(card_lib_handle);
#endif
      card_lib_handle = nullptr;
    }
    if (!loaded_temp_path.empty()) {
#ifdef _WIN32
      _unlink(loaded_temp_path.c_str());
#else
      unlink(loaded_temp_path.c_str());
#endif
      loaded_temp_path = "";
    }
  }

  void stop_card() {
    card_globals.block_audio_processing.store(true, std::memory_order_release);
    while (card_globals.in_audio_callback.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    t_instance = &card_globals;

    card_globals.card_ptr = nullptr;
    card_globals.g_dsp_ready = false;
    ComputerCard::thisptr = nullptr;
    g_current_card_ptr = nullptr;

    card_globals.set_thread_globals_fn = nullptr;
    card_globals.set_core1_thread_fn = nullptr;
    card_globals.save_flash_to_disk_fn = nullptr;
    card_globals.multicore_launch_core1_fn = nullptr;

    g_cancellation_requested = true;
    try {
      g_fifo_1_to_0.push(0);
    } catch (...) {
    }
    try {
      g_fifo_0_to_1.push(0);
    } catch (...) {
    }
    g_synth_need_render.store(true);
    g_synth_cv.notify_all();

    if (background_thread.joinable()) {
      try {
        background_thread.join();
      } catch (...) {
      }
    }
    if (card_globals.g_core1_thread_val.joinable()) {
      try {
        card_globals.g_core1_thread_val.join();
      } catch (...) {
      }
    }

    g_fifo_1_to_0.clear();
    g_fifo_0_to_1.clear();
    g_midi_rx_packet_queue.clear();
    g_midi_tx_byte_queue.clear();

    for (int i = 0; i < 2; i++) {
      g_audio_out[i] = 0.f;
      g_cv_out[i] = 0.f;
      g_pulse_out[i] = false;
    }
    for (int i = 0; i < 6; i++) {
      g_led_brightness[i] = 0.f;
    }

    g_synth_need_render.store(false);
    g_cancellation_requested = false;

    unload_card_library();
  }

  void save_flash_to_disk() {
    std::string filename = "/Users/vmaurer/Music/Workshop_Computer_VCV/flash_" +
                           g_active_card_id + ".bin";
    FILE *f = fopen(filename.c_str(), "wb");
    if (f) {
      fwrite(g_flash_memory, 1, PICO_FLASH_SIZE_BYTES, f);
      fclose(f);
    }
  }

  void load_flash_from_disk() {
    std::string filename = "/Users/vmaurer/Music/Workshop_Computer_VCV/flash_" +
                           g_active_card_id + ".bin";
    FILE *f = fopen(filename.c_str(), "rb");
    if (f) {
      size_t n = fread(g_flash_memory, 1, PICO_FLASH_SIZE_BYTES, f);
      fclose(f);
      if (n < PICO_FLASH_SIZE_BYTES)
        memset(g_flash_memory + n, 0xFF, PICO_FLASH_SIZE_BYTES - n);
    } else {
      memset(g_flash_memory, 0xFF, PICO_FLASH_SIZE_BYTES);
    }
  }

  void change_card_impl(int new_idx, bool load_flash = true) {
    stop_card();
    t_instance = &card_globals;

    card_globals.grid_connected_flag = (connected_grid != nullptr);
    card_globals.sample_mgr_active = false;
    card_globals.on_grid_connected_fn = nullptr;

    activeCardIdx = new_idx;
    if (activeCardIdx < 0 || activeCardIdx >= (int)g_card_registry.size()) {
      activeCardIdx = -1;
      g_active_card_id = "";
      return;
    }

    g_active_card_id = g_card_registry[activeCardIdx].id;

    INFO("change_card called: activeCardIdx = %d, ID = %s", activeCardIdx,
         g_active_card_id.c_str());

    for (int i = 0; i < 6; i++)
      g_led_brightness[i] = 0.f;

    if (load_flash) {
      load_flash_from_disk();
    }

    if (g_active_card_id == "utility_pair") {
      g_flash_memory[2093056] = utility_indices[0];
      g_flash_memory[2093057] = utility_indices[1];
    }

    g_midi_rx_packet_queue.clear();
    g_midi_tx_byte_queue.clear();
    g_synth_need_render.store(false);
    g_cancellation_requested = false;

    card_globals.save_flash_to_disk_fn = host_save_flash_to_disk;
    card_globals.multicore_launch_core1_fn = host_multicore_launch_core1;

#ifdef _WIN32
    std::string lib_name = "card_" + g_active_card_id + ".dll";
#elif defined(__APPLE__)
    std::string lib_name = "libcard_" + g_active_card_id + ".dylib";
#else
    std::string lib_name = "libcard_" + g_active_card_id + ".so";
#endif

    rack::plugin::Plugin *computerPlugin =
        rack::plugin::getPlugin("MTMWorkshopComputer");
    std::string src_path;
    if (computerPlugin) {
      src_path = rack::asset::plugin(computerPlugin, "res/cards/" + lib_name);
    } else {
      src_path = rack::asset::plugin(pluginInstance, "res/cards/" + lib_name);
    }
    INFO("computerPlugin found = %s, src_path = %s",
         computerPlugin ? "YES" : "NO", src_path.c_str());

    std::string tmp_dir = asset::plugin(pluginInstance, "tmp");
#ifdef _WIN32
    _mkdir(tmp_dir.c_str());
#else
    mkdir(tmp_dir.c_str(), 0777);
#endif

    char temp_name[512];
#ifdef _WIN32
    snprintf(temp_name, sizeof(temp_name), "%s\\card_%s_%p.dll",
             tmp_dir.c_str(), g_active_card_id.c_str(), this);
#elif defined(__APPLE__)
    snprintf(temp_name, sizeof(temp_name), "%s/libcard_%s_%p.dylib",
             tmp_dir.c_str(), g_active_card_id.c_str(), this);
#else
    snprintf(temp_name, sizeof(temp_name), "%s/libcard_%s_%p.so",
             tmp_dir.c_str(), g_active_card_id.c_str(), this);
#endif
    loaded_temp_path = temp_name;
    INFO("loaded_temp_path = %s", loaded_temp_path.c_str());

    {
      std::ifstream src(src_path, std::ios::binary);
      std::ofstream dst(loaded_temp_path, std::ios::binary);
      if (src && dst) {
        dst << src.rdbuf();
        INFO("Dynamic library successfully copied to temp path.");
      } else {
        WARN("Failed to copy dynamic library from %s to %s", src_path.c_str(),
             loaded_temp_path.c_str());
      }
    }

#ifdef _WIN32
    card_lib_handle = LoadLibraryA(loaded_temp_path.c_str());
#else
    card_lib_handle = dlopen(loaded_temp_path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif

    if (!card_lib_handle) {
#ifdef _WIN32
      WARN("Failed to load library: %lu", GetLastError());
#else
      WARN("Failed to load library: %s", dlerror());
#endif
      return;
    }
    INFO("Library dlopen succeeded.");

#ifdef _WIN32
    card_globals.set_thread_globals_fn =
        (void (*)(CardGlobals *))GetProcAddress((HMODULE)card_lib_handle,
                                                "set_thread_globals");
    card_globals.set_core1_thread_fn = (void (*)(bool))GetProcAddress(
        (HMODULE)card_lib_handle, "set_core1_thread");
    auto run_card_fn =
        (void (*)())GetProcAddress((HMODULE)card_lib_handle, "run_card");
#else
    card_globals.set_thread_globals_fn =
        (void (*)(CardGlobals *))dlsym(card_lib_handle, "set_thread_globals");
    card_globals.set_core1_thread_fn =
        (void (*)(bool))dlsym(card_lib_handle, "set_core1_thread");
    auto run_card_fn = (void (*)())dlsym(card_lib_handle, "run_card");
#endif

    if (!card_globals.set_thread_globals_fn || !run_card_fn) {
      WARN("Failed to resolve card symbols in dylib!");
      return;
    }
    INFO("Resolved symbols successfully.");

    if (card_globals.set_thread_globals_fn) {
      card_globals.set_thread_globals_fn(&card_globals);
    }

    CardGlobals *inst = &card_globals;
    background_thread = std::thread([inst, run_card_fn]() {
      t_instance = inst;
      is_core1_thread = false;
      if (inst->set_thread_globals_fn) {
        inst->set_thread_globals_fn(inst);
      }
      if (inst->set_core1_thread_fn) {
        inst->set_core1_thread_fn(false);
      }
      try {
        if (run_card_fn)
          run_card_fn();
      } catch (const ThreadExitException &) {
      }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    card_globals.block_audio_processing.store(false, std::memory_order_release);
  }

  // Contact Mic player instances and state variables
  TransientPlayer transientPlayer;
  GranularLoopPlayer scratchLoopPlayer;
  GranularLoopPlayer knobLoopPlayer;

  std::atomic<int> scratchActiveCounter{0};
  std::atomic<int> knobActiveCounter{0};
  const float* currentScratchLoopData = nullptr;
  float scratchLpState = 0.0f;
  std::atomic<bool> scratchApplyLowpass{false};

  void handlePanelTap(float x, float y) {
    INFO("handlePanelTap: x=%f, y=%f", x, y);
    if (x < 200.f) {
      float vol = (0.7f + ((float)std::rand() / RAND_MAX) * 0.4f) * 0.5f;
      transientPlayer.trigger(tapFarGroup, vol);
    } else if (x < 400.f) {
      float vol = (0.8f + ((float)std::rand() / RAND_MAX) * 0.4f) * 0.8f;
      transientPlayer.trigger(tapMidGroup, vol);
    } else if (y < 250.f) {
      float vol = 0.8f + ((float)std::rand() / RAND_MAX) * 0.4f;
      transientPlayer.trigger(tapNearGroup, vol);
    } else {
      float vol = 0.8f + ((float)std::rand() / RAND_MAX) * 0.4f;
      transientPlayer.trigger(bumpChassisGroup, vol);
    }
  }

  void handlePanelScratch(float x, float y, float dx, float dy) {
    float speed = std::sqrt(dx * dx + dy * dy);
    if (speed < 0.01f) return;

    const float* loopData = nullptr;
    int loopSize = 0;
    float volumeScale = 1.0f;
    bool applyLowpass = false;

    if (x < 200.f) {
      loopData = piezo_scratch_mid_loop_data;
      loopSize = piezo_scratch_mid_loop_size;
      volumeScale = 0.4f;
      applyLowpass = true;
    } else if (x < 400.f) {
      loopData = piezo_scratch_mid_loop_data;
      loopSize = piezo_scratch_mid_loop_size;
      volumeScale = 0.7f;
    } else if (y < 250.f) {
      loopData = piezo_scratch_near_loop_data;
      loopSize = piezo_scratch_near_loop_size;
      volumeScale = 1.2f;
    } else {
      loopData = piezo_scratch_grating_loop_data;
      loopSize = piezo_scratch_grating_loop_size;
      volumeScale = 1.2f;
    }

    if (loopData != currentScratchLoopData) {
      currentScratchLoopData = loopData;
      scratchLoopPlayer.setLoop(loopData, loopSize);
    }

    float targetVol = std::min(1.5f, speed * 0.15f) * volumeScale;
    scratchLoopPlayer.setTargetVolume(targetVol);
    scratchActiveCounter.store(4800, std::memory_order_relaxed);
    scratchApplyLowpass.store(applyLowpass, std::memory_order_relaxed);

    static float dragDistanceAccum = 0.0f;
    dragDistanceAccum += speed;
    if (dragDistanceAccum > 80.f) {
      dragDistanceAccum = 0.0f;
      float transientVol = std::min(1.0f, speed * 0.08f) * volumeScale;
      if (x < 200.f) {
        transientPlayer.trigger(scratchFarGroup, transientVol);
      } else if (x < 400.f) {
        transientPlayer.trigger(scratchMidGroup, transientVol);
      } else if (y < 250.f) {
        transientPlayer.trigger(scratchNearGroup, transientVol);
      } else {
        transientPlayer.trigger(scratchGratingGroup, transientVol);
      }
    }
  }

  StereoDeviceInputPort stereoDeviceInput;

  StereoDeviceOutputPort stereoDeviceOutput;
  float lastParams[NUM_PARAMS] = {};

  float getKnobProximity(int paramId) {
    struct Pos {
      float x;
      float y;
    };
    static const Pos knobPositions[NUM_PARAMS] = {
        {3.25f, 32.50f},  // COMPUTER_X_PARAM
        {9.44f, 15.28f},  // COMPUTER_MAIN_PARAM
        {9.50f, 32.50f},  // COMPUTER_Y_PARAM
        {22.64f, 32.50f}, // OSC1_FINE_PARAM
        {28.55f, 15.28f}, // OSC1_FREQ_PARAM
        {34.14f, 32.65f}, // OSC1_FM_PARAM
        {22.64f, 70.45f}, // OSC2_FINE_PARAM
        {28.40f, 87.75f}, // OSC2_FREQ_PARAM
        {34.14f, 70.45f}, // OSC2_FM_PARAM
        {78.87f, 13.20f}, // SLOPES1_RATE_PARAM
        {78.74f, 89.30f}, // SLOPES2_RATE_PARAM
        {60.12f, 32.50f}, // FILTER1_FM_PARAM
        {66.35f, 15.28f}, // FILTER1_CUTOFF_PARAM
        {72.55f, 32.50f}, // FILTER1_RES_PARAM
        {60.12f, 70.45f}, // FILTER2_FM_PARAM
        {66.35f, 87.75f}, // FILTER2_CUTOFF_PARAM
        {72.52f, 70.45f}, // FILTER2_RES_PARAM
        {51.35f, 16.75f}, // AMP_GAIN_PARAM
        {52.26f, 67.50f}, // VOLT_BLEND_PARAM
        {40.30f, 76.35f}, // STOMP_FEEDBACK_PARAM
        {44.50f, 67.50f}, // STOMP_BLEND_PARAM
        {88.75f, 13.15f}, // MIX_CH1_PARAM
        {88.75f, 23.30f}, // MIX_CH2_PARAM
        {88.75f, 33.55f}, // MIX_CH3_PARAM
        {96.42f, 33.55f}, // MIX_CH4_PARAM
        {96.42f, 13.15f}, // MIX_PAN1_PARAM
        {96.42f, 23.30f}, // MIX_PAN2_PARAM
        {92.50f, 87.75f}  // MIX_MAIN_PARAM
    };

    if (paramId < 0 || paramId >= (int)MIX_MAIN_PARAM + 1)
      return 0.0f;

    float ax = 51.35f;
    float ay = 16.75f;
    float px = knobPositions[paramId].x;
    float py = knobPositions[paramId].y;

    float dist = std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));
    return std::exp(-dist / 30.0f);
  }

  void triggerKnobNoise(int paramId, float diff) {
    float proximity = getKnobProximity(paramId);
    if (proximity < 0.01f)
      return;

    float speed = diff * 50.0f;
    float targetVol = std::min(1.5f, speed) * proximity;
    knobLoopPlayer.setTargetVolume(targetVol);
    knobActiveCounter.store(4800, std::memory_order_relaxed);

    static float knobDistanceAccum[NUM_PARAMS] = {0.0f};
    knobDistanceAccum[paramId] += diff;
    if (knobDistanceAccum[paramId] > 1.5f) {
      knobDistanceAccum[paramId] = 0.0f;
      float transientVol = std::min(1.0f, speed * 0.5f) * proximity;
      transientPlayer.trigger(rotateKnobGroup, transientVol);
    }
  }

  // Called by VoltButton widget to handle radio/multi-latch logic
  void handleVoltButton(int index, bool shiftHeld) {
    if (shiftHeld) {
      // Shift: multi-latch toggle (independent, can have 0 or many active)
      voltBtnStates[index] = !voltBtnStates[index];
    } else {
      // Radio mode: activate only this button, turn all others off
      for (int i = 0; i < 4; i++)
        voltBtnStates[i] = false;
      voltBtnStates[index] = true;
    }
  }

  void setActiveCard(int idx, bool load_flash = true) {
    change_card_impl(idx, load_flash);
  }



  ~WorkshopSystem() override {
    if (MonomeBridge::get().is_available()) {
      MonomeBridge::get().disconnect(this);
      MonomeBridge::get().deregister_consumer(this);
    }
    stop_card();
    // Deregister instance and stop web server if last instance
    {
      std::lock_guard<std::mutex> lock(g_instances_mutex);
      g_instances.erase(this);
      if (g_instances.empty()) {
        stop_web_server();
      }
    }
  }

  WorkshopSystem() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
    ensure_monome_registered();

    scratchLoopPlayer.setLoop(piezo_scratch_near_loop_data, piezo_scratch_near_loop_size);
    knobLoopPlayer.setLoop(piezo_rotate_knob_loop_data, piezo_rotate_knob_loop_size);


    // --- Config Parameters ---
    // Knobs (range -150 to 150 to match degrees in original JS)
    configParam(COMPUTER_X_PARAM, -150.f, 150.f, 0.f, "Computer X");
    configParam(COMPUTER_MAIN_PARAM, -150.f, 150.f, 0.f,
                "Computer Main/Coarse");
    configParam(COMPUTER_Y_PARAM, -150.f, 150.f, 0.f, "Computer Y");

    configParam(OSC1_FINE_PARAM, -150.f, 150.f, 0.f, "Osc 1 Fine Pitch",
                " cents", 0.f, 279.f / 150.f);
    configParam(OSC1_FREQ_PARAM, -150.f, 150.f, 0.f, "Osc 1 Frequency");
    configParam(OSC1_FM_PARAM, -150.f, 150.f, -150.f, "Osc 1 FM Depth");

    configParam(OSC2_FINE_PARAM, -150.f, 150.f, 0.f, "Osc 2 Fine Pitch",
                " cents", 0.f, 279.f / 150.f);
    configParam(OSC2_FREQ_PARAM, -150.f, 150.f, 0.f, "Osc 2 Frequency");
    configParam(OSC2_FM_PARAM, -150.f, 150.f, -150.f, "Osc 2 FM Depth");

    configParam(SLOPES1_RATE_PARAM, -150.f, 150.f, 0.f, "Slopes 1 Rate");
    configParam(SLOPES2_RATE_PARAM, -150.f, 150.f, 0.f, "Slopes 2 Rate");

    configParam(FILTER1_FM_PARAM, -150.f, 150.f, -150.f, "Filter 1 FM Depth");
    configParam(FILTER1_CUTOFF_PARAM, -150.f, 150.f, -150.f, "Filter 1 Cutoff");
    configParam(FILTER1_RES_PARAM, -150.f, 150.f, -150.f, "Filter 1 Resonance");

    configParam(FILTER2_FM_PARAM, -150.f, 150.f, -150.f, "Filter 2 FM Depth");
    configParam(FILTER2_CUTOFF_PARAM, -150.f, 150.f, -150.f, "Filter 2 Cutoff");
    configParam(FILTER2_RES_PARAM, -150.f, 150.f, -150.f, "Filter 2 Resonance");

    configParam(AMP_GAIN_PARAM, -150.f, 150.f, -150.f, "Amplifier Gain");
    configParam(VOLT_BLEND_PARAM, -150.f, 150.f, -150.f, "Voltages Blend");
    configParam(STOMP_FEEDBACK_PARAM, -150.f, 150.f, 0.f, "Stompbox Feedback");
    configParam(STOMP_BLEND_PARAM, -150.f, 150.f, -150.f,
                "Stompbox Dry/Wet Blend");

    // Mixer: channel volumes default to center (0° = 0.5 gain), master at
    // center (1.0 gain)
    configParam(MIX_CH1_PARAM, -150.f, 150.f, 0.f, "Mixer Channel 1 Level");
    configParam(MIX_CH2_PARAM, -150.f, 150.f, 0.f, "Mixer Channel 2 Level");
    configParam(MIX_CH3_PARAM, -150.f, 150.f, 0.f, "Mixer Channel 3 Level");
    configParam(MIX_CH4_PARAM, -150.f, 150.f, 0.f, "Mixer Channel 4 Level");
    configParam(MIX_PAN1_PARAM, -150.f, 150.f, 0.f, "Mixer Channel 1 Pan");
    configParam(MIX_PAN2_PARAM, -150.f, 150.f, 0.f, "Mixer Channel 2 Pan");
    configParam(MIX_MAIN_PARAM, -150.f, 150.f, 0.f, "Mixer Main Volume");

    // Switches
    configParam(COMPUTER_SWITCH_PARAM, 0.f, 2.f, 1.f,
                "Computer Z Switch"); // 3-way momentary-down
    configParam(AMP_SWITCH_PARAM, 0.f, 1.f, 0.f,
                "Amplifier Mode (Clean / LoFi)");
    configParam(FILTER1_SWITCH_PARAM, 0.f, 1.f, 0.f, "Filter 1 Mode (BP / HP)");
    configParam(FILTER2_SWITCH_PARAM, 0.f, 1.f, 0.f, "Filter 2 Mode (BP / HP)");
    configParam(SLOPES1_SHAPE_PARAM, 0.f, 2.f, 1.f,
                "Slopes 1 Shape (FastRise / Both / FastFall)");
    configParam(SLOPES2_SHAPE_PARAM, 0.f, 2.f, 1.f,
                "Slopes 2 Shape (FastRise / Both / FastFall)");
    configParam(SLOPES1_MODE_PARAM, 0.f, 2.f, 1.f,
                "Slopes 1 Mode (Loop / Slew / Gate)");
    configParam(SLOPES2_MODE_PARAM, 0.f, 2.f, 1.f,
                "Slopes 2 Mode (Loop / Slew / Gate)");

    // Voltage Buttons
    configParam(VOLT_BTN1_PARAM, 0.f, 1.f, 0.f, "Voltage Button 1");
    configParam(VOLT_BTN2_PARAM, 0.f, 1.f, 0.f, "Voltage Button 2");
    configParam(VOLT_BTN3_PARAM, 0.f, 1.f, 0.f, "Voltage Button 3");
    configParam(VOLT_BTN4_PARAM, 0.f, 1.f, 0.f, "Voltage Button 4");

    // --- Config Inputs ---
    configInput(OSC1_PITCH_IN, "Osc 1 Pitch (1V/Oct)");
    configInput(OSC2_PITCH_IN, "Osc 2 Pitch (1V/Oct)");
    configInput(OSC1_FM_IN, "Osc 1 FM CV");
    configInput(OSC2_FM_IN, "Osc 2 FM CV");
    configInput(STEREO_IN_JACK, "Stereo Line In (Poly L/R)");
    configInput(RING_IN1, "Ring Mod Input 1 (norm: Osc1 Sin)");
    configInput(RING_IN2, "Ring Mod Input 2 (norm: Osc2 Sin)");
    configInput(STOMP_IN, "Stompbox Audio In");
    configInput(STOMP_RETURN, "Stompbox Pedal Return");
    configInput(AMP_IN, "Amplifier Audio In (norm: contact noise)");
    configInput(FILTER1_IN, "Filter 1 Audio In");
    configInput(FILTER2_IN, "Filter 2 Audio In (norm: Filter1 LP)");
    configInput(FILTER1_FM_IN, "Filter 1 Cutoff FM CV");
    configInput(FILTER2_FM_IN, "Filter 2 Cutoff FM CV");
    configInput(SLOPES1_IN, "Slopes 1 In");
    configInput(SLOPES1_CV_IN, "Slopes 1 Rate CV");
    configInput(SLOPES2_IN, "Slopes 2 In");
    configInput(SLOPES2_CV_IN, "Slopes 2 Rate CV");
    configInput(MIXER1_IN, "Mixer Channel 1 Input");
    configInput(MIXER2_IN, "Mixer Channel 2 Input");
    configInput(MIXER3_IN, "Mixer Channel 3 Input");
    configInput(MIXER4_IN, "Mixer Channel 4 Input");
    configInput(COMPUTER_AUDIO1_IN, "Computer Audio 1 In");
    configInput(COMPUTER_CV1_IN, "Computer CV 1 In");
    configInput(COMPUTER_CV2_IN, "Computer CV 2 In");
    configInput(COMPUTER_PULSE1_IN, "Computer Pulse 1 In");
    configInput(COMPUTER_AUDIO2_IN, "Computer Audio 2 In");
    configInput(COMPUTER_PULSE2_IN, "Computer Pulse 2 In");

    // --- Config Outputs ---
    configOutput(OSC1_SQR_OUT, "Osc 1 Square (±4.5V)");
    configOutput(OSC2_SQR_OUT, "Osc 2 Square (±4.5V)");
    configOutput(OSC1_SIN_OUT, "Osc 1 Sine (±5.5V)");
    configOutput(OSC2_SIN_OUT, "Osc 2 Sine (±5.5V)");
    configOutput(STEREO_L_OUT, "Stereo Left Line Out");
    configOutput(STEREO_R_OUT, "Stereo Right Line Out");
    configOutput(RING_OUT, "Ring Modulator Output");
    configOutput(STOMP_OUT, "Stompbox Audio Out (dry/wet)");
    configOutput(STOMP_SEND, "Stompbox Send (to pedal)");
    configOutput(AMP_OUT, "Amplifier Audio Out");
    configOutput(VOLT1_OUT, "Voltage 1 Out");
    configOutput(VOLT2_OUT, "Voltage 2 Out");
    configOutput(VOLT3_OUT, "Voltage 3 Out");
    configOutput(VOLT4_OUT, "Voltage 4 Out");
    configOutput(FILTER1_HP_OUT, "Filter 1 Switched HP/BP Out");
    configOutput(FILTER2_HP_OUT, "Filter 2 Switched HP/BP Out");
    configOutput(FILTER1_LP_OUT, "Filter 1 LP Out");
    configOutput(FILTER2_LP_OUT, "Filter 2 LP Out");
    configOutput(SLOPES1_OUT, "Slopes 1 Output");
    configOutput(SLOPES2_OUT, "Slopes 2 Output");
    configOutput(MIXER_L_OUT, "Mixer Left Output");
    configOutput(MIXER_R_OUT, "Mixer Right Output");
    configOutput(PHONES1_OUT, "Headphones 1 Output");
    configOutput(PHONES2_OUT, "Headphones 2 Output");
    configOutput(COMPUTER_AUDIO1_OUT, "Computer Audio 1 Out");
    configOutput(COMPUTER_CV1_OUT, "Computer CV 1 Out");
    configOutput(COMPUTER_PULSE1_OUT, "Computer Pulse 1 Out");
    configOutput(COMPUTER_AUDIO2_OUT, "Computer Audio 2 Out");
    configOutput(COMPUTER_CV2_OUT, "Computer CV 2 Out");
    configOutput(COMPUTER_PULSE2_OUT, "Computer Pulse 2 Out");

    // Seed random for noise generator
    std::srand(std::time(nullptr));

    for (int i = 0; i < NUM_PARAMS; ++i) {
      lastParams[i] = params[i].getValue();
    }

    for (int i = 0; i < NUM_INPUTS; ++i) {
      lastInputConnected[i] = inputs[i].isConnected();
    }
    for (int i = 0; i < NUM_OUTPUTS; ++i) {
      lastOutputConnected[i] = outputs[i].isConnected();
    }

    // Register cards dynamically
    register_all_cards();

    // Register instance and start web server
    {
      std::lock_guard<std::mutex> lock(g_instances_mutex);
      g_instances.insert(this);
      start_web_server();
    }

    change_card(-1);
  }

  void onSampleRateChange() override {
    float sr = APP->engine->getSampleRate();
    vco1.setSampleRate(sr);
    vco2.setSampleRate(sr);
    filter1.setSampleRate(sr);
    filter2.setSampleRate(sr);
    amp.setSampleRate(sr);
  }

  // Helper functions for parameter scaling (mapping degrees to exact values)
  float getOscFreq(float kVal) {
    float center = 130.81f;
    if (kVal >= 0.0f) {
      return center * std::pow(26500.0f / center, kVal / 150.0f);
    } else {
      return center * std::pow(0.5f / center, std::abs(kVal) / 150.0f);
    }
  }

  float getFineTune(float kVal) {
    float totalCents = 1200.0f * std::log2(1.38f);
    return (kVal / 150.0f) * (totalCents / 2.0f);
  }

  float getKnobValue(float deg, float minVal, float maxVal,
                     bool isExp = false) {
    float norm = (deg + 150.0f) / 300.0f;
    norm = std::max(0.0f, std::min(1.0f, norm));
    if (isExp) {
      if (minVal == 0.0f || std::abs(minVal) < 0.001f) {
        return maxVal * norm * norm;
      }
      return minVal * std::pow(maxVal / minVal, norm);
    }
    return minVal + (maxVal - minVal) * norm;
  }

  json_t *dataToJson() override {
    json_t *rootJ = json_object();
    json_object_set_new(rootJ, "internalComputerEnabled",
                        json_boolean(internalComputerEnabled));
    json_object_set_new(rootJ, "activeCardIdx", json_integer(activeCardIdx));
    if (activeCardIdx >= 0 && activeCardIdx < (int)g_card_registry.size()) {
      json_object_set_new(rootJ, "activeCardId", json_string(g_card_registry[activeCardIdx].id.c_str()));
      std::string flash_hex = hex_encode(card_globals.g_flash_memory_val, PICO_FLASH_SIZE_BYTES);
      json_object_set_new(rootJ, "flashMemoryHex", json_string(flash_hex.c_str()));
    }
    json_object_set_new(rootJ, "leftUtility", json_integer(utility_indices[0]));
    json_object_set_new(rootJ, "rightUtility", json_integer(utility_indices[1]));
    json_object_set_new(rootJ, "stereoDeviceInput", stereoDeviceInput.toJson());
    json_object_set_new(rootJ, "stereoDeviceOutput",
                        stereoDeviceOutput.toJson());
    json_object_set_new(rootJ, "midi_input", midiInput.toJson());
    json_object_set_new(rootJ, "midi_output", midiOutput.toJson());
    json_object_set_new(rootJ, "filter1Jumper",
                        json_boolean(filter1.jumperClosed));
    json_object_set_new(rootJ, "filter2Jumper",
                        json_boolean(filter2.jumperClosed));

    json_t *buttonsJ = json_array();
    for (int i = 0; i < 4; ++i) {
      json_array_append_new(buttonsJ, json_boolean(voltBtnStates[i]));
    }
    json_object_set_new(rootJ, "voltBtnStates", buttonsJ);

    return rootJ;
  }

  void dataFromJson(json_t *rootJ) override {
    json_t *icEnabledJ = json_object_get(rootJ, "internalComputerEnabled");
    if (icEnabledJ)
      internalComputerEnabled = json_boolean_value(icEnabledJ);

    // Dynamic cards registration must be run before we try to look up the ID
    register_all_cards();

    int target_card_idx = -1;
    json_t *idJ = json_object_get(rootJ, "activeCardId");
    if (idJ) {
      std::string saved_id = json_string_value(idJ);
      for (size_t i = 0; i < g_card_registry.size(); i++) {
        if (g_card_registry[i].id == saved_id) {
          target_card_idx = (int)i;
          break;
        }
      }
    } else {
      json_t *activeCardJ = json_object_get(rootJ, "activeCardIdx");
      if (activeCardJ)
        target_card_idx = json_integer_value(activeCardJ);
    }

    bool has_json_flash = false;
    json_t *flashHexJ = json_object_get(rootJ, "flashMemoryHex");
    if (flashHexJ) {
      std::string flash_hex = json_string_value(flashHexJ);
      hex_decode(flash_hex, card_globals.g_flash_memory_val, PICO_FLASH_SIZE_BYTES);
      has_json_flash = true;
    }
    setActiveCard(target_card_idx, !has_json_flash);

    json_t *luJ = json_object_get(rootJ, "leftUtility");
    if (luJ) utility_indices[0] = json_integer_value(luJ);
    json_t *ruJ = json_object_get(rootJ, "rightUtility");
    if (ruJ) utility_indices[1] = json_integer_value(ruJ);

    json_t *stereoInputJ = json_object_get(rootJ, "stereoDeviceInput");
    if (stereoInputJ)
      stereoDeviceInput.fromJson(stereoInputJ);
    json_t *miJ = json_object_get(rootJ, "midi_input");
    if (miJ) midiInput.fromJson(miJ);
    json_t *moJ = json_object_get(rootJ, "midi_output");
    if (moJ) midiOutput.fromJson(moJ);

    json_t *stereoOutputJ = json_object_get(rootJ, "stereoDeviceOutput");
    if (stereoOutputJ)
      stereoDeviceOutput.fromJson(stereoOutputJ);

    json_t *oldPortJ = json_object_get(rootJ, "stereoDevicePort");
    if (oldPortJ) {
      stereoDeviceInput.fromJson(oldPortJ);
      stereoDeviceOutput.fromJson(oldPortJ);
    }

    json_t *f1J = json_object_get(rootJ, "filter1Jumper");
    if (f1J)
      filter1.jumperClosed = json_boolean_value(f1J);

    json_t *f2J = json_object_get(rootJ, "filter2Jumper");
    if (f2J)
      filter2.jumperClosed = json_boolean_value(f2J);

    json_t *buttonsJ = json_object_get(rootJ, "voltBtnStates");
    if (buttonsJ) {
      for (int i = 0; i < 4; ++i) {
        json_t *btnJ = json_array_get(buttonsJ, i);
        if (btnJ)
          voltBtnStates[i] = json_boolean_value(btnJ);
      }
    }
  }

  void process(const ProcessArgs &args) override {
    // --- 1. VOLTAGE BUTTON LED UPDATE ---
    // LED brightness driven directly from voltBtnStates (managed by VoltButton
    // widget)
    for (int i = 0; i < 4; ++i) {
      lights[VOLT_BTN1_LED + i].setBrightness(voltBtnStates[i] ? 1.0f : 0.0f);
    }

    // --- 2. OSCILLATOR PARAMETERS ---
    float osc1Freq = getOscFreq(params[OSC1_FREQ_PARAM].getValue());
    float osc1Fine = getFineTune(params[OSC1_FINE_PARAM].getValue());
    // FM knob: linear attenuator, at maximum depth the frequency scales by exp(0.71 * V)
    // which corresponds to a maximum depth of 0.71 * 1200 / ln(2) = 1229.1765 cents/V.
    float osc1FmAmt =
        getKnobValue(params[OSC1_FM_PARAM].getValue(), 0.0f, 0.71f * 1200.f / std::log(2.f), false);

    float osc2Freq = getOscFreq(params[OSC2_FREQ_PARAM].getValue());
    float osc2Fine = getFineTune(params[OSC2_FINE_PARAM].getValue());
    float osc2FmAmt =
        getKnobValue(params[OSC2_FM_PARAM].getValue(), 0.0f, 0.71f * 1200.f / std::log(2.f), false);

    // --- 3. PITCH CV & FM NORMALIZATION ---
    float pitch1 = inputs[OSC1_PITCH_IN].isConnected()
                       ? inputs[OSC1_PITCH_IN].getVoltage()
                       : 0.0f;
    float pitch2 = inputs[OSC2_PITCH_IN].isConnected()
                       ? inputs[OSC2_PITCH_IN].getVoltage()
                       : 0.0f;

    // V/Oct: 1200 cents per volt
    float osc1Detune = osc1Fine + (pitch1 * 1200.0f);
    float osc2Detune = osc2Fine + (pitch2 * 1200.0f);

    // FM normalization: when unpatched, feed the other oscillator's sine in
    // Volts (osc1SinVolts and osc2SinVolts are cached from the previous sample)
    float osc1FmInput = inputs[OSC1_FM_IN].isConnected()
                            ? inputs[OSC1_FM_IN].getVoltage()
                            : osc2SinVolts;
    float osc2FmInput = inputs[OSC2_FM_IN].isConnected()
                            ? inputs[OSC2_FM_IN].getVoltage()
                            : osc1SinVolts;

    float selfPatch1 = isOsc1SelfPatched ? 1.0f : 0.0f;
    float selfPatch2 = isOsc2SelfPatched ? 1.0f : 0.0f;

    // --- 4. PROCESS OSCILLATORS ---
    VCO::Output osc1Out =
        vco1.process(osc1Freq, osc1Detune, osc1FmInput, osc1FmAmt, selfPatch1);
    VCO::Output osc2Out =
        vco2.process(osc2Freq, osc2Detune, osc2FmInput, osc2FmAmt, selfPatch2);

    // Cache sine outputs in Volts for next-sample FM normalization
    osc1SinVolts = osc1Out.sine * 5.25f;
    osc2SinVolts = osc2Out.sine * 5.25f;

    // Calibrated output levels: Sine ±5.25V, Square ±4.45V (measured: 10.5V ptp and 8.9V ptp)
    outputs[OSC1_SIN_OUT].setVoltage(osc1SinVolts);
    outputs[OSC1_SQR_OUT].setVoltage(osc1Out.square * 4.45f);
    outputs[OSC2_SIN_OUT].setVoltage(osc2SinVolts);
    outputs[OSC2_SQR_OUT].setVoltage(osc2Out.square * 4.45f);

    // --- 5. STEREO LINE IN ---
    // Polyphonic jack: Ch1 = Left, Ch2 = Right. Mono cable grounds Right (TRS
    // tip normalisation).
    float stL = 0.0f, stR = 0.0f;
    if (inputs[STEREO_IN_JACK].isConnected()) {
      stL = inputs[STEREO_IN_JACK].getPolyVoltage(0);
      stR = (inputs[STEREO_IN_JACK].getChannels() >= 2)
                ? inputs[STEREO_IN_JACK].getPolyVoltage(1)
                : 0.0f;
    } else if (stereoDeviceInput.getDevice()) {
      if (!stereoDeviceInput.inputBuffer.empty()) {
        StereoFrame frame = stereoDeviceInput.inputBuffer.shift();
        stL = frame.l;
        stR = frame.r;
      }
    } else {
      // Unconnected: tiny open-input noise floor (audio interface simulation)
      stL = (((float)std::rand() / RAND_MAX) - 0.5f) * 1.5e-5f;
      stR = (((float)std::rand() / RAND_MAX) - 0.5f) * 1.5e-5f;
    }
    // Stereo Line In applies 2.44x gain (measured from stereo_in recording)
    outputs[STEREO_L_OUT].setVoltage(stL * 2.44f);
    outputs[STEREO_R_OUT].setVoltage(stR * 2.44f);

    // --- 6. RING MODULATOR ---
    // Normals: A = Osc1 Sin, B = Osc2 Sin
    float ringA = inputs[RING_IN1].isConnected()
                      ? inputs[RING_IN1].getVoltage()
                      : outputs[OSC1_SIN_OUT].getVoltage();
    float ringB = inputs[RING_IN2].isConnected()
                      ? inputs[RING_IN2].getVoltage()
                      : outputs[OSC2_SIN_OUT].getVoltage();
    outputs[RING_OUT].setVoltage(RingMod::process(ringA, ringB));

    // --- 7. STOMPBOX ---
    float stompIn = inputs[STOMP_IN].getVoltage();
    float returnIn = 0.0f;
    float returnInModular = 0.0f;
    float wetSource = 0.0f;

    if (inputs[STOMP_RETURN].isConnected()) {
      returnIn = inputs[STOMP_RETURN].getVoltage();
      // Boost line-level return (±1V) to modular level (±10V), clamped to ±11.5V rails
      returnInModular = std::max(-11.5f, std::min(11.5f, returnIn * 10.0f));
      wetSource = returnInModular;
    } else {
      // Normalled feedback path: return = last stompbox send
      returnInModular = std::max(-11.5f, std::min(11.5f, stompLastSend * 0.9f));
      wetSource = 0.0f; // Blend wet side is silent when nothing is plugged in
    }

    float stompBlend =
        getKnobValue(params[STOMP_BLEND_PARAM].getValue(), 0.0f, 1.0f);
    float fbAngle = params[STOMP_FEEDBACK_PARAM].getValue();
    float stompFbGain =
        (std::abs(fbAngle) > 10.0f) ? (fbAngle / 150.0f) * 0.556f : 0.0f;

    float sendModular =
        stomp.processSend(stompIn, returnInModular, stompFbGain);
    // Clamp the feedback summing stage (SEND) to the ±11.5V rails
    sendModular = std::max(-11.5f, std::min(11.5f, sendModular));

    // Save send for next sample normalization
    stompLastSend = sendModular;

    // Scale send from modular to line level (0.09x, measured from stompbox_send recording)
    outputs[STOMP_SEND].setVoltage(sendModular * 0.09f);
    float stompOutVal = stomp.processOut(sendModular, wetSource, stompBlend);
    // Clamp the final output mix to the ±11.5V rails
    stompOutVal = std::max(-11.5f, std::min(11.5f, stompOutVal));
    outputs[STOMP_OUT].setVoltage(stompOutVal);

    // --- 8. AMPLIFIER ---
    // Normal: contact/piezo mic noise simulation when amp input is disconnected
    float ampIn = 0.0f;
    if (inputs[AMP_IN].isConnected()) {
      ampIn = inputs[AMP_IN].getVoltage();
      transientPlayer.stopAll();
      scratchLoopPlayer.setTargetVolume(0.0f);
      knobLoopPlayer.setTargetVolume(0.0f);
    } else {
      int sCounter = scratchActiveCounter.load(std::memory_order_relaxed);
      if (sCounter > 0) {
        sCounter--;
        scratchActiveCounter.store(sCounter, std::memory_order_relaxed);
        if (sCounter == 0) {
          scratchLoopPlayer.setTargetVolume(0.0f);
        }
      }

      int kCounter = knobActiveCounter.load(std::memory_order_relaxed);
      if (kCounter > 0) {
        kCounter--;
        knobActiveCounter.store(kCounter, std::memory_order_relaxed);
        if (kCounter == 0) {
          knobLoopPlayer.setTargetVolume(0.0f);
        }
      }

      float transOut = transientPlayer.process(args.sampleRate);
      float scratchOut = scratchLoopPlayer.process(args.sampleRate);
      if (scratchApplyLowpass.load(std::memory_order_relaxed)) {
        scratchLpState += (scratchOut - scratchLpState) * 0.12f;
        scratchOut = scratchLpState;
      }
      float knobOut = knobLoopPlayer.process(args.sampleRate);

      float contactSignal = transOut + scratchOut + knobOut;
      // Scale raw sample levels to physical piezo voltages (peaking around 0.35V for hard transients)
      // so they can saturate the preamp stages and mask the analog noise floor
      ampIn = contactSignal * 0.35f;
    }
    // Linear normalized gain value: maps knob range [-150.0, 150.0] to [0.0, 1.0]
    float ampGain = (params[AMP_GAIN_PARAM].getValue() + 150.0f) / 300.0f;
    ampGain = std::max(0.0f, std::min(1.0f, ampGain));
    // Mode switch: Left (value > 0.5f) is LoFi (1), Right (value <= 0.5f) is Clean/Mic (0)
    int ampMode =
        (params[AMP_SWITCH_PARAM].getValue() > 0.5f) ? 1 : 0;
    outputs[AMP_OUT].setVoltage(amp.process(ampIn, ampGain, ampMode));

    // VU meter LEDs (4 steps, cascade from bottom)
    float vuLevel = amp.getVULevel();
    for (int i = 0; i < 4; ++i) {
      lights[AMP_LED1 + i].setBrightness(
          std::max(0.0f, std::min(1.0f, vuLevel - (float)i)));
    }

    // --- 9. 4 VOLTAGES ---
    float voltKnobVal = params[VOLT_BLEND_PARAM].getValue();
    int voltBtnMask = 0;
    for (int i = 0; i < 4; ++i) {
      if (voltBtnStates[i])
        voltBtnMask |= (1 << i);
    }
    outputs[VOLT1_OUT].setVoltage(
        Voltages::getInterpolatedVoltage(voltKnobVal, voltBtnMask, 0));
    outputs[VOLT2_OUT].setVoltage(
        Voltages::getInterpolatedVoltage(voltKnobVal, voltBtnMask, 1));
    outputs[VOLT3_OUT].setVoltage(
        Voltages::getInterpolatedVoltage(voltKnobVal, voltBtnMask, 2));
    outputs[VOLT4_OUT].setVoltage(
        Voltages::getInterpolatedVoltage(voltKnobVal, voltBtnMask, 3));

    // --- 10. HUMPBACK FILTERS ---
    // Filter 1
    float x1 = (params[FILTER1_CUTOFF_PARAM].getValue() + 150.0f) / 300.0f;
    x1 = std::max(0.0f, std::min(1.0f, x1));
    float filter1CutoffHz = 27.4f * std::pow(1357.0f, x1);
    filter1CutoffHz = std::min(16500.0f, filter1CutoffHz);

    float filter1FmAmt = (params[FILTER1_FM_PARAM].getValue() + 150.0f) / 300.0f;
    filter1FmAmt = std::max(0.0f, std::min(1.0f, filter1FmAmt));

    float rRaw1 = (params[FILTER1_RES_PARAM].getValue() + 150.0f) / 300.0f;
    float resVal1 = std::max(0.0f, std::min(1.0f, rRaw1));

    float filt1In = inputs[FILTER1_IN].getVoltage() / 5.0f;
    float filt1Fm = inputs[FILTER1_FM_IN].getVoltage();
    int filt1Mode = (int)params[FILTER1_SWITCH_PARAM].getValue();

    HumpbackFilter::Output filt1Out =
        filter1.process(filt1In, filter1CutoffHz, resVal1, filt1Fm,
                        filter1FmAmt, (float)filt1Mode);

    outputs[FILTER1_LP_OUT].setVoltage(filt1Out.lp * 5.0f);
    outputs[FILTER1_HP_OUT].setVoltage(filt1Out.switched * 5.0f);

    // Filter 2
    float x2 = (params[FILTER2_CUTOFF_PARAM].getValue() + 150.0f) / 300.0f;
    x2 = std::max(0.0f, std::min(1.0f, x2));
    float filter2CutoffHz = 27.4f * std::pow(1357.0f, x2);
    filter2CutoffHz = std::min(16500.0f, filter2CutoffHz);

    float filter2FmAmt = (params[FILTER2_FM_PARAM].getValue() + 150.0f) / 300.0f;
    filter2FmAmt = std::max(0.0f, std::min(1.0f, filter2FmAmt));

    float rRaw2 = (params[FILTER2_RES_PARAM].getValue() + 150.0f) / 300.0f;
    float resVal2 = std::max(0.0f, std::min(1.0f, rRaw2));

    float filt2In = (inputs[FILTER2_IN].isConnected()
                         ? inputs[FILTER2_IN].getVoltage()
                         : outputs[FILTER1_LP_OUT].getVoltage()) /
                    5.0f;
    float filt2Fm = inputs[FILTER2_FM_IN].getVoltage();
    int filt2Mode = (int)params[FILTER2_SWITCH_PARAM].getValue();

    HumpbackFilter::Output filt2Out =
        filter2.process(filt2In, filter2CutoffHz, resVal2, filt2Fm,
                        filter2FmAmt, (float)filt2Mode);

    outputs[FILTER2_LP_OUT].setVoltage(filt2Out.lp * 5.0f);
    outputs[FILTER2_HP_OUT].setVoltage(filt2Out.switched * 5.0f);

    // --- 11. SLOPES ---
    // Rate: -150..150 → 0..1 (normalized knob position)
    float slopes1Rate =
        (params[SLOPES1_RATE_PARAM].getValue() + 150.0f) / 300.0f;
    float slopes2Rate =
        (params[SLOPES2_RATE_PARAM].getValue() + 150.0f) / 300.0f;

    // Input: raw volts
    float slopes1In = inputs[SLOPES1_IN].getVoltage();
    float slopes2In = inputs[SLOPES2_IN].getVoltage();

    float slopes1Cv = inputs[SLOPES1_CV_IN].getVoltage() / 5.0f;
    float slopes2Cv = inputs[SLOPES2_CV_IN].getVoltage() / 5.0f;

    // Patchnotes switch state is 0 = up, 1 = middle, 2 = down.
    // Slopes DSP expects 0 = Loop, 1 = Slew, 2 = Gate, so map down to Loop.
    int slopes1Switch = (int)params[SLOPES1_MODE_PARAM].getValue();
    int slopes2Switch = (int)params[SLOPES2_MODE_PARAM].getValue();
    int slopes1Mode = (slopes1Switch == 2) ? 0 : (slopes1Switch == 0 ? 2 : 1);
    int slopes2Mode = (slopes2Switch == 2) ? 0 : (slopes2Switch == 0 ? 2 : 1);

    // Switch shape: value 0 (DOWN) = Shape 2 (upward slope), 1 (center) = Shape
    // 1 (symmetrical), 2 (UP) = Shape 0 (downward slope)
    int slopes1Shape = 2 - (int)params[SLOPES1_SHAPE_PARAM].getValue();
    int slopes2Shape = 2 - (int)params[SLOPES2_SHAPE_PARAM].getValue();

    // Slopes1 = linear, Slopes2 = exponential (isExponential flag)
    Slopes::Output s1Out = slopes1.process(slopes1In, slopes1Cv, slopes1Rate,
                                           slopes1Mode, slopes1Shape, false);
    Slopes::Output s2Out = slopes2.process(slopes2In, slopes2Cv, slopes2Rate,
                                           slopes2Mode, slopes2Shape, true);

    // Output: raw volts
    outputs[SLOPES1_OUT].setVoltage(s1Out.value);
    outputs[SLOPES2_OUT].setVoltage(s2Out.value);

    // Rise/Fall LEDs
    lights[SLOPES1_RISE_LED].setBrightness(s1Out.riseLed);
    lights[SLOPES1_FALL_LED].setBrightness(s1Out.fallLed);
    lights[SLOPES2_RISE_LED].setBrightness(s2Out.riseLed);
    lights[SLOPES2_FALL_LED].setBrightness(s2Out.fallLed);

    // --- 12. COMPUTER SECTION ---
    float compOutL = 0.f, compOutR = 0.f;
    float compOutCv1 = 0.f, compOutCv2 = 0.f;
    float compOutPulse1 = 0.f, compOutPulse2 = 0.f;

    if (internalComputerEnabled && activeCardIdx != -1) {
      if (card_globals.block_audio_processing.load(std::memory_order_acquire)) {
        // Passthrough while loading
        compOutL = inputs[COMPUTER_AUDIO1_IN].getVoltage();
        compOutR = inputs[COMPUTER_AUDIO2_IN].getVoltage();
        compOutCv1 = inputs[COMPUTER_CV1_IN].getVoltage();
        compOutCv2 = inputs[COMPUTER_CV2_IN].getVoltage();
        compOutPulse1 = inputs[COMPUTER_PULSE1_IN].getVoltage();
        compOutPulse2 = inputs[COMPUTER_PULSE2_IN].getVoltage();
      } else {
        card_globals.in_audio_callback.store(true, std::memory_order_release);
        if (!card_globals.block_audio_processing.load(
                std::memory_order_acquire)) {
          t_instance = &card_globals;
          if (card_globals.set_thread_globals_fn) {
            card_globals.set_thread_globals_fn(&card_globals);
          }

          // 1. Gather inputs
          float compKnobMain =
              (params[COMPUTER_MAIN_PARAM].getValue() + 150.f) / 300.f;
          float compKnobX =
              (params[COMPUTER_X_PARAM].getValue() + 150.f) / 300.f;
          float compKnobY =
              (params[COMPUTER_Y_PARAM].getValue() + 150.f) / 300.f;

          g_knobs[0] = compKnobMain;
          g_knobs[1] = compKnobX;
          g_knobs[2] = compKnobY;
          g_switch = (int)params[COMPUTER_SWITCH_PARAM].getValue();

          g_audio_in[0] = inputs[COMPUTER_AUDIO1_IN].getVoltage();
          g_audio_in[1] = inputs[COMPUTER_AUDIO2_IN].getVoltage();
          g_cv_in[0] = inputs[COMPUTER_CV1_IN].getVoltage();
          g_cv_in[1] = inputs[COMPUTER_CV2_IN].getVoltage();

          pulse1Trigger.process(inputs[COMPUTER_PULSE1_IN].getVoltage(), 0.1f,
                                1.0f);
          g_pulse_in[0] = pulse1Trigger.isHigh();
          pulse2Trigger.process(inputs[COMPUTER_PULSE2_IN].getVoltage(), 0.1f,
                                1.0f);
          g_pulse_in[1] = pulse2Trigger.isHigh();

          g_input_connected[0] = inputs[COMPUTER_AUDIO1_IN].isConnected();
          g_input_connected[1] = inputs[COMPUTER_AUDIO2_IN].isConnected();
          g_input_connected[2] = inputs[COMPUTER_CV1_IN].isConnected();
          g_input_connected[3] = inputs[COMPUTER_CV2_IN].isConnected();
          g_input_connected[4] = inputs[COMPUTER_PULSE1_IN].isConnected();
          g_input_connected[5] = inputs[COMPUTER_PULSE2_IN].isConnected();

          // 2. Drive the card's DSP callback
          ComputerCard *card = card_globals.card_ptr;
          if (card &&
              card_globals.g_dsp_ready.load(std::memory_order_relaxed)) {
            card_globals.dsp_phase +=
                card_globals.expected_sample_rate / args.sampleRate;
            while (card_globals.dsp_phase >= 1.0) {
              card->update_inputs();
              card->ProcessSample();
              card_globals.dsp_phase -= 1.0;
            }
          }

          // 1.5. MIDI Input
          rack::midi::Message rx_msg;
          while (midiInput.tryPop(&rx_msg, args.frame)) {
              push_midi_to_rx_queue(rx_msg.bytes.data(), rx_msg.bytes.size());
              translate_midi_to_monome_grid(rx_msg.bytes.data(), rx_msg.bytes.size());
          }

          // 2.5. MIDI Output
          uint8_t tx_byte;
          std::vector<rack::midi::Message> tx_msgs;
          while (g_midi_tx_byte_queue.pop(tx_byte))
              txParser.parse_byte(tx_byte, tx_msgs);
          for (const auto& tx_msg : tx_msgs) {
              midiOutput.sendMessage(tx_msg);
              websocket_midi_tx_queue.push(tx_msg.bytes);
          }

          // 2.7. Serial Output
          uint8_t serial_tx_byte;
          std::vector<uint8_t> serial_bytes;
          std::vector<rack::midi::Message> grid_midi_msgs;
          serialParser.target_grid = connected_grid;
          while (g_serial_tx_byte_queue.pop(serial_tx_byte)) {
              serial_bytes.push_back(serial_tx_byte);
              serialParser.parse_byte(serial_tx_byte, grid_midi_msgs);
              
              if (serial_tx_byte == 0x05 && connected_grid && !card_globals.sample_mgr_active) {
                  const MonomeDevice& dev = connected_grid->getDevice();
                  uint8_t cols = (dev.width  > 0 && dev.width  <= 16) ? (uint8_t)dev.width  : 16;
                  uint8_t rows = (dev.height > 0 && dev.height <= 16) ? (uint8_t)dev.height :  8;
                  uint8_t size_resp[3] = { 0x03, cols, rows };
                  g_serial_rx_byte_queue.push(size_resp, 3);
              }
          }
          if (!serial_bytes.empty()) {
              websocket_serial_tx_queue.push(serial_bytes);
          }
          for (const auto& tx_msg : grid_midi_msgs) {
              midiOutput.sendMessage(tx_msg);
          }
          if (!serial_bytes.empty()) {
              websocket_serial_tx_queue.push(serial_bytes);
          }

          // 3. Write outputs
          compOutL = g_audio_out[0];
          compOutR = g_audio_out[1];
          compOutCv1 = g_cv_out[0];
          compOutCv2 = g_cv_out[1];
          compOutPulse1 = g_pulse_out[0] ? 5.f : 0.f;
          compOutPulse2 = g_pulse_out[1] ? 5.f : 0.f;

          // 4. LEDs
          for (int i = 0; i < 6; i++) {
            lights[COMP_LED0 + i].setBrightness(g_led_brightness[i]);
          }
        }
        card_globals.in_audio_callback.store(false, std::memory_order_release);
      }
    } else {
      // Bridge/passthrough mode: forwards Computer I/O jacks from patch to
      // outputs
      compOutL = inputs[COMPUTER_AUDIO1_IN].getVoltage();
      compOutR = inputs[COMPUTER_AUDIO2_IN].getVoltage();
      compOutCv1 = inputs[COMPUTER_CV1_IN].getVoltage();
      compOutCv2 = inputs[COMPUTER_CV2_IN].getVoltage();
      compOutPulse1 = inputs[COMPUTER_PULSE1_IN].getVoltage();
      compOutPulse2 = inputs[COMPUTER_PULSE2_IN].getVoltage();
      for (int i = 0; i < 6; ++i)
        lights[COMP_LED0 + i].setBrightness(0.0f);
    }

    outputs[COMPUTER_AUDIO1_OUT].setVoltage(compOutL);
    outputs[COMPUTER_AUDIO2_OUT].setVoltage(compOutR);
    outputs[COMPUTER_CV1_OUT].setVoltage(compOutCv1);
    outputs[COMPUTER_CV2_OUT].setVoltage(compOutCv2);
    outputs[COMPUTER_PULSE1_OUT].setVoltage(compOutPulse1);
    outputs[COMPUTER_PULSE2_OUT].setVoltage(compOutPulse2);

    // --- 13. MIXER ---
    float mixCh1 = inputs[MIXER1_IN].getVoltage();
    float mixCh2 = inputs[MIXER2_IN].getVoltage();
    float mixCh3 = inputs[MIXER3_IN].getVoltage();
    float mixCh4 = inputs[MIXER4_IN].getVoltage();

    // Channel volumes: 0.0–1.0 (knob center = 0.5)
    float mixVol1 = getKnobValue(params[MIX_CH1_PARAM].getValue(), 0.0f, 1.0f);
    float mixVol2 = getKnobValue(params[MIX_CH2_PARAM].getValue(), 0.0f, 1.0f);
    float mixVol3 = getKnobValue(params[MIX_CH3_PARAM].getValue(), 0.0f, 1.0f);
    float mixVol4 = getKnobValue(params[MIX_CH4_PARAM].getValue(), 0.0f, 1.0f);

    float mixPan1 =
        getKnobValue(params[MIX_PAN1_PARAM].getValue(), -1.0f, 1.0f);
    float mixPan2 =
        getKnobValue(params[MIX_PAN2_PARAM].getValue(), -1.0f, 1.0f);

    // Master volume: 0.0–2.0 (knob center = 1.0)
    float masterVol =
        getKnobValue(params[MIX_MAIN_PARAM].getValue(), 0.0f, 2.0f);

    Mixer::Output mixOut =
        mixer.process(mixCh1, mixCh2, mixCh3, mixCh4, mixVol1, mixVol2, mixVol3,
                      mixVol4, mixPan1, mixPan2, masterVol);

    outputs[MIXER_L_OUT].setVoltage(mixOut.mixL);
    outputs[MIXER_R_OUT].setVoltage(mixOut.mixR);
    outputs[PHONES1_OUT].setVoltage(mixOut.phones1);
    outputs[PHONES2_OUT].setVoltage(mixOut.phones2);

    // Send headphones output to the stereo device output port (normalized from
    // VCV ±5V to ±1.0)
    if (stereoDeviceOutput.getDevice()) {
      if (!stereoDeviceOutput.outputBuffer.full()) {
        stereoDeviceOutput.outputBuffer.push(
            {mixOut.phones1 / 5.0f, mixOut.phones2 / 5.0f});
      }
    }

    // Check knob turn noise (run on every sample, very lightweight)
    for (int i = 0; i < (int)MIX_MAIN_PARAM + 1; ++i) {
      float val = params[i].getValue();
      float diff = std::abs(val - lastParams[i]);
      if (diff > 0.02f) {
        triggerKnobNoise(i, diff);
      }
      lastParams[i] = val;
    }

    // Check switch and button changes
    for (int i = COMPUTER_SWITCH_PARAM; i < NUM_PARAMS; ++i) {
      float val = params[i].getValue();
      float lastVal = lastParams[i];
      if (val != lastVal) {
        lastParams[i] = val;
        if (i >= VOLT_BTN1_PARAM && i <= VOLT_BTN4_PARAM) {
          if (val > lastVal && val > 0.5f) {
            transientPlayer.trigger(pressButtonGroup, 0.8f);
          }
        } else {
          if (val < lastVal) {
            transientPlayer.trigger(momSwitchDownGroup, 0.8f);
          } else {
            if (std::rand() % 2 == 0) {
              transientPlayer.trigger(latchSwitchGroup, 0.8f);
            } else {
              transientPlayer.trigger(momSwitchSnapGroup, 0.8f);
            }
          }
        }
      }
    }

    // Check jack connection changes
    for (int i = 0; i < NUM_INPUTS; ++i) {
      bool conn = inputs[i].isConnected();
      if (conn != lastInputConnected[i]) {
        lastInputConnected[i] = conn;
        if (conn) {
          transientPlayer.trigger(plugJackInGroup, 0.8f);
        } else {
          transientPlayer.trigger(plugJackOutGroup, 0.8f);
        }
      }
    }
    for (int i = 0; i < NUM_OUTPUTS; ++i) {
      bool conn = outputs[i].isConnected();
      if (conn != lastOutputConnected[i]) {
        lastOutputConnected[i] = conn;
        if (conn) {
          transientPlayer.trigger(plugJackInGroup, 0.8f);
        } else {
          transientPlayer.trigger(plugJackOutGroup, 0.8f);
        }
      }
    }
  }
};

// ============================================================
// UI WIDGET DEFINITIONS
// ============================================================

// --- Knobs ---
struct WorkshopMediumKnob : app::SvgKnob {
  WorkshopMediumKnob() {
    minAngle = -0.83f * (float)M_PI;
    maxAngle = 0.83f * (float)M_PI;
    shadow->opacity = 0.0f;
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/mediumKnob_dark.svg")));
  }
};

// --- 3-Way Toggle Switch: standard latching (Shape switches) ---
struct WorkshopSlopesToggleSwitch : app::SvgSwitch {
  WorkshopSlopesToggleSwitch() {
    momentary = false;
    shadow->opacity = 0.0;
    // Reordered frames: DOWN = Frame 0, CENTER = Frame 1, UP = Frame 2.
    addFrame(Svg::load(
        asset::plugin(pluginInstance, "res/switch_down.svg"))); // DOWN
    addFrame(Svg::load(
        asset::plugin(pluginInstance, "res/switch_middle.svg"))); // CENTER
    addFrame(
        Svg::load(asset::plugin(pluginInstance, "res/switch_up.svg"))); // UP
    box.size = Vec(24.f, 28.f);
  }

  void draw(const DrawArgs &args) override {
    // SVG is 24×28 px; box is 36×42 px.
    // Translate so the SVG renders centered within the larger hit area.
    float svgW = 24.f, svgH = 28.f;
    float offX = (box.size.x - svgW) / 2.f;
    float offY = (box.size.y - svgH) / 2.f;
    nvgSave(args.vg);
    nvgTranslate(args.vg, offX, offY);
    // Scale 1.125x (25% smaller than 1.5x)
    float scale = 1.125f;
    nvgTranslate(args.vg, svgW / 2.f * (1.f - scale),
                 svgH / 2.f * (1.f - scale));
    nvgScale(args.vg, scale, scale);
    SvgSwitch::draw(args);
    nvgRestore(args.vg);
  }
};

// --- 3-Way Toggle Switch: momentary DOWN (Computer Z, Slopes Mode) ---
struct WorkshopToggleSwitchMomentary : app::SvgSwitch {
  WorkshopToggleSwitchMomentary() {
    momentary = false;
    shadow->opacity = 0.0;
    // Reordered frames: DOWN = Frame 0, CENTER = Frame 1, UP = Frame 2.
    addFrame(Svg::load(
        asset::plugin(pluginInstance, "res/switch_down.svg"))); // DOWN
    addFrame(Svg::load(
        asset::plugin(pluginInstance, "res/switch_middle.svg"))); // CENTER
    addFrame(
        Svg::load(asset::plugin(pluginInstance, "res/switch_up.svg"))); // UP
    box.size = Vec(24.f, 28.f);
  }

  void onButton(const event::Button &e) override {
    if (e.button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    auto *pq = getParamQuantity();
    if (!pq)
      return;

    float midY = box.size.y / 2.f;

    if (e.action == GLFW_PRESS) {
      if (e.pos.y < midY) {
        // UP zone: latch UP (value 2.f)
        if (pq->getValue() == 2.f) {
          pq->setValue(1.f); // already UP -> return to CENTER
        } else {
          pq->setValue(2.f); // latch UP
        }
      } else {
        // DOWN zone: momentary DOWN (value 0.f)
        pq->setValue(0.f);
      }
      e.consume(this);
    } else if (e.action == GLFW_RELEASE) {
      // If it is in DOWN position (0.f), release it back to CENTER (1.f)
      if (pq->getValue() == 0.f) {
        pq->setValue(1.f);
      }
      e.consume(this);
    }
  }

  // Suppress VCV's default drag so it doesn't fight custom logic
  void onDragStart(const DragStartEvent &e) override {}
  void onDragEnd(const DragEndEvent &e) override {}
  void onDragMove(const DragMoveEvent &e) override {}

  void draw(const DrawArgs &args) override {
    // SVG is 24×28 px; box is 36×42 px.
    // Translate so the SVG renders centered within the larger hit area.
    float svgW = 24.f, svgH = 28.f;
    float offX = (box.size.x - svgW) / 2.f;
    float offY = (box.size.y - svgH) / 2.f;
    nvgSave(args.vg);
    nvgTranslate(args.vg, offX, offY);
    // Scale 1.125x (25% smaller than 1.5x)
    float scale = 1.125f;
    nvgTranslate(args.vg, svgW / 2.f * (1.f - scale),
                 svgH / 2.f * (1.f - scale));
    nvgScale(args.vg, scale, scale);
    SvgSwitch::draw(args);
    nvgRestore(args.vg);
  }
};

// --- 2-Way Toggle Switch: vertical bat lever (Filter HP/BP) ---
// Uses the same bat lever SVGs as the 3-way switch for visual consistency.
// Frame 0 (value 0) = UP = Highpass, Frame 1 (value 1) = DOWN = Bandpass
struct WorkshopToggleSwitch2way : app::SvgSwitch {
  WorkshopToggleSwitch2way() {
    momentary = false;
    shadow->opacity = 0.0;
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/switch_up.svg")));
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/switch_down.svg")));
    box.size = Vec(24.f, 28.f);
  }

  void draw(const DrawArgs &args) override {
    // SVG is 24×28 px; box is 36×42 px.
    // Translate so the SVG renders centered within the larger hit area.
    float svgW = 24.f, svgH = 28.f;
    float offX = (box.size.x - svgW) / 2.f;
    float offY = (box.size.y - svgH) / 2.f;
    nvgSave(args.vg);
    nvgTranslate(args.vg, offX, offY);
    // Scale 1.125x (25% smaller than 1.5x)
    float scale = 1.125f;
    nvgTranslate(args.vg, svgW / 2.f * (1.f - scale),
                 svgH / 2.f * (1.f - scale));
    nvgScale(args.vg, scale, scale);
    SvgSwitch::draw(args);
    nvgRestore(args.vg);
  }
};

// --- 2-Way Toggle Switch: horizontal bat lever (Amplifier Clean/LoFi) ---
// Visually rotated 90° CCW. Left = Clean (0), Right = LoFi (1).
struct WorkshopToggleSwitch2wayH : app::SvgSwitch {
  WorkshopToggleSwitch2wayH() {
    momentary = false;
    shadow->opacity = 0.0;
    addFrame(Svg::load(asset::plugin(
        pluginInstance,
        "res/switch_down.svg"))); // 0 = left (after -90 deg rotation)
    addFrame(Svg::load(asset::plugin(
        pluginInstance,
        "res/switch_up.svg"))); // 1 = right (after -90 deg rotation)
    box.size = Vec(56.f, 28.f); // Horizontal box size
    if (fb) {
      fb->visible = false; // Hide automatic vertical draw child
    }
  }

  void draw(const DrawArgs &args) override {
    auto *pq = getParamQuantity();
    int frameIdx = 0;
    if (pq) {
      frameIdx = (int)std::round(pq->getValue());
      if (frameIdx < 0)
        frameIdx = 0;
      if (frameIdx >= (int)frames.size())
        frameIdx = (int)frames.size() - 1;
    }

    if (frameIdx < (int)frames.size() && frames[frameIdx]) {
      nvgSave(args.vg);
      nvgTranslate(args.vg, box.size.x / 2.f, box.size.y / 2.f);
      nvgRotate(args.vg, -(float)M_PI / 2.f);

      float scale = 1.125f;
      nvgScale(args.vg, scale, scale);

      auto svg = frames[frameIdx];
      math::Vec svgSize = svg->getSize();
      nvgTranslate(args.vg, -svgSize.x / 2.f, -svgSize.y / 2.f);

      svg->draw(args.vg);
      nvgRestore(args.vg);
    }
  }

  void onButton(const event::Button &e) override {
    if (e.button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    auto *pq = getParamQuantity();
    if (!pq)
      return;
    if (e.action == GLFW_PRESS) {
      pq->setValue(pq->getValue() > 0.5f ? 0.f : 1.f);
      e.consume(this);
    }
  }
  void onDragStart(const DragStartEvent &e) override {}
  void onDragEnd(const DragEndEvent &e) override {}
  void onDragMove(const DragMoveEvent &e) override {}
};

// --- Voltage Button: radio exclusivity + shift multi-latch ---
// Normal click: activates only this button (radio group).
// Shift+click: toggles independently (multi-latch).
// State is stored in module->voltBtnStates[], not in param value.
struct VoltButton : ParamWidget {
  int myIndex = 0;

  VoltButton() { box.size = Vec(26.f, 26.f); }

  void draw(const DrawArgs &args) override {
    auto *m = dynamic_cast<WorkshopSystem *>(module);
    if (m && m->voltBtnStates[myIndex]) {
      nvgSave(args.vg);
      nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

      float cx = box.size.x * 0.5f;
      float cy = box.size.y * 0.5f;
      float r = 12.f; // Radius matching SVG circle

      // Outer radial glow
      NVGpaint glow =
          nvgRadialGradient(args.vg, cx, cy, r * 0.4f, r * 1.5f,
                            nvgRGBA(255, 0, 0, 220), nvgRGBA(255, 0, 0, 0));
      nvgBeginPath(args.vg);
      nvgCircle(args.vg, cx, cy, r * 1.5f);
      nvgFillPaint(args.vg, glow);
      nvgFill(args.vg);

      // Semi-transparent button overlay
      nvgBeginPath(args.vg);
      nvgCircle(args.vg, cx, cy, r);
      nvgFillColor(args.vg, nvgRGBA(255, 0, 0, 120));
      nvgFill(args.vg);

      nvgRestore(args.vg);
    }
  }

  void onButton(const event::Button &e) override {
    if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
      INFO("VoltButton %d clicked, mods: %d", myIndex, e.mods);
      auto *m = dynamic_cast<WorkshopSystem *>(module);
      if (m) {
        bool shiftHeld = (e.mods & GLFW_MOD_SHIFT) != 0;
        m->handleVoltButton(myIndex, shiftHeld);
        e.consume(this);
        return;
      }
    }
  }
};

struct AudioMenuHotspot : widget::OpaqueWidget {
  audio::Port *port = nullptr;
  std::string label;

  void draw(const DrawArgs &args) override {
    // Transparent hit area. The printed button lives in the panel SVG.
  }

  void onButton(const event::Button &e) override {
    if (e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS || !port)
      return;

    Menu *menu = createMenu();
    menu->addChild(createMenuLabel(label));
    app::appendAudioMenu(menu, port);
    e.consume(this);
  }
};

struct AudioButtonGlow : Widget {
  audio::Port *port = nullptr;
  NVGcolor color = nvgRGB(251, 191, 36);

  void draw(const DrawArgs &args) override {
    if (!port || !port->getDevice())
      return;

    nvgSave(args.vg);
    nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);

    float cx = box.size.x * 0.5f;
    float cy = box.size.y * 0.5f;
    float r = std::min(box.size.x, box.size.y) * 0.45f;
    NVGpaint glow =
        nvgRadialGradient(args.vg, cx, cy, r * 0.15f, r * 1.35f,
                          nvgRGBA(251, 191, 36, 180), nvgRGBA(251, 191, 36, 0));
    nvgBeginPath(args.vg);
    nvgCircle(args.vg, cx, cy, r * 1.35f);
    nvgFillPaint(args.vg, glow);
    nvgFill(args.vg);

    nvgBeginPath(args.vg);
    nvgCircle(args.vg, cx, cy, r * 0.62f);
    nvgFillColor(args.vg, color);
    nvgFill(args.vg);
    nvgRestore(args.vg);
  }
};

struct WorkshopAudioInputPort : PJ301MPort {
  void appendContextMenu(ui::Menu *menu) override {
    PJ301MPort::appendContextMenu(menu);

    WorkshopSystem *m = dynamic_cast<WorkshopSystem *>(module);
    if (!m)
      return;

    menu->addChild(new ui::MenuSeparator());
    menu->addChild(createMenuLabel("USB Audio Input Device"));
    app::appendAudioMenu(menu, &m->stereoDeviceInput);
  }
};

struct WorkshopAudioOutputPort : PJ301MPort {
  void appendContextMenu(ui::Menu *menu) override {
    PJ301MPort::appendContextMenu(menu);

    WorkshopSystem *m = dynamic_cast<WorkshopSystem *>(module);
    if (!m)
      return;

    menu->addChild(new ui::MenuSeparator());
    menu->addChild(createMenuLabel("USB Audio Output Device"));
    app::appendAudioMenu(menu, &m->stereoDeviceOutput);
  }
};

// --- Custom widgets and slots are loaded from shared/ComputerWidgets.hpp ---

// ============================================================
// MODULE WIDGET
// ============================================================

struct WorkshopSystemWidget : ModuleWidget {
  WorkshopSystemWidget(WorkshopSystem *module) {
    setModule(module);

    // Load panel SVG (604×365 px viewBox)
    setPanel(
        createPanel(asset::plugin(pluginInstance, "res/WorkshopSystem.svg")));

    // Screws at four corners
    addChild(createWidget<ScrewBlack>(Vec(0.f, 0.f)));
    addChild(createWidget<ScrewBlack>(Vec(0.f, 365.f)));
    addChild(createWidget<ScrewBlack>(Vec(615.f, 0.f)));
    addChild(createWidget<ScrewBlack>(Vec(615.f, 365.f)));

    // Coordinate helper: percentage → pixel (panel is 604×365 px)
    // Y axis uses 365.f to match the SVG viewBox height exactly.
    auto getVec = [](float xPct, float yPct) -> Vec {
      constexpr float w = 604.f;
      constexpr float h = 365.f;
      float x = (xPct / 100.f) * w;
      float y = (yPct / 100.f) * h;
      return Vec(x * 1.04109589f + 0.589f, y * 1.04109589f);
    };

    // Computer-section coordinate helper — uses the SAME scale/offset as the
    // standalone WorkshopComputer module so the two panels align perfectly
    // when placed side-by-side.  Takes raw design-space pixels, not percentages.
    auto getVecComp = [](float x, float y) -> Vec {
      return Vec(x * 1.034483f + 2.17862f, y * 1.027774f + 1.97964f);
    };

    // --------------------------------------------------------
    // KNOBS
    // --------------------------------------------------------
    // Computer — raw coords match standalone WorkshopComputer exactly
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVecComp(17.962f, 118.715f), module, WorkshopSystem::COMPUTER_X_PARAM));
    addParam(createParamCentered<WorkshopLargeKnob>(
        getVecComp(55.881f, 54.847f), module, WorkshopSystem::COMPUTER_MAIN_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVecComp(55.691f, 118.431f), module, WorkshopSystem::COMPUTER_Y_PARAM));

    // Oscillator 1
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(22.643f, 32.592f), module, WorkshopSystem::OSC1_FINE_PARAM));
    addParam(createParamCentered<WorkshopLargeKnob>(
        getVec(28.415f, 15.355f), module, WorkshopSystem::OSC1_FREQ_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(34.118f, 32.592f), module, WorkshopSystem::OSC1_FM_PARAM));

    // Oscillator 2
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(22.643f, 70.160f), module, WorkshopSystem::OSC2_FINE_PARAM));
    addParam(createParamCentered<WorkshopLargeKnob>(
        getVec(28.415f, 87.374f), module, WorkshopSystem::OSC2_FREQ_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(34.118f, 70.160f), module, WorkshopSystem::OSC2_FM_PARAM));

    // Slopes
    addParam(createParamCentered<WorkshopMediumKnob>(
        getVec(78.894f, 13.400f), module, WorkshopSystem::SLOPES1_RATE_PARAM));
    addParam(createParamCentered<WorkshopMediumKnob>(
        getVec(78.755f, 89.069f), module, WorkshopSystem::SLOPES2_RATE_PARAM));

    // Filter 1
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(60.087f, 32.592f), module, WorkshopSystem::FILTER1_FM_PARAM));
    addParam(createParamCentered<WorkshopLargeKnob>(
        getVec(66.295f, 15.355f), module, WorkshopSystem::FILTER1_CUTOFF_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(72.509f, 32.606f), module, WorkshopSystem::FILTER1_RES_PARAM));

    // Filter 2
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(60.110f, 70.173f), module, WorkshopSystem::FILTER2_FM_PARAM));
    addParam(createParamCentered<WorkshopLargeKnob>(
        getVec(66.295f, 87.374f), module, WorkshopSystem::FILTER2_CUTOFF_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(72.533f, 70.188f), module, WorkshopSystem::FILTER2_RES_PARAM));

    // Amplifier / Voltages / Stomp
    addParam(createParamCentered<WorkshopMediumKnob>(
        getVec(51.672f, 16.115f), module, WorkshopSystem::AMP_GAIN_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(52.284f, 67.233f), module, WorkshopSystem::VOLT_BLEND_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(40.258f, 75.941f), module, WorkshopSystem::STOMP_FEEDBACK_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(44.444f, 67.233f), module, WorkshopSystem::STOMP_BLEND_PARAM));

    // Mixer
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(88.750f, 13.448f), module, WorkshopSystem::MIX_CH1_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(88.750f, 23.410f), module, WorkshopSystem::MIX_CH2_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(88.750f, 33.564f), module, WorkshopSystem::MIX_CH3_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(96.411f, 33.564f), module, WorkshopSystem::MIX_CH4_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(96.411f, 13.448f), module, WorkshopSystem::MIX_PAN1_PARAM));
    addParam(createParamCentered<WorkshopSmallKnob>(
        getVec(96.411f, 23.410f), module, WorkshopSystem::MIX_PAN2_PARAM));
    addParam(createParamCentered<WorkshopLargeKnob>(
        getVec(92.436f, 87.364f), module, WorkshopSystem::MIX_MAIN_PARAM));

    // --------------------------------------------------------
    // JACKS — Computer  (raw coords match standalone WorkshopComputer exactly)
    // --------------------------------------------------------
    addInput(createInputCentered<WorkshopComputerPort>(
        getVecComp(12.409f, 167.873f), module, WorkshopSystem::COMPUTER_AUDIO1_IN));
    addInput(createInputCentered<WorkshopComputerPort>(
        getVecComp(12.409f, 207.473f), module, WorkshopSystem::COMPUTER_CV1_IN));
    addInput(createInputCentered<WorkshopComputerPort>(
        getVecComp(41.209f, 207.473f), module, WorkshopSystem::COMPUTER_CV2_IN));
    addInput(createInputCentered<WorkshopComputerPort>(
        getVecComp(12.409f, 247.130f), module, WorkshopSystem::COMPUTER_PULSE1_IN));
    addInput(createInputCentered<WorkshopComputerPort>(
        getVecComp(41.209f, 167.873f), module, WorkshopSystem::COMPUTER_AUDIO2_IN));
    addInput(createInputCentered<WorkshopComputerPort>(
        getVecComp(41.205f, 247.035f), module, WorkshopSystem::COMPUTER_PULSE2_IN));

    addOutput(createOutputCentered<WorkshopComputerPort>(
        getVecComp(70.008f, 167.873f), module, WorkshopSystem::COMPUTER_AUDIO1_OUT));
    addOutput(createOutputCentered<WorkshopComputerPort>(
        getVecComp(70.008f, 207.473f), module, WorkshopSystem::COMPUTER_CV1_OUT));
    addOutput(createOutputCentered<WorkshopComputerPort>(
        getVecComp(70.086f, 247.054f), module, WorkshopSystem::COMPUTER_PULSE1_OUT));
    addOutput(createOutputCentered<WorkshopComputerPort>(
        getVecComp(98.808f, 167.873f), module, WorkshopSystem::COMPUTER_AUDIO2_OUT));
    addOutput(createOutputCentered<WorkshopComputerPort>(
        getVecComp(98.808f, 207.473f), module, WorkshopSystem::COMPUTER_CV2_OUT));
    addOutput(createOutputCentered<WorkshopComputerPort>(
        getVecComp(98.854f, 246.997f), module, WorkshopSystem::COMPUTER_PULSE2_OUT));

    // Oscillators
    addInput(createInputCentered<PJ301MPort>(getVec(21.40f, 45.926f), module,
                                             WorkshopSystem::OSC1_PITCH_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(21.40f, 56.637f), module,
                                             WorkshopSystem::OSC2_PITCH_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(26.07f, 45.926f), module,
                                             WorkshopSystem::OSC1_FM_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(26.09f, 56.637f), module,
                                             WorkshopSystem::OSC2_FM_IN));
    addOutput(createOutputCentered<PJ301MPort>(getVec(30.87f, 45.926f), module,
                                               WorkshopSystem::OSC1_SQR_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(30.87f, 56.637f), module,
                                               WorkshopSystem::OSC2_SQR_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(35.56f, 45.926f), module,
                                               WorkshopSystem::OSC1_SIN_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(35.59f, 56.637f), module,
                                               WorkshopSystem::OSC2_SIN_OUT));

    // Stereo In / Out
    addInput(createInputCentered<WorkshopAudioInputPort>(
        getVec(40.33f, 13.58f), module, WorkshopSystem::STEREO_IN_JACK));
    addOutput(createOutputCentered<PJ301MPort>(getVec(45.10f, 13.58f), module,
                                               WorkshopSystem::STEREO_L_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(45.10f, 24.60f), module,
                                               WorkshopSystem::STEREO_R_OUT));

    // Ring Mod
    addInput(createInputCentered<PJ301MPort>(getVec(40.43f, 35.15f), module,
                                             WorkshopSystem::RING_IN1));
    addInput(createInputCentered<PJ301MPort>(getVec(40.43f, 45.926f), module,
                                             WorkshopSystem::RING_IN2));
    addOutput(createOutputCentered<PJ301MPort>(getVec(45.10f, 45.926f), module,
                                               WorkshopSystem::RING_OUT));

    // Stompbox
    addInput(createInputCentered<PJ301MPort>(getVec(40.38f, 56.637f), module,
                                             WorkshopSystem::STOMP_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(40.33f, 89.04f), module,
                                             WorkshopSystem::STOMP_RETURN));
    addOutput(createOutputCentered<PJ301MPort>(getVec(45.10f, 56.637f), module,
                                               WorkshopSystem::STOMP_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(45.03f, 89.04f), module,
                                               WorkshopSystem::STOMP_SEND));

    // Amplifier
    addInput(createInputCentered<PJ301MPort>(getVec(49.79f, 35.15f), module,
                                             WorkshopSystem::AMP_IN));
    addOutput(createOutputCentered<PJ301MPort>(getVec(54.56f, 35.15f), module,
                                               WorkshopSystem::AMP_OUT));

    // Voltages
    addOutput(createOutputCentered<PJ301MPort>(getVec(49.84f, 45.926f), module,
                                               WorkshopSystem::VOLT1_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(54.55f, 45.926f), module,
                                               WorkshopSystem::VOLT2_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(49.89f, 56.637f), module,
                                               WorkshopSystem::VOLT3_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(54.56f, 56.637f), module,
                                               WorkshopSystem::VOLT4_OUT));

    // Filters
    addInput(createInputCentered<PJ301MPort>(getVec(59.25f, 45.926f), module,
                                             WorkshopSystem::FILTER1_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(59.25f, 56.637f), module,
                                             WorkshopSystem::FILTER2_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(63.97f, 45.926f), module,
                                             WorkshopSystem::FILTER1_FM_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(64.02f, 56.637f), module,
                                             WorkshopSystem::FILTER2_FM_IN));

    addOutput(createOutputCentered<PJ301MPort>(getVec(68.79f, 45.926f), module,
                                               WorkshopSystem::FILTER1_HP_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(68.79f, 56.637f), module,
                                               WorkshopSystem::FILTER2_HP_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(73.49f, 45.926f), module,
                                               WorkshopSystem::FILTER1_LP_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(73.50f, 56.637f), module,
                                               WorkshopSystem::FILTER2_LP_OUT));

    // Slopes
    addInput(createInputCentered<PJ301MPort>(getVec(78.28f, 45.926f), module,
                                             WorkshopSystem::SLOPES1_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(78.25f, 35.15f), module,
                                             WorkshopSystem::SLOPES1_CV_IN));
    addOutput(createOutputCentered<PJ301MPort>(getVec(82.95f, 45.926f), module,
                                               WorkshopSystem::SLOPES1_OUT));

    addInput(createInputCentered<PJ301MPort>(getVec(78.28f, 56.637f), module,
                                             WorkshopSystem::SLOPES2_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(82.92f, 67.361f), module,
                                             WorkshopSystem::SLOPES2_CV_IN));
    addOutput(createOutputCentered<PJ301MPort>(getVec(82.95f, 56.637f), module,
                                               WorkshopSystem::SLOPES2_OUT));

    // Mixer
    addInput(createInputCentered<PJ301MPort>(getVec(87.72f, 45.926f), module,
                                             WorkshopSystem::MIXER1_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(92.42f, 45.926f), module,
                                             WorkshopSystem::MIXER2_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(87.72f, 56.637f), module,
                                             WorkshopSystem::MIXER3_IN));
    addInput(createInputCentered<PJ301MPort>(getVec(92.47f, 56.637f), module,
                                             WorkshopSystem::MIXER4_IN));

    addOutput(createOutputCentered<PJ301MPort>(getVec(97.23f, 45.926f), module,
                                               WorkshopSystem::MIXER_L_OUT));
    addOutput(createOutputCentered<PJ301MPort>(getVec(97.12f, 56.637f), module,
                                               WorkshopSystem::MIXER_R_OUT));
    addOutput(createOutputCentered<WorkshopAudioOutputPort>(
        getVec(92.52f, 67.361f), module, WorkshopSystem::PHONES1_OUT));
    addOutput(createOutputCentered<WorkshopAudioOutputPort>(
        getVec(97.21f, 67.361f), module, WorkshopSystem::PHONES2_OUT));

    // --------------------------------------------------------
    // SWITCHES
    // --------------------------------------------------------
    // Computer Z: momentary-down (flick = pulse, hold = latch)
    // Use same raw position as standalone WorkshopComputer (Vec, not getVecComp,
    // to match WorkshopComputer which places the switch directly without transform)
    if (module) {
      WorkshopToggleSwitch* sw = createParamCentered<WorkshopToggleSwitch>(
          getVecComp(93.365f, 115.122f), module,
          WorkshopSystem::COMPUTER_SWITCH_PARAM);
      sw->setComputerModule(module);
      addParam(sw);
    } else {
      addParam(createParamCentered<WorkshopToggleSwitch>(
          getVecComp(93.365f, 115.122f), module,
          WorkshopSystem::COMPUTER_SWITCH_PARAM));
    }

    // Amplifier mode: horizontal switch (LoFi=left, Clean=right)
    addParam(createParamCentered<WorkshopToggleSwitch2wayH>(
        getVec(52.105f, 27.021f), module, WorkshopSystem::AMP_SWITCH_PARAM));

    // Filter HP/BP switches: vertical bat lever
    addParam(createParamCentered<WorkshopToggleSwitch2way>(
        getVec(66.347f, 32.521f), module,
        WorkshopSystem::FILTER1_SWITCH_PARAM));
    addParam(createParamCentered<WorkshopToggleSwitch2way>(
        getVec(66.359f, 70.127f), module,
        WorkshopSystem::FILTER2_SWITCH_PARAM));

    // Slopes Shape: standard latching 3-way (FastRise / Both / FastFall)
    addParam(createParamCentered<WorkshopSlopesToggleSwitch>(
        getVec(77.948f, 24.809f), module, WorkshopSystem::SLOPES1_SHAPE_PARAM));
    addParam(createParamCentered<WorkshopSlopesToggleSwitch>(
        getVec(77.946f, 78.017f), module, WorkshopSystem::SLOPES2_SHAPE_PARAM));

    // Slopes Mode: momentary-down (Loop=flick, Slew=center, Gate=latch up)
    addParam(createParamCentered<WorkshopToggleSwitchMomentary>(
        getVec(83.266f, 24.757f), module, WorkshopSystem::SLOPES1_MODE_PARAM));
    addParam(createParamCentered<WorkshopToggleSwitchMomentary>(
        getVec(83.273f, 78.009f), module, WorkshopSystem::SLOPES2_MODE_PARAM));

    // --------------------------------------------------------
    // VOLTAGE BUTTONS (radio group, shift = multi-latch)
    // --------------------------------------------------------
    static const float voltBtnX[4] = {50.271f, 55.923f, 50.273f, 55.924f};
    static const float voltBtnY[4] = {79.366f, 79.419f, 88.868f, 88.921f};
    for (int i = 0; i < 4; i++) {
      VoltButton *btn = createParamCentered<VoltButton>(
          getVec(voltBtnX[i], voltBtnY[i]), module,
          WorkshopSystem::VOLT_BTN1_PARAM + i);
      btn->myIndex = i;
      addParam(btn);
    }

    // --------------------------------------------------------
    // LED LIGHTS
    // --------------------------------------------------------
    // Amplifier VU meter (4 LEDs, red)
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(49.9f, 10.5f), module, WorkshopSystem::AMP_LED1));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(52.4f, 10.13f), module, WorkshopSystem::AMP_LED2));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(54.3f, 11.9f), module, WorkshopSystem::AMP_LED3));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(55.2f, 15.3f), module, WorkshopSystem::AMP_LED4));

    // Computer LEDs (6, red — raw coords match standalone WorkshopComputer exactly)
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVecComp(6.714f, 309.281f), module, WorkshopSystem::COMP_LED0));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVecComp(19.952f, 309.309f), module, WorkshopSystem::COMP_LED1));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVecComp(6.686f, 322.263f), module, WorkshopSystem::COMP_LED2));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVecComp(19.952f, 322.235f), module, WorkshopSystem::COMP_LED3));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVecComp(6.714f, 335.189f), module, WorkshopSystem::COMP_LED4));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVecComp(19.924f, 335.189f), module, WorkshopSystem::COMP_LED5));

    // Slopes rise/fall LEDs (red)
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(80.857f, 22.658f), module, WorkshopSystem::SLOPES1_RISE_LED));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(80.857f, 26.658f), module, WorkshopSystem::SLOPES1_FALL_LED));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(80.850f, 76.184f), module, WorkshopSystem::SLOPES2_RISE_LED));
    addChild(createLightCentered<MediumLight<RedLight>>(
        getVec(80.850f, 80.184f), module, WorkshopSystem::SLOPES2_FALL_LED));

    // --------------------------------------------------------
    // PROGRAM CARD DISPLAY
    // --------------------------------------------------------
    ProgramCardWidget *cardWidget = new ProgramCardWidget();
    // Match WorkshopComputer card slot position/size exactly
    cardWidget->box.pos  = Vec(43.304f, 286.529f);
    cardWidget->box.size = Vec(37.155f, 55.04f);
    cardWidget->setComputerModule(module);
    addChild(cardWidget);

    // Virtual Reset Button — match WorkshopComputer position exactly
    if (module) {
      WorkshopResetButton *resetButton = new WorkshopResetButton();
      resetButton->box.pos  = getVecComp(94.043f, 327.133f);
      resetButton->box.size = Vec(11.338f * 1.019277f, 11.338f * 1.019277f);
      resetButton->setComputerModule(module);
      addChild(resetButton);
    }
  }

  void step() override {
    ModuleWidget::step();
    WorkshopSystem *m = dynamic_cast<WorkshopSystem *>(module);
    if (m && APP && APP->scene && APP->scene->rack) {
      m->isOsc1SelfPatched = false;
      m->isOsc2SelfPatched = false;

      // Detect self-patch cables for VCO feedback (zero-latency feedback path)
      std::vector<app::CableWidget *> completeCables =
          APP->scene->rack->getCompleteCables();
      for (app::CableWidget *cw : completeCables) {
        if (cw && cw->inputPort && cw->outputPort) {
          if (cw->inputPort->module == module &&
              cw->outputPort->module == module) {
            if (cw->inputPort->portId == WorkshopSystem::OSC1_FM_IN &&
                cw->outputPort->portId == WorkshopSystem::OSC1_SIN_OUT) {
              m->isOsc1SelfPatched = true;
            }
            if (cw->inputPort->portId == WorkshopSystem::OSC2_FM_IN &&
                cw->outputPort->portId == WorkshopSystem::OSC2_SIN_OUT) {
              m->isOsc2SelfPatched = true;
            }
          }
        }
      }

      // Track incomplete cables to trigger plug click on initiation/drop on our ports
      int currentIncompleteCount = 0;
      std::vector<app::CableWidget *> incompleteCables =
          APP->scene->rack->getIncompleteCables();
      for (app::CableWidget *cw : incompleteCables) {
        if (cw) {
          if ((cw->inputPort && cw->inputPort->module == module) ||
              (cw->outputPort && cw->outputPort->module == module)) {
            currentIncompleteCount++;
          }
        }
      }
      if (currentIncompleteCount != lastIncompleteCount) {
        if (currentIncompleteCount > lastIncompleteCount) {
          m->transientPlayer.trigger(plugJackInGroup, 0.8f);
        } else {
          m->transientPlayer.trigger(plugJackOutGroup, 0.8f);
        }
        lastIncompleteCount = currentIncompleteCount;
      }
    }
  }

  void onButton(const event::Button &e) override {
    bool alreadyConsumed = e.isConsumed();
    ModuleWidget::onButton(e);
    if (alreadyConsumed || e.button != GLFW_MOUSE_BUTTON_LEFT)
      return;

    WorkshopSystem *m = dynamic_cast<WorkshopSystem *>(module);
    if (!m)
      return;

    if (e.action == GLFW_PRESS) {
      float zoom = getAbsoluteZoom();
      math::Vec localPos = APP->scene->getMousePos() - getAbsoluteOffset(math::Vec(0, 0));
      if (zoom > 0.0f) {
        localPos = localPos / zoom;
      }
      m->handlePanelTap(localPos.x, localPos.y);
    }
  }

  void onDragMove(const event::DragMove &e) override {
    ModuleWidget::onDragMove(e);
    WorkshopSystem *m = dynamic_cast<WorkshopSystem *>(module);
    if (m) {
      float zoom = getAbsoluteZoom();
      math::Vec localPos = APP->scene->getMousePos() - getAbsoluteOffset(math::Vec(0, 0));
      if (zoom > 0.0f) {
        localPos = localPos / zoom;
      }
      m->handlePanelScratch(localPos.x, localPos.y, e.mouseDelta.x, e.mouseDelta.y);
    }
  }

  void appendContextMenu(Menu *menu) override {
    ModuleWidget::appendContextMenu(menu);

    WorkshopSystem *m = dynamic_cast<WorkshopSystem *>(module);
    if (!m)
      return;

    menu->addChild(new MenuSeparator());
    menu->addChild(createMenuLabel("USB Audio Device Configuration"));
    menu->addChild(
        createMenuLabel("- Right-click Stereo In jack to select Input Device"));
    menu->addChild(
        createMenuLabel("- Right-click Phones jacks to select Output Device"));

    // Humpback Filter and Enable Computer toggles removed per user request.

    if (m->internalComputerEnabled) {
      if (m->activeCardNeedsMidi()) {
        menu->addChild(new MenuSeparator());

        struct MidiSubmenuItem : MenuItem {
          WorkshopSystem* module;
          Menu* createChildMenu() override {
            Menu* menu = new Menu();

            struct MidiInputSubmenuItem : MenuItem {
              WorkshopSystem* module;
              Menu* createChildMenu() override {
                Menu* menu = new Menu();
                appendMidiMenu(menu, &module->midiInput);
                return menu;
              }
            };
            MidiInputSubmenuItem* midiInMenu = new MidiInputSubmenuItem();
            midiInMenu->text = "MIDI Input Settings";
            midiInMenu->module = module;
            midiInMenu->rightText = "➔";
            menu->addChild(midiInMenu);

            struct MidiOutputSubmenuItem : MenuItem {
              WorkshopSystem* module;
              Menu* createChildMenu() override {
                Menu* menu = new Menu();
                appendMidiMenu(menu, &module->midiOutput);
                return menu;
              }
            };
            MidiOutputSubmenuItem* midiOutMenu = new MidiOutputSubmenuItem();
            midiOutMenu->text = "MIDI Output Settings";
            midiOutMenu->module = module;
            midiOutMenu->rightText = "➔";
            menu->addChild(midiOutMenu);

            return menu;
          }
        };
        MidiSubmenuItem* midiMenu = new MidiSubmenuItem();
        midiMenu->text = "Computer MIDI Settings";
        midiMenu->module = m;
        midiMenu->rightText = "➔";
        menu->addChild(midiMenu);
      }

      if (m->activeCardNeedsUsbAudio()) {
        menu->addChild(new MenuSeparator());

        struct UsbAudioInputSubmenuItem : MenuItem {
          WorkshopSystem* module;
          Menu* createChildMenu() override {
            Menu* menu = new Menu();
            app::appendAudioMenu(menu, &module->stereoDeviceInput);
            return menu;
          }
        };
        UsbAudioInputSubmenuItem* audioInMenu = new UsbAudioInputSubmenuItem();
        audioInMenu->text = "USB Audio Input Device Settings";
        audioInMenu->module = m;
        audioInMenu->rightText = "➔";
        menu->addChild(audioInMenu);

        struct UsbAudioOutputSubmenuItem : MenuItem {
          WorkshopSystem* module;
          Menu* createChildMenu() override {
            Menu* menu = new Menu();
            app::appendAudioMenu(menu, &module->stereoDeviceOutput);
            return menu;
          }
        };
        UsbAudioOutputSubmenuItem* audioOutMenu = new UsbAudioOutputSubmenuItem();
        audioOutMenu->text = "USB Audio Output Device Settings";
        audioOutMenu->module = m;
        audioOutMenu->rightText = "➔";
        menu->addChild(audioOutMenu);
      }

      if (m->activeCardNeedsGrid()) {
        m->ensure_monome_registered();
        if (MonomeBridge::get().is_available()) {
          menu->addChild(new MenuSeparator());

          struct MonomeGridSubmenuItem : MenuItem {
            WorkshopSystem* module;
            Menu* createChildMenu() override {
              Menu* menu = new Menu();
              auto grids = MonomeBridge::get().get_grids();
              if (grids.empty()) {
                menu->addChild(createMenuLabel("(no hardware or virtual grids found)"));
              } else {
                for (Grid* grid : grids) {
                  struct GridConnectItem : MenuItem {
                    WorkshopSystem* module;
                    Grid* grid;
                    void onAction(const event::Action& e) override {
                      if (module->connected_grid == grid) {
                        MonomeBridge::get().disconnect(module);
                      } else {
                        MonomeBridge::get().connect(grid, module);
                      }
                    }
                  };

                  GridConnectItem* item = new GridConnectItem();
                  item->module = module;
                  item->grid = grid;

                  std::string label = grid->getDevice().type + " (" + grid->getDevice().id + ")";
                  item->text = label;
                  if (module->connected_grid == grid) {
                    item->rightText = "✔";
                  }
                  menu->addChild(item);
                }
              }
              return menu;
            }
          };
          MonomeGridSubmenuItem* gridMenu = new MonomeGridSubmenuItem();
          gridMenu->text = "Monome Grid Settings";
          gridMenu->module = m;
          gridMenu->rightText = "➔";
          menu->addChild(gridMenu);
        }
      }

      // Open Card Manager context menu item
      if (!m->card_globals.active_card_id_str.empty()) {
        std::string active_id = m->card_globals.active_card_id_str;
        if (active_id == "krell" || active_id == "duo_midi") {
          active_id = "blackbird";
        }
        std::string manager_file = "";
        std::vector<std::string> candidates = {
            "index.html",
            "editor.html",
            active_id + "_manager.html",
            active_id + ".html"
        };

        bool found = false;
        for (const auto& candidate : candidates) {
          std::string path = asset::plugin(pluginInstance, "res/web/" + active_id + "/" + candidate);
          std::ifstream f(path);
          if (f.good()) {
            f.close();
            manager_file = candidate;
            found = true;
            break;
          }
          // Try under dist/
          std::string dist_path = asset::plugin(pluginInstance, "res/web/" + active_id + "/dist/" + candidate);
          std::ifstream f_dist(dist_path);
          if (f_dist.good()) {
            f_dist.close();
            manager_file = "dist/" + candidate;
            found = true;
            break;
          }
        }

        if (found) {
          menu->addChild(new MenuSeparator());

          struct OpenManagerItem : MenuItem {
            WorkshopSystem* module;
            std::string manager_file;
            void onAction(const event::Action& e) override {
              char url[512];
              snprintf(url, sizeof(url), "http://127.0.0.1:%d/%s/%s?instance=%p",
                       g_web_server_port, module->card_globals.active_card_id_str.c_str(), manager_file.c_str(), module);

#if defined(_WIN32)
              std::string cmd = "start " + std::string(url);
#elif defined(__APPLE__)
              std::string cmd = "open \"" + std::string(url) + "\"";
#else
              std::string cmd = "xdg-open \"" + std::string(url) + "\"";
#endif
              int ret = std::system(cmd.c_str());
              (void)ret;
            }
          };

          OpenManagerItem* openItem = new OpenManagerItem();
          openItem->text = "Open Card Manager...";
          openItem->module = m;
          openItem->manager_file = manager_file;
          menu->addChild(openItem);
        }
      }

      // Submenus for Left & Right utility pair channels
      if (m->activeCardIdx >= 0 && m->activeCardIdx < (int)g_card_registry.size() && g_card_registry[m->activeCardIdx].id == "utility_pair") {
        menu->addChild(new MenuSeparator());
        menu->addChild(createMenuLabel("Utility Pair Selection"));

        struct UtilitySubmenuItem : MenuItem {
          WorkshopSystem* module;
          int channel; // 0 = Left, 1 = Right

          Menu* createChildMenu() override {
            Menu* menu = new Menu();
            static const std::string UTILITIES[24] = {
                "Attenuverter", "Bernoulli Gate", "Bitcrusher", "Chords",
                "Chorus", "Clock Divider", "Cross Switch", "CV Mixer",
                "Delay", "Euclidean Rhythms", "Glitch", "Karplus-Strong",
                "Low Pass Gate", "Max/Rectify", "Quantiser", "Sample & Hold",
                "Slopes Plus", "Slow LFO", "Super Saw", "Turing 185",
                "VCA", "VCO", "Wavefolder", "Window Comparator"
            };

            struct UtilItem : MenuItem {
              WorkshopSystem* module;
              int channel;
              int index;
              void onAction(const event::Action& e) override {
                module->utility_indices[channel] = index;
                module->change_card(module->activeCardIdx); // Reload card to engage the new utility
              }
            };

            for (int i = 0; i < 24; i++) {
              UtilItem* item = new UtilItem();
              item->text = UTILITIES[i];
              item->module = module;
              item->channel = channel;
              item->index = i;
              item->rightText = (module->utility_indices[channel] == i) ? "✔" : "";
              menu->addChild(item);
            }
            return menu;
          }
        };

        UtilitySubmenuItem* leftItem = new UtilitySubmenuItem();
        leftItem->text = "Left Utility Channel";
        leftItem->module = m;
        leftItem->channel = 0;
        menu->addChild(leftItem);

        UtilitySubmenuItem* rightItem = new UtilitySubmenuItem();
        rightItem->text = "Right Utility Channel";
        rightItem->module = m;
        rightItem->channel = 1;
        menu->addChild(rightItem);
      }

      // Flash Memory actions
      menu->addChild(new MenuSeparator());
      menu->addChild(createMenuLabel("Flash Memory"));

      struct FlashLoadItem : MenuItem {
        WorkshopSystem* module;
        void onAction(const event::Action& e) override {
          osdialog_filters* filters = osdialog_filters_parse("Flash/SoundFont:uf2,bin,sf2");
          char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
          if (path) {
            module->load_file_to_flash(path);
            free(path);
          }
          osdialog_filters_free(filters);
        }
      };

      FlashLoadItem* loadFlashItem = new FlashLoadItem();
      loadFlashItem->text = "Load Flash File (.uf2 / .bin / .sf2) ...";
      loadFlashItem->module = m;
      menu->addChild(loadFlashItem);

      struct FlashClearItem : MenuItem {
        WorkshopSystem* module;
        void onAction(const event::Action& e) override {
          if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_YES_NO, "Are you sure you want to clear the simulated flash memory? This will erase all samples/settings loaded in flash.")) {
            memset(module->card_globals.g_flash_memory_val, 0xFF, PICO_FLASH_SIZE_BYTES);
            CardGlobals* old_instance = t_instance;
            t_instance = &module->card_globals;
            module->save_flash_to_disk();
            t_instance = old_instance;
          }
        }
      };

      FlashClearItem* clearFlashItem = new FlashClearItem();
      clearFlashItem->text = "Clear Flash Memory (Reset to 0xFF)";
      clearFlashItem->module = m;
      menu->addChild(clearFlashItem);
    }
  }

  int lastIncompleteCount = 0;
};

void push_midi_to_rx_queue(const uint8_t* msg_bytes, size_t msg_size) {
    if (msg_size == 0) return;
    
    if (msg_bytes[0] == 0xF0) {
        // SysEx
        size_t idx = 0;
        while (idx < msg_size) {
            size_t rem = msg_size - idx;
            uint8_t packet[4] = {0};
            if (rem >= 3) {
                if (msg_bytes[idx + 2] == 0xF7) {
                    packet[0] = 0x07; // SysEx ends with 3 bytes
                } else {
                    packet[0] = 0x04; // SysEx starts or continues
                }
                packet[1] = msg_bytes[idx];
                packet[2] = msg_bytes[idx+1];
                packet[3] = msg_bytes[idx+2];
                idx += 3;
            } else if (rem == 2) {
                packet[0] = 0x06; // SysEx ends with 2 bytes
                packet[1] = msg_bytes[idx];
                packet[2] = msg_bytes[idx+1];
                idx += 2;
            } else if (rem == 1) {
                packet[0] = 0x05; // SysEx ends with 1 byte
                packet[1] = msg_bytes[idx];
                idx += 1;
            }
            g_midi_rx_packet_queue.push(packet);
        }
    } else {
        // Non-SysEx
        uint8_t status = msg_bytes[0];
        uint8_t high_nibble = status & 0xF0;
        uint8_t packet[4] = {0};
        
        if (high_nibble == 0x80) { packet[0] = 0x08; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; packet[3] = msg_size > 2 ? msg_bytes[2] : 0; }
        else if (high_nibble == 0x90) { packet[0] = 0x09; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; packet[3] = msg_size > 2 ? msg_bytes[2] : 0; }
        else if (high_nibble == 0xA0) { packet[0] = 0x0A; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; packet[3] = msg_size > 2 ? msg_bytes[2] : 0; }
        else if (high_nibble == 0xB0) { packet[0] = 0x0B; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; packet[3] = msg_size > 2 ? msg_bytes[2] : 0; }
        else if (high_nibble == 0xC0) { packet[0] = 0x0C; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; }
        else if (high_nibble == 0xD0) { packet[0] = 0x0D; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; }
        else if (high_nibble == 0xE0) { packet[0] = 0x0E; packet[1] = status; packet[2] = msg_size > 1 ? msg_bytes[1] : 0; packet[3] = msg_size > 2 ? msg_bytes[2] : 0; }
        else {
            packet[0] = 0x0F;
            packet[1] = status;
            if (msg_size > 1) packet[2] = msg_bytes[1];
            if (msg_size > 2) packet[3] = msg_bytes[2];
        }
        g_midi_rx_packet_queue.push(packet);
    }
}

void handle_client(socket_t client_fd) {
    set_socket_timeout(client_fd, 5000);
    
    std::string request;
    char buf[4096];
    while (true) {
        int r = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0) {
            close_socket(client_fd);
            return;
        }
        buf[r] = '\0';
        request += buf;
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
        if (request.length() > 8192) {
            close_socket(client_fd);
            return;
        }
    }
    
    size_t req_line_end = request.find("\r\n");
    if (req_line_end == std::string::npos) {
        close_socket(client_fd);
        return;
    }
    std::string req_line = request.substr(0, req_line_end);
    
    std::stringstream ss(req_line);
    std::string method, full_path, protocol;
    ss >> method >> full_path >> protocol;
    
    if (method != "GET") {
        std::string resp = "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 18\r\n\r\nMethod Not Allowed";
        send(client_fd, resp.c_str(), resp.length(), 0);
        close_socket(client_fd);
        return;
    }
    
    std::string path = full_path;
    std::string query = "";
    size_t q_pos = full_path.find('?');
    if (q_pos != std::string::npos) {
        path = full_path.substr(0, q_pos);
        query = full_path.substr(q_pos + 1);
    }
    
    std::string upgrade_hdr = get_header_value(request, "Upgrade");
    std::transform(upgrade_hdr.begin(), upgrade_hdr.end(), upgrade_hdr.begin(), ::tolower);
    
    if (upgrade_hdr == "websocket") {
        std::string sec_key = get_header_value(request, "Sec-WebSocket-Key");
        if (sec_key.empty()) {
            std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nBad Request";
            send(client_fd, resp.c_str(), resp.length(), 0);
            close_socket(client_fd);
            return;
        }
        
        uint8_t sha1_res[20];
        sha1::calculate(sec_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", sha1_res);
        std::string accept_key = base64::encode(sha1_res, 20);
        
        std::string handshake = 
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";
        send(client_fd, handshake.c_str(), handshake.length(), 0);
        
        size_t inst_pos = query.find("instance=");
        std::string inst_str = "";
        if (inst_pos != std::string::npos) {
            size_t end_pos = query.find_first_of(" &", inst_pos + 9);
            inst_str = query.substr(inst_pos + 9, end_pos - (inst_pos + 9));
        }
        
        uintptr_t val = 0;
        try {
            if (!inst_str.empty()) {
                val = std::stoull(inst_str, nullptr, 16);
            }
        } catch(...) {}
        
        WorkshopSystem* module_ptr = reinterpret_cast<WorkshopSystem*>(val);
        
        {
            std::lock_guard<std::mutex> lock(g_instances_mutex);
            if (g_instances.find(module_ptr) == g_instances.end()) {
                close_socket(client_fd);
                return;
            }
        }
        
        bool ws_connected = true;
        while (ws_connected && g_web_server_running) {
            if (is_readable(client_fd, 5)) {
                uint8_t header[2];
                if (!recv_all(client_fd, header, 2)) {
                    ws_connected = false;
                    break;
                }
                
                uint8_t opcode = header[0] & 0x0F;
                bool masked = (header[1] & 0x80) != 0;
                uint64_t payload_len = header[1] & 0x7F;
                
                if (payload_len == 126) {
                    uint8_t len_bytes[2];
                    if (!recv_all(client_fd, len_bytes, 2)) { ws_connected = false; break; }
                    payload_len = (len_bytes[0] << 8) | len_bytes[1];
                } else if (payload_len == 127) {
                    uint8_t len_bytes[8];
                    if (!recv_all(client_fd, len_bytes, 8)) { ws_connected = false; break; }
                    payload_len = 0;
                    for (int i = 0; i < 8; i++) {
                        payload_len = (payload_len << 8) | len_bytes[i];
                    }
                }
                
                uint8_t mask_key[4] = {0};
                if (masked) {
                    if (!recv_all(client_fd, mask_key, 4)) { ws_connected = false; break; }
                }
                
                std::vector<uint8_t> payload(payload_len);
                if (payload_len > 0) {
                    if (!recv_all(client_fd, payload.data(), payload_len)) { ws_connected = false; break; }
                    if (masked) {
                        for (size_t i = 0; i < payload_len; i++) {
                            payload[i] ^= mask_key[i % 4];
                        }
                    }
                }
                
                if (opcode == 0x08) {
                    ws_connected = false;
                    break;
                } else if (opcode == 0x09) {
                    uint8_t pong[2] = {0x8A, 0x00};
                    send(client_fd, (const char*)pong, 2, 0);
                } else if (opcode == 0x01) {
                    std::string payload_str(payload.begin(), payload.end());
                    json_error_t json_err;
                    json_t* root = json_loads(payload_str.c_str(), 0, &json_err);
                    if (root) {
                        json_t* type_j = json_object_get(root, "type");
                        json_t* data_j = json_object_get(root, "data");
                        if (type_j && data_j && json_is_string(type_j) && json_is_array(data_j)) {
                            std::string type_str = json_string_value(type_j);
                            size_t data_size = json_array_size(data_j);
                            std::vector<uint8_t> msg_bytes;
                            msg_bytes.reserve(data_size);
                            for (size_t i = 0; i < data_size; i++) {
                                json_t* item = json_array_get(data_j, i);
                                if (json_is_integer(item)) {
                                    msg_bytes.push_back((uint8_t)json_integer_value(item));
                                }
                            }
                            
                            std::lock_guard<std::mutex> lock(g_instances_mutex);
                            if (g_instances.find(module_ptr) != g_instances.end()) {
                                t_instance = &module_ptr->card_globals;
                                if (type_str == "midi") {
                                    push_midi_to_rx_queue(msg_bytes.data(), msg_bytes.size());
                                } else if (type_str == "serial") {
                                    g_serial_rx_byte_queue.push(msg_bytes.data(), msg_bytes.size());
                                } else if (type_str == "flash") {
                                    json_t* addr_j = json_object_get(root, "address");
                                    if (addr_j && json_is_integer(addr_j)) {
                                        uintptr_t addr = json_integer_value(addr_j);
                                        uintptr_t offset = addr;
                                        if (offset >= 0x10000000) {
                                            offset -= 0x10000000;
                                        }
                                        if (offset + msg_bytes.size() <= PICO_FLASH_SIZE_BYTES) {
                                            memcpy(module_ptr->card_globals.g_flash_memory_val + offset, msg_bytes.data(), msg_bytes.size());
                                            DEBUG("Flashed %zu bytes to simulated memory offset 0x%lx via WebSocket", msg_bytes.size(), offset);
                                            
                                            CardGlobals* old_instance = t_instance;
                                            t_instance = &module_ptr->card_globals;
                                            module_ptr->save_flash_to_disk();
                                            t_instance = old_instance;
                                            
                                            module_ptr->change_card(module_ptr->activeCardIdx);
                                        }
                                    }
                                }
                            }
                        }
                        json_decref(root);
                    }
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(g_instances_mutex);
                if (g_instances.find(module_ptr) != g_instances.end()) {
                    std::vector<uint8_t> midi_msg;
                    while (module_ptr->websocket_midi_tx_queue.pop(midi_msg)) {
                        send_websocket_frame(client_fd, 0x01, midi_msg, "midi");
                    }
                    std::vector<uint8_t> serial_msg;
                    while (module_ptr->websocket_serial_tx_queue.pop(serial_msg)) {
                        send_websocket_frame(client_fd, 0x01, serial_msg, "serial");
                    }
                } else {
                    ws_connected = false;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        
        close_socket(client_fd);
        return;
    }
    
    std::string file_path = "";
    if (path == "/vcv_bridge.js") {
        file_path = asset::plugin(pluginInstance, "res/vcv_bridge.js");
    } else {
        std::string rel_path = path;
        if (!rel_path.empty() && rel_path[0] == '/') {
            rel_path = rel_path.substr(1);
        }
        
        size_t slash_pos = rel_path.find('/');
        if (slash_pos == std::string::npos) {
            std::string card_id = rel_path;
            std::string subpath = "index.html";
            std::string dist_path = asset::plugin(pluginInstance, "res/web/" + card_id + "/dist/" + subpath);
            std::ifstream test_f(dist_path, std::ios::binary);
            if (test_f.good()) {
                test_f.close();
                file_path = dist_path;
            } else {
                file_path = asset::plugin(pluginInstance, "res/web/" + card_id + "/" + subpath);
            }
        } else {
            std::string card_id = rel_path.substr(0, slash_pos);
            std::string subpath = rel_path.substr(slash_pos + 1);
            if (subpath.empty()) {
                subpath = "index.html";
            }
            
            std::string dist_path = asset::plugin(pluginInstance, "res/web/" + card_id + "/dist/" + subpath);
            std::ifstream test_f(dist_path, std::ios::binary);
            if (test_f.good()) {
                test_f.close();
                file_path = dist_path;
            } else {
                file_path = asset::plugin(pluginInstance, "res/web/" + card_id + "/" + subpath);
            }
        }
    }
    
    std::ifstream f(file_path, std::ios::binary);
    if (!f.good()) {
        std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
        send(client_fd, resp.c_str(), resp.length(), 0);
        close_socket(client_fd);
        return;
    }
    
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string content = buffer.str();
    f.close();
    
    if (path != "/vcv_bridge.js") {
        if (path.find(".html") != std::string::npos || path.find(".htm") != std::string::npos) {
            size_t pos = content.find("<head>");
            if (pos != std::string::npos) {
                content.insert(pos + 6, "\n<script src=\"/vcv_bridge.js\"></script>");
            } else {
                pos = content.find("<body>");
                if (pos != std::string::npos) {
                    content.insert(pos + 6, "\n<script src=\"/vcv_bridge.js\"></script>");
                }
            }
        }
    }
    
    std::string mime = get_mime_type(path);
    std::string headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: " + mime + "\r\n"
        "Content-Length: " + std::to_string(content.length()) + "\r\n"
        "Connection: close\r\n\r\n";
    send(client_fd, headers.c_str(), headers.length(), 0);
    send(client_fd, content.data(), content.length(), 0);
    close_socket(client_fd);
}

Model *modelWorkshopSystem =
    createModel<WorkshopSystem, WorkshopSystemWidget>("WorkshopSystem");
