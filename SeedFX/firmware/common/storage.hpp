#pragma once
#include "parameters.hpp"
#include "protocol.hpp"
#include <cstring>
namespace seedfx {
constexpr uint32_t kPresetMagic = 0x31584653; // SFX1
struct PresetBank {
  uint32_t magic = kPresetMagic, version = 1, generation = 0, active_slot = 0;
  Settings presets[kPresetCount]{};
  uint32_t checksum = 0;
  void seal() {
    checksum = crc32(reinterpret_cast<const uint8_t *>(this),
                     offsetof(PresetBank, checksum));
  }
  bool valid() const {
    if (magic != kPresetMagic || version != 1 || active_slot >= kPresetCount ||
        checksum != crc32(reinterpret_cast<const uint8_t *>(this),
                          offsetof(PresetBank, checksum)))
      return false;
    for (const auto &s : presets)
      if (!s.valid())
        return false;
    return true;
  }
  static PresetBank defaults() {
    PresetBank b;
    for (auto &p : b.presets)
      p = Settings::defaults();
    for (unsigned i = 0; i < 5; ++i)
      b.presets[i].values[MODEL] = float(i);
    b.presets[2].values[CAB_ON] =
        0; // Reference Ampeg capture names an MD421 mic.
    b.presets[5].values[CHORUS_ON] = 1;
    b.presets[5].values[CHORUS] = 30;
    b.presets[5].values[INPUT] = -9;
    b.presets[6].values[DELAY_ON] = 1;
    b.presets[6].values[DELAY_TIME] = 90;
    b.presets[6].values[FEEDBACK] = 15;
    b.presets[7].values[MODEL] = 1;
    b.presets[7].values[DELAY_ON] = 1;
    b.presets[7].values[DELAY_TIME] = 360;
    b.presets[8].values[DELAY_ON] = 1;
    b.presets[8].values[DELAY_TIME] = 500;
    b.presets[8].values[REVERB_ON] = 1;
    b.presets[8].values[REVERB] = 30;
    b.presets[9].values[MODEL] = 4;
    b.presets[9].values[DRIVE_ON] = 1;
    b.presets[10].values[MODEL] = 2;
    b.presets[10].values[CAB_ON] = 0;
    b.presets[10].values[BASS] = 2;
    b.presets[11].values[COMP_ON] = 1;
    b.presets[11].values[INPUT] = -12;
    b.presets[12].values[AMP_ON] = 0;
    b.presets[12].values[CAB_ON] = 0;
    b.presets[12].values[LEVEL] = 0;
    b.presets[13] = b.presets[12];
    b.presets[13].values[CHORUS_ON] = 1;
    b.presets[14] = b.presets[12];
    b.presets[14].values[DELAY_ON] = 1;
    b.seal();
    return b;
  }
};
static_assert(sizeof(PresetBank) < 4096, "Bank must fit a QSPI erase sector");
inline const PresetBank *newest_bank(const PresetBank *a, const PresetBank *b) {
  bool va = a && a->valid(), vb = b && b->valid();
  if (!va)
    return vb ? b : nullptr;
  if (!vb)
    return a;
  return int32_t(b->generation - a->generation) > 0 ? b : a;
}
} // namespace seedfx
