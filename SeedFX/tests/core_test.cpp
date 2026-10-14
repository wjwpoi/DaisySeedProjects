#include "../firmware/audio/controller.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
using namespace seedfx;
static unsigned checks = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)
static AudioBuffers buffers;
static nam_a2_daisy::A2Player model;
int main() {
  CHECK(crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) ==
        0xcbf43926u);
  std::mt19937 rng(12345);
  for (unsigned trial = 0; trial < 500; ++trial) {
    Frame input;
    input.type = SET_PARAM;
    input.seq = uint16_t(rng());
    input.size = rng() % (kMaxPayload + 1);
    for (unsigned i = 0; i < input.size; ++i)
      input.data[i] = uint8_t(rng());
    uint8_t bytes[kMaxFrame];
    auto n = encode(input, bytes, sizeof(bytes));
    CHECK(n > 0);
    Decoder d;
    Frame output;
    unsigned decoded = 0;
    for (size_t i = 0; i < n; ++i)
      decoded += d.feed(bytes[i], output);
    CHECK(decoded == 1 && output.seq == input.seq &&
          output.size == input.size &&
          std::memcmp(output.data, input.data, input.size) == 0);
    bytes[n / 2] ^= 0x40;
    Decoder corrupted;
    decoded = 0;
    for (size_t i = 0; i < n; ++i)
      decoded += corrupted.feed(bytes[i], output);
    CHECK(decoded == 0);
  }
  Decoder d;
  Frame f;
  for (unsigned i = 0; i < 300; ++i)
    d.feed(1, f);
  CHECK(!d.feed(0, f));
  CHECK(d.rejected == 1);
  SpscQueue<int, 4> q;
  CHECK(q.push(1) && q.push(2) && q.push(3) && !q.push(4));
  int v;
  CHECK(q.pop(v) && v == 1);
  CHECK(q.push(4));
  Pickup pickup;
  CHECK(!pickup.accept(0, 0.8f));
  CHECK(!pickup.accept(0.5f, 0.8f));
  CHECK(pickup.accept(0.9f, 0.8f));
  pickup.release();
  CHECK(!pickup.accept(0.9f, 0.2f));
  auto a = PresetBank::defaults(), b = a;
  CHECK(a.valid());
  b.generation = 1;
  b.seal();
  CHECK(newest_bank(&a, &b) == &b);
  b.presets[0].values[MODEL] = 5;
  b.seal();
  CHECK(!b.valid());
  CHECK(newest_bank(&a, &b) == &a);
  b = a;
  b.checksum ^= 1;
  CHECK(!b.valid());
  CHECK(newest_bank(&b, nullptr) == nullptr);
  AudioEngine engine(buffers, model);
  auto bank = PresetBank::defaults();
  Controller controller(engine, bank);
  Frame bad;
  bad.type = SET_PARAM;
  bad.size = 5;
  bad.data[0] = MODEL;
  putfloat(bad.data + 1, 0.5f);
  CHECK(controller.apply(bad) == INVALID);
  putfloat(bad.data + 1, std::nanf(""));
  CHECK(controller.apply(bad) == INVALID);
  Frame load;
  load.type = LOAD_PRESET;
  load.size = 1;
  load.data[0] = 16;
  CHECK(controller.apply(load) == INVALID);
  CHECK(engine.load_model(0));
  engine.load_cab(0);
  float in[48]{}, out[48]{};
  auto clean = Settings::defaults();
  for (unsigned i = AMP_ON; i < PARAM_COUNT; ++i)
    clean.values[i] = 0;
  clean.values[LEVEL] = 0;
  CHECK(engine.set_all(clean));
  for (unsigned i = 0; i < 110; ++i)
    engine.process(in, out, 48);
  in[0] = 0.25f;
  engine.process(in, out, 48);
  CHECK(std::fabs(out[0] - 0.25f) < 1e-5f);
  in[0] = std::nanf("");
  engine.process(in, out, 48);
  for (auto x : out)
    CHECK(std::isfinite(x));
  clean.values[AMP_ON] = 1;
  CHECK(engine.set_all(clean));
  for (unsigned i = 0; i < 100; ++i) {
    for (unsigned j = 0; j < 48; ++j)
      in[j] = 0.05f * std::sin(2 * kPi * 440 * (48 * i + j) / 48000);
    engine.process(in, out, 48);
    for (auto x : out)
      CHECK(std::isfinite(x) && std::fabs(x) <= 0.981f);
  }
  engine.request_pause();
  for (unsigned i = 0; i < 4; ++i)
    engine.process(in, out, 48);
  CHECK(engine.paused());
  CHECK(engine.load_model(1));
  engine.resume();
  engine.process(in, out, 48);
  CHECK(!engine.paused());
  float tone[1024];
  for (unsigned i = 0; i < 1024; ++i)
    tone[i] = 0.1f * std::sin(2 * kPi * 110 * i / 12000);
  CHECK(std::fabs(detect_pitch(tone) - 110) < 0.5f);
  std::fill(std::begin(tone), std::end(tone), 0);
  CHECK(detect_pitch(tone) == 0);
  Frame state = controller.state();
  CHECK(state.size <= kMaxPayload);
  KnobTracker slow_knob;
  CHECK(!slow_knob.changed(0.5f, 0.5f));
  unsigned knob_updates = 0;
  for (unsigned i = 1; i <= 1000; ++i) {
    float value = 0.5f + i * 0.0002f;
    if (slow_knob.changed(value, value)) {
      slow_knob.transmitted(value);
      ++knob_updates;
    }
  }
  CHECK(knob_updates > 40);
  slow_knob.release();
  CHECK(!slow_knob.changed(0.7f, 0.1f));

  // Verify the actual cabinet impulse response, not just nonempty output.
  clean.values[AMP_ON] = 0;
  clean.values[CAB_ON] = 1;
  CHECK(engine.set_all(clean));
  std::fill(std::begin(in), std::end(in), 0);
  for (unsigned i = 0; i < 200; ++i)
    engine.process(in, out, 48);
  engine.load_cab(0);
  in[0] = 0.01f;
  engine.process(in, out, 48);
  float energy = 0;
  for (float c : ir_collection[0])
    energy += c * c;
  for (unsigned block = 0; block < 23; ++block) {
    if (block) {
      std::fill(std::begin(in), std::end(in), 0);
      engine.process(in, out, 48);
    }
    for (unsigned i = 0; i < 48; ++i) {
      unsigned sample = block * 48 + i;
      float expected =
          sample < 1024 ? 0.005f * ir_collection[0][sample] / std::sqrt(energy)
                        : 0;
      CHECK(std::fabs(out[i] - expected) < 1e-6f);
    }
  }
  clean.values[CAB_ON] = 0;
  clean.values[DELAY_ON] = 1;
  clean.values[DELAY] = 100;
  clean.values[DELAY_TIME] = 40;
  clean.values[FEEDBACK] = 0;
  CHECK(engine.set_all(clean));
  std::fill(std::begin(in), std::end(in), 0);
  engine.clear_tails();
  for (unsigned i = 0; i < 200; ++i)
    engine.process(in, out, 48);
  in[0] = 0.1f;
  engine.process(in, out, 48);
  CHECK(std::fabs(out[0] - 0.1f) < 1e-5f);
  std::fill(std::begin(in), std::end(in), 0);
  float echo = 0;
  for (unsigned block = 1; block <= 42; ++block) {
    engine.process(in, out, 48);
    for (unsigned i = 0; i < 48; ++i) {
      unsigned sample = block * 48 + i;
      if (sample < 1900)
        CHECK(std::fabs(out[i]) < 1e-6f);
      if (sample >= 1900)
        echo += std::fabs(out[i]);
    }
  }
  CHECK(echo > 0.09f && echo < 0.11f);

  // All effect stages with each real built-in model must remain bounded/finite.
  auto full = Settings::defaults();
  for (unsigned id = AMP_ON; id < PARAM_COUNT; ++id)
    full.values[id] = 1;
  for (unsigned m = 0; m < 5; ++m) {
    CHECK(engine.load_model(m));
    full.values[MODEL] = float(m);
    CHECK(engine.set_all(full));
    for (unsigned block = 0; block < 20; ++block) {
      for (unsigned i = 0; i < 48; ++i)
        in[i] = 0.03f * std::sin(0.03f * (block * 48 + i));
      engine.process(in, out, 48);
      for (float sample : out)
        CHECK(std::isfinite(sample) && std::fabs(sample) <= 0.981f);
    }
  }

  full.values[AMP_ON] = 0;
  full.values[CAB_ON] = 0;
  full.values[DELAY_TIME] = 40.00001f;
  full.values[CHORUS_RATE] = 4;
  CHECK(engine.set_all(full));
  for (unsigned block = 0; block < 1600; ++block) {
    for (unsigned i = 0; i < 48; ++i)
      in[i] = 0.01f * std::sin(0.05f * (block * 48 + i));
    engine.process(in, out, 48);
    for (float value : out)
      CHECK(std::isfinite(value) && std::fabs(value) <= 0.981f);
  }
  full.values[AMP_ON] = 1;
  full.values[CAB_ON] = 1;
  controller.settings = full;
  controller.initialized();
  Frame change;
  change.type = SET_PARAM;
  change.size = 5;
  change.data[0] = MODEL;
  putfloat(change.data + 1, 2);
  CHECK(controller.apply(change) == OK && controller.busy());
  for (unsigned i = 0; i < 4; ++i)
    engine.process(in, out, 48);
  CHECK(engine.paused() && controller.service_model());
  CHECK(!controller.busy());
  engine.process(in, out, 48);
  Frame save;
  save.type = SAVE_PRESET;
  save.size = 1;
  save.data[0] = 3;
  CHECK(controller.apply(save) == OK);
  for (unsigned i = 0; i < 4; ++i)
    engine.process(in, out, 48);
  CHECK(controller.service_model());
  auto pending = controller.bank_for_save();
  CHECK(pending.valid() && pending.active_slot == 3);
  unsigned generation = bank.generation;
  controller.saved(pending, false);
  CHECK(bank.generation == generation &&
        controller.last_storage == STORAGE_ERROR);
  CHECK(controller.apply(save) == OK);
  for (unsigned i = 0; i < 4; ++i)
    engine.process(in, out, 48);
  CHECK(controller.service_model());
  controller.saved(controller.bank_for_save(), true);
  CHECK(bank.valid() && bank.active_slot == 3);
  controller.bridge_session = 0x12345678;
  state = controller.state();
  CHECK(get32(state.data + 134) == controller.bridge_session);
  for (float frequency : {82.4069f, 146.832f, 196.0f, 246.942f, 329.628f}) {
    for (unsigned i = 0; i < 1024; ++i)
      tone[i] = 0.1f * std::sin(2 * kPi * frequency * i / 12000);
    CHECK(std::fabs(detect_pitch(tone) - frequency) < 1.0f);
  }
  std::printf("Passed %u checks (framing, corruption, presets, parameter "
              "bounds, pickups, real NAM inference, fades, tuner).\n",
              checks);
}
