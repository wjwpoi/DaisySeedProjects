#include "controller.hpp"
#include "daisy_seed.h"
#include "dev/oled_ssd130x.h"
#include "util/CpuLoadMeter.h"
#include <cstdio>

using namespace daisy;
using namespace seedfx;
static DaisySeed hardware;
static OledDisplay<SSD130x4WireSpi128x64Driver> display;
DSY_SDRAM_BSS static AudioBuffers buffers;
NAM_A2_STATE_DATA static nam_a2_daisy::A2Player model;
static AudioEngine audio(buffers, model);
static PresetBank bank;
static Controller control(audio, bank);
static UartHandler uart;
static CpuLoadMeter cpu;
static AnalogControl knobs[6];
static Switch foot[2], encoder_button;
static Encoder encoder;
static GPIO led[2], relay, mute;
DMA_BUFFER_MEM_SECTION static uint8_t uart_rx[256];
static SpscQueue<uint8_t, 1024> rx_queue;
static Decoder decoder;
static KnobTracker pickup[6];
static float tuner_samples[1024]{};
static uint32_t last_ui = 0, last_pitch = 0, last_tap = 0;
static uint16_t last_sequence = 0;
static uint8_t last_result = 0;
static bool have_sequence = false;
static bool save_ack_pending = false;
static uint16_t save_ack_sequence = 0;
static unsigned selected = MODEL, page = 0;
constexpr uint32_t kBankAddress[2] = {0x7f0000, 0x7f1000};
static int active_bank = -1;

static void receive(uint8_t *data, size_t size, void *,
                    UartHandler::Result result) {
  if (result == UartHandler::Result::OK)
    for (size_t i = 0; i < size; ++i)
      rx_queue.push(data[i]);
}
static void send(const Frame &f) {
  uint8_t wire[kMaxFrame]{};
  auto size = encode(f, wire, sizeof(wire));
  if (size)
    uart.BlockingTransmit(wire, size, 20);
}
static void release_knobs() {
  for (auto &p : pickup)
    p.release();
}
static Result apply(const Frame &frame) {
  auto r = control.apply(frame);
  if (r == OK && (frame.type == SET_PARAM || frame.type == LOAD_PRESET ||
                  frame.type == SET_KNOBS))
    release_knobs();
  return r;
}
static bool set_parameter(unsigned id, float value, bool from_knob = false) {
  Frame f;
  f.type = SET_PARAM;
  f.size = 5;
  f.data[0] = id;
  putfloat(f.data + 1, value);
  return (from_knob ? control.apply(f) : apply(f)) == OK;
}
static void load_preset(unsigned index) {
  Frame f;
  f.type = LOAD_PRESET;
  f.size = 1;
  f.data[0] = index;
  apply(f);
}
static void toggle(uint8_t command, bool current) {
  Frame f;
  f.type = command;
  f.size = 1;
  f.data[0] = !current;
  apply(f);
}
static void tap(uint32_t now) {
  if (last_tap && now - last_tap >= 40 && now - last_tap <= 1500)
    set_parameter(DELAY_TIME, float(now - last_tap));
  last_tap = now;
}
static void callback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                     size_t size) {
  cpu.OnBlockStart();
  audio.process(in[0], out[0], size);
  for (size_t i = 0; i < size; ++i)
    out[1][i] = 0.0f;
  cpu.OnBlockEnd();
}
static void storage_init() {
  bank = PresetBank::defaults();
  const auto *a = reinterpret_cast<const PresetBank *>(
      hardware.qspi.GetData(kBankAddress[0]));
  const auto *b = reinterpret_cast<const PresetBank *>(
      hardware.qspi.GetData(kBankAddress[1]));
  const auto *chosen = newest_bank(a, b);
  if (chosen) {
    bank = *chosen;
    active_bank = chosen == a ? 0 : 1;
  }
}
static bool storage_save(const PresetBank &next) {
  int slot = active_bank == 0 ? 1 : 0;
  auto address = kBankAddress[slot];
  hardware.StopAudio();
  bool ok =
      hardware.qspi.Erase(address, address + 4096) == QSPIHandle::Result::OK;
  if (ok)
    ok = hardware.qspi.Write(
             address, sizeof(next),
             reinterpret_cast<uint8_t *>(const_cast<PresetBank *>(&next))) ==
         QSPIHandle::Result::OK;
  if (ok) {
    auto *mapped = static_cast<uint8_t *>(hardware.qspi.GetData(address));
    // QSPI changed through indirect writes; discard any cached old bank lines.
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(mapped), 4096);
    __DSB();
    __ISB();
    const auto *verify = reinterpret_cast<const PresetBank *>(mapped);
    ok = verify->valid() && std::memcmp(verify, &next, sizeof(next)) == 0;
  }
  if (ok)
    active_bank = slot;
  hardware.StartAudio(callback);
  return ok;
}
static void text(int x, int y, const char *value) {
  display.SetCursor(x, y);
  display.WriteString(value, Font_6x8, true);
}
static void draw() {
  display.Fill(false);
  char line[48];
  std::snprintf(line, sizeof(line), "SeedFX P%02u%s %s", control.preset + 1,
                control.dirty ? "*" : "",
                control.busy()                          ? "WAIT"
                : control.last_storage == STORAGE_ERROR ? "FLASH!"
                : control.bypassed                      ? "BYP"
                                                        : "ON");
  text(0, 0, line);
  if (control.tuning) {
    if (control.pitch > 0) {
      float midi = 69 + 12 * std::log2(control.pitch / 440);
      int note = int(std::round(midi));
      constexpr const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                       "F#", "G",  "G#", "A",  "A#", "B"};
      std::snprintf(line, sizeof(line), "%s%d", names[(note % 12 + 12) % 12],
                    note / 12 - 1);
      display.SetCursor(0, 12);
      display.WriteString(line, Font_16x26, true);
      std::snprintf(line, sizeof(line), "%+d cents",
                    int(std::round((midi - note) * 100)));
      text(64, 22, line);
      int center = 64,
          offset = int(std::clamp((midi - note) * 100, -50.0f, 50.0f));
      display.DrawLine(center, 36, center, 55, true);
      display.DrawRect(center + offset - 2, 40, center + offset + 2, 50, true,
                       true);
    } else
      text(0, 24, "Play a single note");
  } else {
    display.SetCursor(0, 10);
    display.WriteString(
        nam_a2_models::kNamA2Models[int(control.settings.values[MODEL])].name,
        Font_11x18, true);
    auto &param = kParameters[selected];
    if (selected >= AMP_ON)
      std::snprintf(line, sizeof(line), "%s: %s", param.label,
                    control.settings.values[selected] > 0.5f ? "ON" : "OFF");
    else if (selected == CAB)
      std::snprintf(line, sizeof(line), "Cab: %s",
                    int(control.settings.values[CAB]) == 0 ? "Rhythm" : "Lead");
    else if (selected == MODEL)
      std::snprintf(line, sizeof(line), "Model %u/%u",
                    unsigned(control.settings.values[MODEL]) + 1,
                    unsigned(nam_a2_models::kNamA2ModelCount));
    else
      std::snprintf(line, sizeof(line), "%s: %.1f %s", param.label,
                    double(control.settings.values[selected]), param.unit);
    text(0, 30, line);
    float normalized = (control.settings.values[selected] - param.min) /
                       (param.max - param.min);
    display.DrawRect(0, 40, 127, 46, true, false);
    display.DrawRect(1, 41, int(1 + normalized * 125), 45, true, true);
    std::snprintf(line, sizeof(line), "CPU %2u/%2u%% %s",
                  unsigned(control.cpu_average * 100),
                  unsigned(control.cpu_maximum * 100),
                  audio.clipped ? "CLIP" : "");
    text(0, 48, line);
    std::snprintf(line, sizeof(line), "%s",
                  page == 0 ? "Click:edit Hold:save" : "Turn:value Click:back");
    text(0, 56, line);
  }
  display.Update();
  led[0].Write(!control.bypassed);
  led[1].Write(audio.clipped || control.busy());
}
int main() {
  hardware.Configure();
  hardware.Init(true);
  hardware.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  hardware.SetAudioBlockSize(kBlockSize);
  audio.clear_tails();
  storage_init();
  control.preset = bank.active_slot;
  control.settings = bank.presets[bank.active_slot];
  audio.set_all(control.settings);
  audio.load_model(unsigned(control.settings.values[MODEL]));
  audio.load_cab(unsigned(control.settings.values[CAB]));
  control.initialized();
  AdcChannelConfig channels[6];
  constexpr Pin knob_pins[] = {seed::D15, seed::D16, seed::D17,
                               seed::D18, seed::D19, seed::D20};
  for (unsigned i = 0; i < 6; ++i)
    channels[i].InitSingle(knob_pins[i]);
  hardware.adc.Init(channels, 6);
  hardware.adc.Start();
  for (unsigned i = 0; i < 6; ++i)
    knobs[i].Init(hardware.adc.GetPtr(i), 1000);
  foot[0].Init(seed::D6, 1000);
  foot[1].Init(seed::D5, 1000);
  encoder.Init(seed::D3, seed::D2, seed::D4, 1000);
  for (unsigned i = 0; i < 2; ++i) {
    led[i].Init(i ? seed::D23 : seed::D22, GPIO::Mode::OUTPUT);
  }
  relay.Init(seed::D1, GPIO::Mode::OUTPUT);
  mute.Init(seed::D12, GPIO::Mode::OUTPUT);
  // Keep hardware relays in the DSP path. Software bypass preserves effect
  // tails.
  mute.Write(true);
  relay.Write(true);
  hardware.DelayMs(10);
  decltype(display)::Config dc;
  dc.driver_config.transport_config.pin_config.dc = seed::D9;
  dc.driver_config.transport_config.pin_config.reset = seed::D11;
  display.Init(dc);
  UartHandler::Config uc;
  uc.periph = UartHandler::Config::Peripheral::USART_1;
  uc.mode = UartHandler::Config::Mode::TX_RX;
  uc.baudrate = 230400;
  uc.pin_config.tx = seed::D13;
  uc.pin_config.rx = seed::D14;
  uart.Init(uc);
  uart.DmaListenStart(uart_rx, sizeof(uart_rx), receive, nullptr);
  cpu.Init(kSampleRate, kBlockSize);
  hardware.StartAudio(callback);
  mute.Write(false);
  uint32_t last_controls = System::GetNow();
  const uint32_t knobs_started = last_controls;
  bool foot0_long = false, foot1_long = false, encoder_long = false;
  while (true) {
    uint32_t now = System::GetNow();
    uint8_t byte;
    Frame frame;
    while (rx_queue.pop(byte))
      if (decoder.feed(byte, frame)) {
        if (frame.type == HELLO && frame.size == 4) {
          auto session = get32(frame.data);
          if (control.bridge_session != session) {
            control.bridge_session = session;
            have_sequence = false;
          }
          continue;
        }
        if (frame.type == GET_STATE && frame.size == 0) {
          send(control.state(frame.seq));
          continue;
        }
        Result result;
        if (save_ack_pending && frame.seq == save_ack_sequence)
          continue;
        if (have_sequence && frame.seq == last_sequence)
          result = Result(last_result);
        else {
          result = frame.type == TAP && frame.size == 0 ? (tap(now), OK)
                                                        : apply(frame);
          last_sequence = frame.seq;
          last_result = result;
          have_sequence = true;
        }
        if (frame.type == SAVE_PRESET && result == OK && control.save_pending) {
          save_ack_pending = true;
          save_ack_sequence = frame.seq;
          continue;
        }
        Frame ack;
        ack.type = ACK;
        ack.seq = frame.seq;
        ack.size = 1;
        ack.data[0] = result;
        send(ack);
      }
    if (now - last_controls >= 1) {
      last_controls = now;
      for (unsigned i = 0; i < 6; ++i) {
        knobs[i].Process();
        float value = knobs[i].Value();
        unsigned id = control.settings.knobs[i];
        const auto &p = kParameters[id];
        float target = (control.settings.values[id] - p.min) / (p.max - p.min);
        if (now - knobs_started >= 1000 && pickup[i].changed(value, target)) {
          if (set_parameter(id, p.min + value * (p.max - p.min), true)) {
            pickup[i].transmitted(value);
            selected = id;
            for (unsigned j = 0; j < 6; ++j)
              if (j != i && control.settings.knobs[j] == id)
                pickup[j].release();
          }
        }
      }
      for (auto &f : foot)
        f.Debounce();
      encoder.Debounce();
      if (foot[0].RisingEdge())
        foot0_long = false;
      if (foot[0].TimeHeldMs() > 700 && !foot0_long) {
        toggle(TUNER, control.tuning);
        foot0_long = true;
      }
      if (foot[0].FallingEdge() && !foot0_long) {
        if (control.tuning)
          toggle(TUNER, true);
        else
          toggle(BYPASS, control.bypassed);
      }
      if (foot[1].RisingEdge())
        foot1_long = false;
      if (foot[1].TimeHeldMs() > 700 && !foot1_long) {
        load_preset((control.preset + 1) % kPresetCount);
        foot1_long = true;
      }
      if (foot[1].FallingEdge() && !foot1_long)
        tap(now);
      if (encoder.RisingEdge())
        encoder_long = false;
      if (encoder.TimeHeldMs() > 1000 && !encoder_long) {
        Frame save;
        save.type = SAVE_PRESET;
        save.size = 1;
        save.data[0] = control.preset;
        apply(save);
        encoder_long = true;
      }
      if (encoder.FallingEdge() && !encoder_long)
        page = !page;
      int increment = encoder.Increment();
      if (increment) {
        if (!page) {
          int next = int(selected) + increment;
          selected = unsigned((next % int(PARAM_COUNT) + int(PARAM_COUNT)) %
                              int(PARAM_COUNT));
        } else {
          auto &p = kParameters[selected];
          float step =
              (selected == MODEL || selected == CAB || selected >= AMP_ON)
                  ? 1
                  : (p.max - p.min) / 100;
          set_parameter(selected, std::clamp(control.settings.values[selected] +
                                                 increment * step,
                                             p.min, p.max));
        }
      }
    }
    control.prepare();
    bool prepared = control.service_model();
    if (control.save_pending && audio.paused() && prepared) {
      auto next = control.bank_for_save();
      bool ok = storage_save(next);
      control.saved(next, ok);
      if (save_ack_pending) {
        Frame ack;
        ack.type = ACK;
        ack.seq = save_ack_sequence;
        ack.size = 1;
        ack.data[0] = ok ? OK : STORAGE_ERROR;
        last_result = ack.data[0];
        send(ack);
        save_ack_pending = false;
      }
    }
    control.cpu_average = cpu.GetAvgCpuLoad();
    control.cpu_maximum = cpu.GetMaxCpuLoad();
    if (control.tuning && now - last_pitch >= 100 &&
        audio.tuner_snapshot(tuner_samples)) {
      last_pitch = now;
      control.pitch = detect_pitch(tuner_samples);
    }
    if (now - last_ui >= 50) {
      last_ui = now;
      draw();
    }
  }
}
