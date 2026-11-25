#pragma once

#include <stdint.h>

class HardwareSerial;

constexpr uint8_t S8N_TRACKS = 8;
constexpr uint8_t S8N_PATTERNS = 4;
constexpr uint8_t S8N_STEPS = 16;

struct Step {
  uint8_t note;
  uint8_t velocity;  // Zero is a rest.
  uint8_t gate;      // Percentage of this swung sixteenth, 1..100.
};

struct Track {
  uint8_t program;
  uint8_t volume;
  uint8_t cutoff;
  uint8_t reverb;
  uint8_t sampleSlot;
  uint8_t muted;
};

struct Project {
  uint16_t bpm;
  uint8_t swing;  // Long sixteenth as a percentage of an eighth note.
  Track tracks[S8N_TRACKS];
  Step steps[S8N_PATTERNS][S8N_TRACKS][S8N_STEPS];
};

class Engine {
 public:
  using SampleTrigger = void (*)(uint8_t slot, uint8_t velocity);
  Project project{};
  uint8_t pattern = 0, track = 0, playhead = 0;
  bool playing = false, recording = false;

  // The caller owns UART setup. begin() installs the factory project.
  void begin(HardwareSerial& port, SampleTrigger sample);
  void start(uint32_t now);
  void stop();
  void tick(uint32_t now);
  void selectPattern(uint8_t index);
  void setMute(uint8_t lane, bool muted);
  void applyTrack(uint8_t lane);
  void applyProject();
  void noteOn(uint8_t note, uint8_t velocity, uint32_t now);
  void noteOff(uint8_t note);
  // The touch keyboard has releases. CardKB reports presses only.
  void tap(uint8_t note, uint8_t velocity, uint32_t now, uint32_t durationUs);
  void tone(uint8_t lane, uint8_t cutoff, uint8_t reverb);
  void panic();

  static void factory(Project& dest);
  static bool valid(const Project& data);
  static uint8_t defaultNote(uint8_t lane);
  static const char* trackName(uint8_t lane);

 private:
  HardwareSerial* midi_ = nullptr;
  SampleTrigger sample_ = nullptr;
  uint32_t nextTime_ = 0, stepTime_ = 0, stepLength_ = 1;
  uint8_t nextStep_ = 0;
  bool haveStep_ = false;

  struct Gate { uint32_t end; uint8_t note; bool active; };
  Gate gates_[6]{};
  // Notes shared by live keys, sequenced steps and chord intervals release
  // only when their last owner lets go. Indices 0..2 are melodic, 3 is drums.
  uint8_t voices_[4][128]{};
  // Each input channel and the physical keyboard own independent key releases.
  uint8_t held_[18][128]{}; // MIDI channels, touch, CardKB; lane + 1, or zero.
  uint32_t tapEnds_[128]{};
  uint8_t sentCutoff_[3]{}, sentReverb_[3]{};
  uint8_t drumReverb_[128]{};
  uint8_t skipNext_[S8N_TRACKS]{};
  uint8_t runningStatus_ = 0, dataCount_ = 0, firstData_ = 0;

  void message(uint8_t status, uint8_t a, uint8_t b);
  void programChange(uint8_t channel, uint8_t program);
  void nrpn(uint8_t channel, uint8_t msb, uint8_t lsb, uint8_t value);
  bool sound(uint8_t lane, uint8_t note, uint8_t velocity, bool on);
  void releaseGate(uint8_t lane);
  void playStep(uint32_t now);
  void liveOn(uint8_t source, uint8_t note, uint8_t velocity, uint32_t now);
  void liveOff(uint8_t source, uint8_t note);
  void midiByte(uint8_t value, uint32_t now);
};
