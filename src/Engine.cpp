#include "Engine.h"

#include <Arduino.h>
#include <string.h>

namespace {
constexpr uint8_t MIDI_DRUMS = 9;
constexpr uint8_t LOCAL_KEYS = 16;
constexpr uint8_t CARD_KEYS = 17;
constexpr uint8_t EMPTY = 255;
constexpr uint8_t defaults[] = {36, 38, 42, 36, 67, 60, 60, 60};
constexpr const char* names[] = {"Kick", "Snare", "Hat", "Bass", "Lead", "Chord", "Sample A", "Sample B"};
bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

uint8_t scaled(uint8_t velocity, uint8_t level) {
  return (uint16_t(velocity) * level + 63) / 127;
}
} // namespace

const char* Engine::trackName(uint8_t lane) {
  return lane < S8N_TRACKS ? names[lane] : "";
}

uint8_t Engine::defaultNote(uint8_t lane) {
  return lane < S8N_TRACKS ? defaults[lane] : 60;
}

void Engine::factory(Project& dest) {
  memset(&dest, 0, sizeof(dest));
  dest.bpm = 110;
  dest.swing = 56;
  const uint8_t programs[] = {0, 0, 0, 38, 81, 89, 0, 0};
  const uint8_t levels[] = {117, 108, 85, 105, 82, 68, 105, 105};
  for (uint8_t lane = 0; lane < S8N_TRACKS; ++lane) {
    dest.tracks[lane] = {programs[lane], levels[lane], 64, uint8_t(lane == 5 ? 45 : 18),
                         uint8_t(lane == 7 ? 1 : 0), 0};
    for (uint8_t p = 0; p < S8N_PATTERNS; ++p) {
      for (Step& step : dest.steps[p][lane]) step = {defaultNote(lane), 0, 70};
    }
  }
  // Four playable variations. The sample lanes are free for a first mic take.
  const uint16_t kicks[]  = {0x0509, 0x4111, 0x0941, 0x8909};
  const uint16_t snares[] = {0x1010, 0x1010, 0x1090, 0x5010};
  const uint16_t hats[]   = {0x5555, 0xd555, 0x7775, 0xd5d5};
  const uint8_t roots[] = {36, 41, 43, 36};
  const uint8_t bassAt[] = {0, 3, 6, 9, 13};
  const uint8_t bassOffset[] = {0, 12, 7, 0, 7};
  const uint8_t leadAt[] = {2, 7, 10, 14};
  const uint8_t leadOffset[] = {7, 12, 4, 7};
  for (uint8_t p = 0; p < S8N_PATTERNS; ++p) {
    auto& bar = dest.steps[p];
    for (uint8_t s = 0; s < S8N_STEPS; ++s) {
      const uint16_t bit = uint16_t(1) << s;
      if (kicks[p] & bit) bar[0][s].velocity = s % 4 ? 101 : 121;
      if (snares[p] & bit) bar[1][s].velocity = s % 4 ? 55 : 116;
      if (hats[p] & bit) bar[2][s].velocity = s % 4 ? 78 : 101;
    }
    for (uint8_t i = 0; i < sizeof(bassAt); ++i)
      bar[3][bassAt[i]] = {uint8_t(roots[p] + bassOffset[i]), uint8_t(i % 2 ? 92 : 112), 85};
    for (uint8_t i = 0; i < sizeof(leadAt); ++i)
      bar[4][leadAt[i]] = {uint8_t(roots[p] + 24 + leadOffset[i]), uint8_t(90 - i * 5), 62};
    bar[5][4] = {uint8_t(roots[p] + 12), 83, 95};
    bar[5][12] = {uint8_t(roots[p] + 19), 78, 85};
  }
}

bool Engine::valid(const Project& data) {
  if (data.bpm < 40 || data.bpm > 240 || data.swing < 50 || data.swing > 75) return false;
  for (const Track& lane : data.tracks) {
    if (lane.program > 127 || lane.volume > 127 || lane.cutoff > 127 ||
        lane.reverb > 127 || lane.sampleSlot >= 4 || lane.muted > 1) return false;
  }
  for (const auto& bar : data.steps)
    for (const auto& lane : bar)
      for (const Step& step : lane)
        if (step.note > 127 || step.velocity > 127 || !step.gate || step.gate > 100) return false;
  return true;
}

void Engine::begin(HardwareSerial& port, SampleTrigger sample) {
  midi_ = &port;
  sample_ = sample;
  pattern = track = 0;
  factory(project);
  applyProject();
}

void Engine::message(uint8_t status, uint8_t a, uint8_t b) {
  const uint8_t bytes[] = {status, a, b};
  if (midi_) midi_->write(bytes, sizeof(bytes));
}

void Engine::programChange(uint8_t channel, uint8_t program) {
  const uint8_t bytes[] = {uint8_t(0xc0 | channel), program};
  if (midi_) midi_->write(bytes, sizeof(bytes));
}

void Engine::nrpn(uint8_t channel, uint8_t msb, uint8_t lsb, uint8_t value) {
  message(0xb0 | channel, 99, msb);
  message(0xb0 | channel, 98, lsb);
  message(0xb0 | channel, 6, value);
}

void Engine::applyTrack(uint8_t lane) {
  if (lane < 3 || lane > 5) return;
  const uint8_t channel = lane - 3;
  const Track& t = project.tracks[lane];
  programChange(channel, t.program);
  message(0xb0 | channel, 7, t.volume);
  sentCutoff_[channel] = sentReverb_[channel] = EMPTY;
  tone(lane, t.cutoff, t.reverb);
}

void Engine::tone(uint8_t lane, uint8_t cutoff, uint8_t reverb) {
  if (lane < 3 || lane > 5) return;
  const uint8_t channel = lane - 3;
  if (reverb != sentReverb_[channel]) {
    message(0xb0 | channel, 91, reverb);
    sentReverb_[channel] = reverb;
  }
  if (cutoff != sentCutoff_[channel]) {
    nrpn(channel, 1, 0x20, cutoff);
    sentCutoff_[channel] = cutoff;
  }
}

void Engine::applyProject() {
  stop();
  memset(drumReverb_, EMPTY, sizeof(drumReverb_));
  programChange(MIDI_DRUMS, 0);
  message(0xb0 | MIDI_DRUMS, 7, 110);
  for (uint8_t lane = 3; lane <= 5; ++lane) applyTrack(lane);
}

bool Engine::sound(uint8_t lane, uint8_t note, uint8_t velocity, bool on) {
  const Track& t = project.tracks[lane];
  if (lane < 3 || lane >= 6) velocity = scaled(velocity, t.volume);
  if (on && !velocity) return false;
  if (lane >= 6) {
    if (on && sample_) sample_(t.sampleSlot, velocity);
    return false; // Sample playback owns the lifetime of its one-shot.
  }
  const uint8_t part = lane < 3 ? 3 : lane - 3;
  const uint8_t channel = lane < 3 ? MIDI_DRUMS : part;
  if (on && lane < 3 && drumReverb_[note] != t.reverb) {
    nrpn(channel, 0x1d, note, t.reverb);
    drumReverb_[note] = t.reverb;
  }
  const uint8_t intervals[] = {0, 4, 7};
  const uint8_t count = lane == 5 ? 3 : 1;
  for (uint8_t i = 0; i < count; ++i) {
    const unsigned pitch = note + intervals[i];
    if (pitch > 127) continue;
    uint8_t& owners = voices_[part][pitch];
    if (on) {
      if (owners && lane < 3) message(0x80 | channel, pitch, 0);
      if (owners == 0 || lane < 3) message(0x90 | channel, pitch, velocity);
      ++owners;
    } else if (owners && --owners == 0) {
      message(0x80 | channel, pitch, 0);
    }
  }
  return true;
}

void Engine::releaseGate(uint8_t lane) {
  Gate& gate = gates_[lane];
  if (gate.active) sound(lane, gate.note, 0, false);
  gate.active = false;
}

void Engine::setMute(uint8_t lane, bool muted) {
  if (lane >= S8N_TRACKS) return;
  project.tracks[lane].muted = muted;
  if (!muted) return;
  if (lane < 6) releaseGate(lane);
  for (uint8_t src = 0; src <= CARD_KEYS; ++src) {
    for (unsigned pitch = 0; pitch < 128; ++pitch)
      if (held_[src][pitch] == lane + 1) liveOff(src, pitch);
  }
}

void Engine::selectPattern(uint8_t index) {
  if (index >= S8N_PATTERNS || index == pattern) return;
  pattern = index;
  memset(skipNext_, EMPTY, sizeof(skipNext_));
}

void Engine::start(uint32_t now) {
  if (playing) return;
  playing = true;
  nextStep_ = playhead = 0;
  haveStep_ = false;
  nextTime_ = now;
  memset(skipNext_, EMPTY, sizeof(skipNext_));
}

void Engine::panic() {
  memset(held_, 0, sizeof(held_));
  memset(voices_, 0, sizeof(voices_));
  memset(gates_, 0, sizeof(gates_));
  for (uint8_t ch = 0; ch < 16; ++ch) {
    message(0xb0 | ch, 64, 0);
    message(0xb0 | ch, 120, 0);
    message(0xb0 | ch, 123, 0);
  }
}

void Engine::stop() {
  playing = recording = haveStep_ = false;
  playhead = nextStep_ = 0;
  runningStatus_ = dataCount_ = 0;
  memset(skipNext_, EMPTY, sizeof(skipNext_));
  panic();
}

void Engine::playStep(uint32_t now) {
  const uint32_t pair = 30000000UL / project.bpm;
  const uint32_t first = pair * project.swing / 100;
  // Keep the musical phase over a slow SD call. Skip missed hits rather than
  // transmitting a burst of old notes. Deadlines use modulo-2^32 arithmetic.
  const uint32_t bar = pair * 8;
  const uint32_t late = now - nextTime_;
  if (late >= bar) nextTime_ += (late / bar) * bar;
  const uint32_t firstDue = nextTime_;
  do {
    playhead = nextStep_;
    stepTime_ = nextTime_;
    stepLength_ = (playhead & 1) ? pair - first : first;
    nextTime_ += stepLength_;
    nextStep_ = (playhead + 1) % S8N_STEPS;
  } while (due(now, nextTime_));
  haveStep_ = true;
  const bool skipped = late >= bar || firstDue != stepTime_;
  for (uint8_t lane = 0; lane < S8N_TRACKS; ++lane) {
    if (lane < 6) releaseGate(lane);
    const Step& step = project.steps[pattern][lane][playhead];
    const bool monitored = !skipped && skipNext_[lane] == playhead;
    skipNext_[lane] = EMPTY;
    if (!step.velocity || project.tracks[lane].muted || monitored) continue;
    const uint32_t end = stepTime_ + stepLength_ * step.gate / 100;
    if (lane < 6 && due(now, end)) continue;
    if (sound(lane, step.note, step.velocity, true)) gates_[lane] = {end, step.note, true};
  }
}

void Engine::tick(uint32_t now) {
  for (unsigned pitch = 0; pitch < 128; ++pitch)
    if (held_[CARD_KEYS][pitch] && due(now, tapEnds_[pitch])) liveOff(CARD_KEYS, pitch);
  for (uint8_t lane = 0; lane < 6; ++lane)
    if (gates_[lane].active && due(now, gates_[lane].end)) releaseGate(lane);
  if (playing && due(now, nextTime_)) playStep(now);
  // Bound work per UI pass, including when another device sends dense SysEx.
  if (midi_) {
    for (unsigned i = 0; i < 64 && midi_->available(); ++i) {
      const int value = midi_->read();
      if (value >= 0) midiByte(uint8_t(value), now);
    }
  }
}

void Engine::liveOn(uint8_t source, uint8_t note, uint8_t velocity, uint32_t now) {
  if (note > 127 || velocity > 127 || track >= S8N_TRACKS) return;
  if (playing && due(now, nextTime_)) playStep(now);
  liveOff(source, note);
  if (!velocity || project.tracks[track].muted) return;
  if (recording && playing) {
    const bool upcoming = !haveStep_ || (now - stepTime_ >= stepLength_ / 2);
    const uint8_t s = upcoming ? nextStep_ : playhead;
    Step& dest = project.steps[pattern][track][s];
    dest.note = note;
    dest.velocity = velocity;
    if (upcoming) skipNext_[track] = s;
  }
  if (sound(track, note, velocity, true)) held_[source][note] = track + 1;
}

void Engine::liveOff(uint8_t source, uint8_t note) {
  if (note > 127) return;
  uint8_t& owner = held_[source][note];
  if (owner) sound(owner - 1, note, 0, false);
  owner = 0;
}

void Engine::noteOn(uint8_t note, uint8_t velocity, uint32_t now) {
  liveOn(LOCAL_KEYS, note, velocity, now);
}

void Engine::noteOff(uint8_t note) {
  liveOff(LOCAL_KEYS, note);
}

void Engine::tap(uint8_t note, uint8_t velocity, uint32_t now, uint32_t durationUs) {
  if (note > 127) return;
  liveOn(CARD_KEYS, note, velocity, now);
  tapEnds_[note] = now + durationUs;
}

void Engine::midiByte(uint8_t value, uint32_t now) {
  if (value >= 0xf8) return;  // Realtime bytes can occur inside any message.
  if (value & 0x80) {
    runningStatus_ = value < 0xf0 ? value : 0;
    dataCount_ = 0;
    return;
  }
  if (!runningStatus_) return;
  const uint8_t type = runningStatus_ >> 4;
  if (type == 0xc || type == 0xd) return; // One-byte channel messages.
  if (!dataCount_) {
    firstData_ = value;
    dataCount_ = 1;
    return;
  }
  dataCount_ = 0;
  const uint8_t source = runningStatus_ & 15;
  if (type == 9 && value) liveOn(source, firstData_, value, now);
  else if (type == 8 || type == 9) liveOff(source, firstData_);
  else if (type == 0xb && (firstData_ == 120 || firstData_ == 123)) {
    for (unsigned pitch = 0; pitch < 128; ++pitch) liveOff(source, pitch);
  }
}
