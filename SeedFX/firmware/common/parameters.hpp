#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace seedfx {
constexpr unsigned kSampleRate = 48000, kBlockSize = 48, kPresetCount = 16,
                   kKnobCount = 6;
enum Param : uint8_t {
  INPUT,
  GATE,
  COMP,
  DRIVE,
  MODEL,
  BASS,
  MID,
  TREBLE,
  CAB,
  CHORUS,
  CHORUS_RATE,
  DELAY,
  DELAY_TIME,
  FEEDBACK,
  REVERB,
  DECAY,
  LEVEL,
  AMP_ON,
  GATE_ON,
  COMP_ON,
  DRIVE_ON,
  EQ_ON,
  CAB_ON,
  CHORUS_ON,
  DELAY_ON,
  REVERB_ON,
  PARAM_COUNT
};
struct Parameter {
  const char *key;
  const char *label;
  const char *unit;
  float min, max, initial;
};
inline constexpr Parameter kParameters[PARAM_COUNT] = {
    {"input", "Input", "dB", -18, 18, 0},
    {"gate", "Gate", "dB", -80, -20, -65},
    {"comp", "Compress", "%", 0, 100, 25},
    {"drive", "Drive", "%", 0, 100, 20},
    {"model", "Amp model", "", 0, 4, 0},
    {"bass", "Bass", "dB", -12, 12, 0},
    {"mid", "Mid", "dB", -12, 12, 0},
    {"treble", "Treble", "dB", -12, 12, 0},
    {"cab", "Cabinet", "", 0, 1, 0},
    {"chorus", "Chorus", "%", 0, 100, 20},
    {"chorus_rate", "Chorus rate", "Hz", 0.1f, 4, 0.8f},
    {"delay", "Delay", "%", 0, 100, 20},
    {"delay_time", "Delay time", "ms", 40, 1500, 320},
    {"feedback", "Feedback", "%", 0, 85, 30},
    {"reverb", "Reverb", "%", 0, 60, 12},
    {"decay", "Reverb decay", "%", 0, 95, 50},
    {"level", "Output", "dB", -30, 6, -6},
    {"amp_on", "Amp", "", 0, 1, 1},
    {"gate_on", "Gate", "", 0, 1, 1},
    {"comp_on", "Compressor", "", 0, 1, 0},
    {"drive_on", "Overdrive", "", 0, 1, 0},
    {"eq_on", "Equalizer", "", 0, 1, 1},
    {"cab_on", "Cabinet", "", 0, 1, 1},
    {"chorus_on", "Chorus", "", 0, 1, 0},
    {"delay_on", "Delay", "", 0, 1, 0},
    {"reverb_on", "Reverb", "", 0, 1, 0}};
inline bool valid_value(unsigned id, float value) {
  if (id >= PARAM_COUNT || !std::isfinite(value))
    return false;
  const auto &p = kParameters[id];
  if (value < p.min || value > p.max)
    return false;
  if ((id == MODEL || id == CAB || id >= AMP_ON) && value != std::floor(value))
    return false;
  return true;
}
struct Settings {
  float values[PARAM_COUNT]{};
  uint8_t knobs[kKnobCount]{INPUT, BASS, MID, TREBLE, LEVEL, DELAY};
  static Settings defaults() {
    Settings s;
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
      s.values[i] = kParameters[i].initial;
    return s;
  }
  bool valid() const {
    for (unsigned i = 0; i < PARAM_COUNT; ++i)
      if (!valid_value(i, values[i]))
        return false;
    for (auto id : knobs)
      if (id >= PARAM_COUNT || id == MODEL || id == CAB || id >= AMP_ON)
        return false;
    return true;
  }
};
// Soft takeover after remote/preset changes. Cross the stored value to acquire.
class Pickup {
  float previous_ = 0;
  bool has_previous_ = false, acquired_ = false;

public:
  void release() { acquired_ = false; }
  bool accept(float physical, float target) {
    if (!std::isfinite(physical) || !std::isfinite(target))
      return false;
    bool near = std::fabs(physical - target) <= 0.025f;
    bool crossed =
        has_previous_ && (physical - target) * (previous_ - target) <= 0;
    previous_ = physical;
    has_previous_ = true;
    if (near || crossed)
      acquired_ = true;
    return acquired_;
  }
};
// Slow physical turns accumulate against the last transmitted value.
class KnobTracker {
  Pickup pickup_;
  float sent_ = 0;
  bool initialized_ = false;

public:
  void release() {
    pickup_.release();
    initialized_ = false;
  }
  bool changed(float physical, float target) {
    if (!std::isfinite(physical))
      return false;
    if (!initialized_) {
      sent_ = physical;
      initialized_ = true;
    }
    if (!pickup_.accept(physical, target)) {
      sent_ = physical;
      return false;
    }
    return std::fabs(physical - sent_) > 0.004f;
  }
  void transmitted(float physical) { sent_ = physical; }
};
} // namespace seedfx
