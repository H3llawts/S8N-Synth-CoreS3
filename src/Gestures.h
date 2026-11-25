#pragma once
#include <stdint.h>

// Motion and hand distance supply temporary modulation relative to the
// selected voice's stored cutoff and reverb. update() takes milliseconds.
class Gestures {
 public:
  void begin();
  void update(uint32_t nowMs);
  void enable(bool on);
  bool calibrate();
  bool enabled() const { return enabled_; }
  bool tiltAvailable() const { return tilt_.fresh; }
  bool proximityAvailable() const { return proximity_.fresh; }
  int tiltOffset() const { return enabled_ ? tilt_.offset : 0; }
  int proximityOffset() const { return enabled_ ? proximity_.offset : 0; }

 private:
  struct Channel {
    bool fresh = false;
    uint32_t lastSample = 0;
    float raw = 0, filtered = 0, center = 0;
    int offset = 0;
    void observe(float value, uint32_t now, float scale, int limit);
    void expire(uint32_t now);
    void zero();
  };
  Channel tilt_, proximity_;
  bool enabled_ = false, proximityConfigured_ = false;
  uint32_t lastPoll_ = 0, lastProbe_ = 0;
};
