#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace seedfx {
constexpr size_t kMaxPayload = 192, kMaxFrame = 208;
enum Message : uint8_t {
  GET_STATE = 1,
  SET_PARAM = 2,
  LOAD_PRESET = 3,
  SAVE_PRESET = 4,
  SET_KNOBS = 5,
  BYPASS = 6,
  TUNER = 7,
  TAP = 8,
  HELLO = 9,
  STATE = 0x80,
  ACK = 0x81
};
enum Result : uint8_t {
  OK = 0,
  INVALID = 1,
  BUSY = 2,
  STORAGE_ERROR = 3,
  LINK_TIMEOUT = 4
};
struct Frame {
  uint8_t type = 0;
  uint16_t seq = 0;
  uint16_t size = 0;
  uint8_t data[kMaxPayload]{};
};
inline uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    c ^= data[i];
    for (int j = 0; j < 8; ++j)
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
  }
  return c ^ 0xffffffffu;
}
inline void put16(uint8_t *d, uint16_t v) {
  d[0] = uint8_t(v);
  d[1] = uint8_t(v >> 8);
}
inline uint16_t get16(const uint8_t *d) {
  return uint16_t(d[0]) | (uint16_t(d[1]) << 8);
}
inline void put32(uint8_t *d, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    d[i] = uint8_t(v >> (8 * i));
}
inline uint32_t get32(const uint8_t *d) {
  return uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) |
         (uint32_t(d[3]) << 24);
}
inline void putfloat(uint8_t *d, float v) {
  uint32_t b;
  std::memcpy(&b, &v, 4);
  put32(d, b);
}
inline float getfloat(const uint8_t *d) {
  uint32_t b = get32(d);
  float v;
  std::memcpy(&v, &b, 4);
  return v;
}
inline size_t encode(const Frame &f, uint8_t *out, size_t capacity) {
  if (f.size > kMaxPayload || capacity < kMaxFrame)
    return 0;
  uint8_t raw[kMaxPayload + 10]{};
  raw[0] = 1;
  raw[1] = f.type;
  put16(raw + 2, f.seq);
  put16(raw + 4, f.size);
  std::memcpy(raw + 6, f.data, f.size);
  put32(raw + 6 + f.size, crc32(raw, 6 + f.size));
  size_t write = 1, code_pos = 0;
  uint8_t code = 1;
  for (size_t i = 0; i < f.size + 10u; ++i) {
    if (raw[i] == 0) {
      out[code_pos] = code;
      code_pos = write++;
      code = 1;
    } else {
      out[write++] = raw[i];
      if (++code == 255) {
        out[code_pos] = code;
        code_pos = write++;
        code = 1;
      }
    }
  }
  out[code_pos] = code;
  out[write++] = 0;
  return write;
}
class Decoder {
  uint8_t buffer_[kMaxFrame]{};
  size_t used_ = 0;
  bool overflow_ = false;

public:
  unsigned rejected = 0;
  bool feed(uint8_t byte, Frame &f) {
    if (byte) {
      if (used_ < sizeof(buffer_))
        buffer_[used_++] = byte;
      else
        overflow_ = true;
      return false;
    }
    if (!used_ && !overflow_)
      return false;
    uint8_t raw[kMaxPayload + 10]{};
    size_t read = 0, write = 0;
    bool valid = !overflow_;
    while (valid && read < used_) {
      uint8_t code = buffer_[read++];
      if (!code || read + code - 1 > used_) {
        valid = false;
        break;
      }
      for (unsigned i = 1; i < code; ++i) {
        if (write >= sizeof(raw)) {
          valid = false;
          break;
        }
        raw[write++] = buffer_[read++];
      }
      if (valid && code != 255 && read < used_) {
        if (write >= sizeof(raw))
          valid = false;
        else
          raw[write++] = 0;
      }
    }
    used_ = 0;
    overflow_ = false;
    if (!valid || write < 10 || raw[0] != 1) {
      ++rejected;
      return false;
    }
    auto size = get16(raw + 4);
    if (size > kMaxPayload || write != size + 10u ||
        crc32(raw, size + 6) != get32(raw + size + 6)) {
      ++rejected;
      return false;
    }
    f.type = raw[1];
    f.seq = get16(raw + 2);
    f.size = size;
    std::memcpy(f.data, raw + 6, size);
    return true;
  }
};
} // namespace seedfx
