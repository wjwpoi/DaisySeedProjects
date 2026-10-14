#pragma once
#include "../../../Software/GuitarPedal/Effect-Modules/ImpulseResponse/ir_data.h"
#include "../../../Software/GuitarPedal/Effect-Modules/Nam/model_data_nam_a2.h"
#include "../common/parameters.hpp"
#include "../common/queue.hpp"
#include "dsp/filtering_functions.h"
#include "warm_start.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace seedfx {
constexpr float kPi = 3.14159265358979323846f;
inline float dbgain(float db) { return std::pow(10.0f, db / 20.0f); }
struct AudioBuffers {
  float delay[72002]{}, chorus[2048]{}, reverb[4][2048]{}, diffusion[2][512]{};
};
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void peak(float hz, float db) {
    float a = std::pow(10.0f, db / 40.0f), w = 2 * kPi * hz / kSampleRate,
          alpha = std::sin(w) / (2 * 0.7f), c = std::cos(w),
          den = 1 + alpha / a;
    b0 = (1 + alpha * a) / den;
    b1 = -2 * c / den;
    b2 = (1 - alpha * a) / den;
    a1 = -2 * c / den;
    a2 = (1 - alpha / a) / den;
  }
  float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void clear() { z1 = z2 = 0; }
};
struct Event {
  uint8_t id;
  float value;
};
class AudioEngine {
  AudioBuffers &buffers_;
  nam_a2_daisy::A2Player &nam_;
  SpscQueue<Event, 64> events_;
  float p_[PARAM_COUNT]{}, target_[PARAM_COUNT]{},
      input_gain_ = 1, output_gain_ = 1, gate_threshold_ = 0.0005f;
  float envelope_ = 0, gate_gain_ = 1, compressor_gain_ = 1, drive_gain_ = 1,
        phase_ = 0, fade_ = 1, bypass_mix_ = 0, tuner_gain_ = 1;
  float nam_input_[kBlockSize]{}, nam_output_[kBlockSize]{},
      ir_history_[1024 + kBlockSize - 1]{};
  float cab_coeffs_[1024]{};
  arm_fir_instance_f32 cab_filter_{};
  float model_gain_ = 1;
  unsigned nam_pos_ = 0, delay_pos_ = 0, chorus_pos_ = 0;
  unsigned reverb_pos_[4]{}, diffusion_pos_[2]{};
  float reverb_damped_[4]{};
  Biquad eq_[3];
  std::atomic<bool> pause_requested_{false}, paused_{false};
  std::atomic<bool> bypass_{false}, tuner_mute_{false};
  std::atomic<unsigned> tuner_pos_{0};
  unsigned tuner_decimation_ = 0;
  std::atomic<float> tuner_window_[1024]{};
  std::atomic<unsigned> tuner_epoch_{0};
  bool refresh_ = true;
  unsigned loaded_model_ = 0, clip_hold_ = 0;
  void refresh() {
    input_gain_ = dbgain(p_[INPUT]);
    output_gain_ = dbgain(p_[LEVEL]);
    gate_threshold_ = dbgain(p_[GATE]);
    drive_gain_ = 1 + 19 * p_[DRIVE] / 100;
    eq_[0].peak(110, p_[BASS]);
    eq_[1].peak(900, p_[MID]);
    eq_[2].peak(4000, p_[TREBLE]);
    refresh_ = false;
  }
  float chorus(float x, float distance) {
    buffers_.chorus[chorus_pos_] = x;
    float pos = chorus_pos_ - distance;
    if (pos < 0)
      pos += 2048;
    // Floating-point addition at a wrap may round to exactly the ring size.
    unsigned index = unsigned(pos) & 2047;
    float fraction = pos - std::floor(pos);
    float wet = buffers_.chorus[index] * (1 - fraction) +
                buffers_.chorus[(index + 1) & 2047] * fraction;
    chorus_pos_ = (chorus_pos_ + 1) & 2047;
    return x + 0.5f * p_[CHORUS] / 100 * (wet - x);
  }
  float delay(float x) {
    float distance = std::clamp(p_[DELAY_TIME] * 48, 1.0f, 72000.0f),
          pos = delay_pos_ - distance;
    while (pos < 0)
      pos += 72002;
    unsigned i = unsigned(pos) % 72002;
    float f = pos - std::floor(pos);
    float wet =
        buffers_.delay[i] * (1 - f) + buffers_.delay[(i + 1) % 72002] * f;
    buffers_.delay[delay_pos_] =
        std::clamp(x + wet * p_[FEEDBACK] / 100, -4.0f, 4.0f);
    delay_pos_ = (delay_pos_ + 1) % 72002;
    return x + wet * p_[DELAY] / 100;
  }
  float reverb(float x) {
    constexpr unsigned lengths[4] = {1557, 1617, 1491, 1422};
    float sum = 0;
    for (unsigned i = 0; i < 4; ++i) {
      auto &pos = reverb_pos_[i];
      float wet = buffers_.reverb[i][pos];
      reverb_damped_[i] += 0.3f * (wet - reverb_damped_[i]);
      buffers_.reverb[i][pos] =
          x * 0.25f + reverb_damped_[i] * (0.25f + 0.65f * p_[DECAY] / 100);
      sum += wet;
      if (++pos == lengths[i])
        pos = 0;
    }
    constexpr unsigned diffusion_lengths[2] = {337, 113};
    for (unsigned i = 0; i < 2; ++i) {
      auto &pos = diffusion_pos_[i];
      float delayed = buffers_.diffusion[i][pos];
      float diffused = delayed - 0.5f * sum;
      buffers_.diffusion[i][pos] = sum + 0.5f * diffused;
      sum = diffused;
      if (++pos == diffusion_lengths[i])
        pos = 0;
    }
    return x + sum * p_[REVERB] / 100;
  }

public:
  std::atomic<float> input_peak{0}, output_peak{0};
  std::atomic<bool> clipped{false};
  AudioEngine(AudioBuffers &buffers, nam_a2_daisy::A2Player &nam)
      : buffers_(buffers), nam_(nam) {
    auto s = Settings::defaults();
    std::copy(s.values, s.values + PARAM_COUNT, p_);
    std::copy(s.values, s.values + PARAM_COUNT, target_);
  }
  bool set(unsigned id, float value) {
    return valid_value(id, value) && events_.push({uint8_t(id), value});
  }
  bool set_all(const Settings &s) {
    if (!s.valid() || events_.available() < PARAM_COUNT)
      return false;
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
      events_.push({uint8_t(i), s.values[i]});
    return true;
  }
  void request_pause() {
    pause_requested_.store(true, std::memory_order_release);
  }
  bool paused() const { return paused_.load(std::memory_order_acquire); }
  void resume() { pause_requested_.store(false, std::memory_order_release); }
  void set_bypass(bool v) { bypass_.store(v); }
  void set_tuner(bool v) { tuner_mute_.store(v); }
  bool load_model(unsigned index) {
    if (index >= unsigned(nam_a2_models::kNamA2ModelCount))
      return false;
    const auto &m = nam_a2_models::kNamA2Models[index];
    if (!nam_a2_daisy::load_weights(nam_.weights(), m.weights,
                                    nam_a2_daisy::kA2WeightCount) ||
        !steady_start(nam_.state(), nam_.hot_state(), nam_.weights()))
      return false;
    model_gain_ = m.outputGain;
    loaded_model_ = index;
    std::fill(std::begin(nam_input_), std::end(nam_input_), 0);
    std::fill(std::begin(nam_output_), std::end(nam_output_), 0);
    nam_pos_ = 0;
    return true;
  }
  void load_cab(unsigned index) {
    auto &ir = ir_collection[index % ir_collection.size()];
    float energy = 0;
    for (unsigned i = 0; i < 1024; ++i) {
      cab_coeffs_[1023 - i] = ir[i];
      energy += ir[i] * ir[i];
    }
    float scale = energy > 1e-12f ? 1 / std::sqrt(energy) : 1;
    for (auto &c : cab_coeffs_)
      c *= scale * 0.5f;
    arm_fir_init_f32(&cab_filter_, 1024, cab_coeffs_, ir_history_, kBlockSize);
  }
  void clear_tails() {
    std::fill(std::begin(buffers_.delay), std::end(buffers_.delay), 0);
    std::fill(std::begin(buffers_.chorus), std::end(buffers_.chorus), 0);
    for (auto &r : buffers_.reverb)
      std::fill(std::begin(r), std::end(r), 0);
    for (auto &a : buffers_.diffusion)
      std::fill(std::begin(a), std::end(a), 0);
    std::fill(std::begin(reverb_damped_), std::end(reverb_damped_), 0);
    for (auto &e : eq_)
      e.clear();
  }
  void process(const float *input, float *output, unsigned n) {
    if (n != kBlockSize) {
      std::fill(output, output + n, 0);
      return;
    }
    Event event;
    while (events_.pop(event)) {
      target_[event.id] = event.value;
    }
    for (unsigned id = 0; id < PARAM_COUNT; ++id) {
      float next = (id == MODEL || id == CAB || id >= AMP_ON)
                       ? target_[id]
                       : p_[id] + 0.095f * (target_[id] - p_[id]);
      if (std::fabs(next - target_[id]) < 0.0001f)
        next = target_[id];
      if (next != p_[id]) {
        p_[id] = next;
        refresh_ = true;
      }
    }
    if (refresh_)
      refresh();
    float block_input_peak = 0, block_output_peak = 0;
    bool block_clipped = false;
    auto epoch = tuner_epoch_.load(std::memory_order_relaxed);
    tuner_epoch_.store(epoch + 1, std::memory_order_release);
    bool mute = pause_requested_.load(std::memory_order_acquire);
    if (!mute)
      paused_.store(false, std::memory_order_release);
    float dry[kBlockSize]{}, pre_cab[kBlockSize]{}, post_cab[kBlockSize]{},
        fade[kBlockSize]{};
    unsigned active_samples = 0;
    // Control-rate transcendental calculations; envelope and gain interpolation
    // remain at sample rate. One block is 1 ms.
    const float compressor_target =
        envelope_ > 0.15f ? std::pow(0.15f / envelope_, 0.75f * p_[COMP] / 100)
                          : 1.0f;
    for (unsigned i = 0; i < n; ++i) {
      float raw = std::isfinite(input[i]) ? input[i] : 0;
      block_input_peak = std::max(block_input_peak, std::fabs(raw));
      block_clipped |= std::fabs(raw) >= 0.98f;
      if (++tuner_decimation_ == 4) {
        tuner_decimation_ = 0;
        tuner_window_[tuner_pos_] = raw;
        tuner_pos_ = (tuner_pos_ + 1) & 1023;
      }
      if (mute)
        fade_ = std::max(0.0f, fade_ - 1.0f / 144);
      else
        fade_ = std::min(1.0f, fade_ + 1.0f / 144);
      dry[i] = raw;
      fade[i] = fade_;
      if (mute && fade_ == 0)
        continue;
      active_samples = i + 1;
      float x = raw * input_gain_, magnitude = std::fabs(x);
      envelope_ +=
          (magnitude > envelope_ ? 0.01f : 0.001f) * (magnitude - envelope_);
      float gate_target = envelope_ > gate_threshold_ ? 1 : 0;
      gate_gain_ += (gate_target > gate_gain_ ? 0.03f : 0.001f) *
                    (gate_target - gate_gain_);
      if (p_[GATE_ON] > 0.5f)
        x *= gate_gain_;
      if (p_[COMP_ON] > 0.5f) {
        compressor_gain_ += 0.002f * (compressor_target - compressor_gain_);
        x *= compressor_gain_;
      }
      if (p_[DRIVE_ON] > 0.5f) {
        float z = x * drive_gain_;
        x = z / (1 + std::fabs(z));
      }
      if (p_[AMP_ON] > 0.5f) {
        float previous = nam_output_[nam_pos_] * model_gain_;
        nam_input_[nam_pos_] = x;
        if (++nam_pos_ == kBlockSize) {
          nam_.process_block_48(nam_input_, nam_output_);
          nam_pos_ = 0;
        }
        x = previous;
      }
      if (p_[EQ_ON] > 0.5f)
        for (auto &e : eq_)
          x = e.process(x);
      pre_cab[i] = x;
    }
    // Do not access mutable FIR/model state while the main loop owns a pause.
    // The ISR completes this block before the main loop can observe paused_.
    if (active_samples) {
      if (p_[CAB_ON] > 0.5f)
        arm_fir_f32(&cab_filter_, pre_cab, post_cab, kBlockSize);
      else
        std::copy(pre_cab, pre_cab + kBlockSize, post_cab);
    }
    float chorus_distance = 600 + 180 * std::sin(phase_);
    const float phase_step = 2 * kPi * p_[CHORUS_RATE] / kSampleRate;
    const float chorus_step =
        active_samples
            ? (600 + 180 * std::sin(phase_ + phase_step * active_samples) -
               chorus_distance) /
                  active_samples
            : 0;
    for (unsigned i = 0; i < n; ++i) {
      if (i >= active_samples) {
        output[i] = 0;
        continue;
      }
      float x = post_cab[i];
      if (p_[CHORUS_ON] > 0.5f) {
        chorus_distance += chorus_step;
        x = chorus(x, chorus_distance);
      }
      // Disabled sends decay to zero; existing delay/reverb tails continue.
      float delay_send = p_[DELAY_ON] > 0.5f ? x : 0;
      float delay_out = delay(delay_send);
      x += delay_out - delay_send;
      float reverb_send = p_[REVERB_ON] > 0.5f ? x : 0;
      float reverb_out = reverb(reverb_send);
      x += reverb_out - reverb_send;
      x *= output_gain_;
      float target = bypass_.load(std::memory_order_relaxed) ? 1 : 0;
      bypass_mix_ += std::clamp(target - bypass_mix_, -1.0f / 240, 1.0f / 240);
      x = x * (1 - bypass_mix_) + dry[i] * bypass_mix_;
      float tuner_target = tuner_mute_.load(std::memory_order_relaxed) ? 0 : 1;
      tuner_gain_ +=
          std::clamp(tuner_target - tuner_gain_, -1.0f / 240, 1.0f / 240);
      x *= tuner_gain_;
      if (!std::isfinite(x))
        x = 0;
      block_clipped |= std::fabs(x) > 0.98f;
      output[i] = std::clamp(x * fade[i], -0.98f, 0.98f);
      block_output_peak = std::max(block_output_peak, std::fabs(output[i]));
    }
    if (p_[CHORUS_ON] > 0.5f) {
      phase_ += phase_step * active_samples;
      if (phase_ >= 2 * kPi)
        phase_ -= 2 * kPi;
    }
    if (mute && fade_ == 0)
      paused_.store(true, std::memory_order_release);
    input_peak.store(std::max(input_peak.load() * 0.995f, block_input_peak),
                     std::memory_order_relaxed);
    output_peak.store(std::max(output_peak.load() * 0.995f, block_output_peak),
                      std::memory_order_relaxed);
    if (block_clipped)
      clip_hold_ = 1000;
    else if (clip_hold_)
      --clip_hold_;
    clipped.store(clip_hold_ > 0, std::memory_order_relaxed);
    tuner_epoch_.store(epoch + 2, std::memory_order_release);
  }
  bool tuner_snapshot(float *dst) {
    auto before = tuner_epoch_.load(std::memory_order_acquire);
    if (before & 1)
      return false;
    unsigned pos = tuner_pos_;
    for (unsigned i = 0; i < 1024; ++i)
      dst[i] = tuner_window_[(pos + i) & 1023];
    return before == tuner_epoch_.load(std::memory_order_acquire);
  }
};
inline float detect_pitch(const float *x) {
  // Main loop only. 12 kHz decimated input, normalized difference + CMND.
  float energy = 0;
  for (unsigned i = 0; i < 1024; ++i)
    energy += x[i] * x[i];
  if (energy < 1e-5f)
    return 0;
  float difference[201]{}, running = 0;
  for (unsigned lag = 1; lag <= 200; ++lag) {
    float sum = 0;
    for (unsigned i = 0; i < 800; ++i) {
      float d = x[i] - x[i + lag];
      sum += d * d;
    }
    running += sum;
    difference[lag] = running > 1e-12f ? sum * lag / running : 1;
  }
  unsigned best = 0;
  for (unsigned lag = 12; lag < 199; ++lag)
    if (difference[lag] < 0.15f && difference[lag] <= difference[lag + 1]) {
      best = lag;
      break;
    }
  if (!best)
    return 0;
  float a = difference[best - 1], b = difference[best],
        c = difference[best + 1], den = a - 2 * b + c;
  float shift = std::fabs(den) > 1e-8f ? 0.5f * (a - c) / den : 0;
  return 12000 / (best + shift);
}
} // namespace seedfx
