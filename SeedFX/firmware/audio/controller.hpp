#pragma once
#include "../common/protocol.hpp"
#include "../common/storage.hpp"
#include "dsp.hpp"

namespace seedfx {
class Controller {
  AudioEngine &audio_;
  PresetBank &bank_;
  unsigned loaded_model_ = 0, loaded_cab_ = 0;
  bool config_pending_ = false, rewarm_ = false;
  void reconfigure(bool rewarm = false) {
    config_pending_ = true;
    rewarm_ |= rewarm;
    audio_.request_pause();
  }

public:
  Settings settings = Settings::defaults();
  uint8_t preset = 0;
  bool dirty = false, bypassed = false, tuning = false, save_pending = false;
  float pitch = 0, cpu_average = 0, cpu_maximum = 0;
  uint32_t bridge_session = 0;
  Result last_storage = OK;
  Controller(AudioEngine &audio, PresetBank &bank)
      : audio_(audio), bank_(bank) {}
  void initialized() {
    loaded_model_ = unsigned(settings.values[MODEL]);
    loaded_cab_ = unsigned(settings.values[CAB]);
  }
  Result apply(const Frame &f) {
    switch (f.type) {
    case SET_PARAM: {
      if (f.size != 5 || !valid_value(f.data[0], getfloat(f.data + 1)))
        return INVALID;
      auto id = f.data[0];
      float value = getfloat(f.data + 1);
      if (value == settings.values[id])
        return OK;
      bool structural = id == MODEL || id == CAB || id >= AMP_ON;
      if (structural) {
        reconfigure(id == AMP_ON && value > 0.5f);
      } else if (!config_pending_ && !audio_.set(id, value))
        return BUSY;
      settings.values[id] = value;
      dirty = true;
      return OK;
    }
    case LOAD_PRESET: {
      if (f.size != 1 || f.data[0] >= kPresetCount)
        return INVALID;
      if (save_pending)
        return BUSY;
      const auto &s = bank_.presets[f.data[0]];
      if (!s.valid())
        return STORAGE_ERROR;
      bool structural = config_pending_ ||
                        s.values[MODEL] != settings.values[MODEL] ||
                        s.values[CAB] != settings.values[CAB];
      for (unsigned i = AMP_ON; i < PARAM_COUNT; ++i)
        structural |= s.values[i] != settings.values[i];
      if (structural)
        reconfigure(s.values[AMP_ON] > 0.5f && settings.values[AMP_ON] < 0.5f);
      else if (!audio_.set_all(s))
        return BUSY;
      settings = s;
      preset = f.data[0];
      dirty = false;
      return OK;
    }
    case SAVE_PRESET:
      if (f.size != 1 || f.data[0] >= kPresetCount)
        return INVALID;
      if (save_pending)
        return BUSY;
      preset = f.data[0];
      save_pending = true;
      audio_.request_pause();
      return OK;
    case SET_KNOBS: {
      if (f.size != kKnobCount)
        return INVALID;
      auto s = settings;
      std::memcpy(s.knobs, f.data, kKnobCount);
      if (!s.valid())
        return INVALID;
      settings = s;
      dirty = true;
      return OK;
    }
    case BYPASS:
      if (f.size != 1 || f.data[0] > 1)
        return INVALID;
      bypassed = f.data[0];
      audio_.set_bypass(bypassed);
      return OK;
    case TUNER:
      if (f.size != 1 || f.data[0] > 1)
        return INVALID;
      tuning = f.data[0];
      audio_.set_tuner(tuning);
      return OK;
    default:
      return INVALID;
    }
  }
  bool busy() const {
    return save_pending || config_pending_ ||
           loaded_model_ != unsigned(settings.values[MODEL]) ||
           loaded_cab_ != unsigned(settings.values[CAB]);
  }
  void prepare() {
    if (busy())
      audio_.request_pause();
  }
  // Main loop only, after the audio ISR has faded out and acknowledged pause.
  bool service_model() {
    if (!busy() || !audio_.paused())
      return false;
    auto model = unsigned(settings.values[MODEL]),
         cab = unsigned(settings.values[CAB]);
    if (model != loaded_model_ || rewarm_) {
      if (!audio_.load_model(model))
        return false;
      loaded_model_ = model;
      rewarm_ = false;
    }
    if (cab != loaded_cab_ || config_pending_) {
      audio_.load_cab(cab);
      loaded_cab_ = cab;
    }
    if (config_pending_) {
      if (!audio_.set_all(settings))
        return false;
      config_pending_ = false;
    }
    if (!save_pending)
      audio_.resume();
    return true;
  }
  PresetBank bank_for_save() const {
    auto next = bank_;
    next.presets[preset] = settings;
    next.active_slot = preset;
    ++next.generation;
    next.seal();
    return next;
  }
  void saved(const PresetBank &next, bool success) {
    if (success) {
      bank_ = next;
      dirty = false;
    }
    last_storage = success ? OK : STORAGE_ERROR;
    save_pending = false;
    audio_.resume();
  }
  Frame state(uint16_t sequence = 0) const {
    Frame f;
    f.type = STATE;
    f.seq = sequence;
    f.size = PARAM_COUNT * 4 + kKnobCount + 28;
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
      putfloat(f.data + 4 * i, settings.values[i]);
    unsigned off = PARAM_COUNT * 4;
    std::memcpy(f.data + off, settings.knobs, kKnobCount);
    off += kKnobCount;
    f.data[off++] = preset;
    f.data[off++] = bypassed;
    f.data[off++] = tuning;
    f.data[off++] = uint8_t(dirty) | uint8_t(busy() << 1) |
                    uint8_t(audio_.clipped.load() << 2) |
                    uint8_t((last_storage != OK) << 3);
    putfloat(f.data + off, pitch);
    putfloat(f.data + off + 4, cpu_average);
    putfloat(f.data + off + 8, cpu_maximum);
    putfloat(f.data + off + 12, audio_.input_peak);
    putfloat(f.data + off + 16, audio_.output_peak);
    put32(f.data + off + 20, bridge_session);
    return f;
  }
};
} // namespace seedfx
