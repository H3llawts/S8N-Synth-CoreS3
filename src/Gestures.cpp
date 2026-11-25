#include "Gestures.h"
#include <M5Unified.h>
#include <cmath>

namespace {
constexpr uint8_t ltrAddress = 0x23;
constexpr uint32_t busHz = 100000;
constexpr uint32_t pollMs = 40;
constexpr uint32_t staleMs = 350;
constexpr uint32_t retryMs = 2000;

bool configureProximity() {
  auto& bus = M5.In_I2C;
  uint8_t identity[2]{};
  if (!bus.readRegister(ltrAddress, 0x86, identity, 2, busHz) ||
      (identity[0] >> 4) != 9 || identity[1] != 0x05) return false;
  // LTR-553ALS-WA: four LED pulses, one measurement per 50 ms, active
  // x16 gain with the saturation indicator enabled.
  return bus.writeRegister8(ltrAddress, 0x83, 4, busHz) &&
         bus.writeRegister8(ltrAddress, 0x84, 0, busHz) &&
         bus.writeRegister8(ltrAddress, 0x81, 0x22, busHz);
}
}

void Gestures::Channel::zero() {
  center = filtered = raw;
  offset = 0;
}

void Gestures::Channel::expire(uint32_t now) {
  if (fresh && now - lastSample > staleMs) {
    fresh = false;
    offset = 0;
  }
}

void Gestures::Channel::observe(float value, uint32_t now, float scale, int limit) {
  raw = value;
  lastSample = now;
  if (!fresh) {
    fresh = true;
    zero();
    return;
  }
  filtered += 0.35f * (raw - filtered);
  int target = static_cast<int>(std::round((filtered - center) * scale));
  if (target < -limit) target = -limit;
  if (target > limit) target = limit;
  if (std::abs(target) <= 1) target = 0;
  if (target == 0 || std::abs(target - offset) >= 2) offset = target;
}

void Gestures::begin() {
  enabled_ = false;
  tilt_ = {};
  proximity_ = {};
  lastProbe_ = millis();
  lastPoll_ = lastProbe_ - pollMs;
  proximityConfigured_ = configureProximity();
}

void Gestures::enable(bool on) {
  if (enabled_ == on) return;
  enabled_ = on;
  calibrate();
}

bool Gestures::calibrate() {
  // An unavailable channel centers on its first subsequent valid reading.
  tilt_.zero();
  proximity_.zero();
  return tilt_.fresh || proximity_.fresh;
}

void Gestures::update(uint32_t now) {
  if (now - lastPoll_ < pollMs) return;
  lastPoll_ = now;
  tilt_.expire(now);
  proximity_.expire(now);

  if (M5.Imu.isEnabled() && (M5.Imu.update() & m5::IMU_Class::sensor_mask_accel)) {
    const auto& a = M5.Imu.getImuData().accel;
    const float g2 = a.x*a.x + a.y*a.y + a.z*a.z;
    // Normalize gravity; reject free fall and vigorous shakes.
    if (g2 > 0.49f && g2 < 1.69f) {
      tilt_.observe(a.x / std::sqrt(g2), now, 100.0f, 64);
    }
  }
  if (!proximityConfigured_) {
    if (now - lastProbe_ < retryMs) return;
    lastProbe_ = now;
    proximityConfigured_ = configureProximity();
    return;
  }
  uint8_t status = 0, sample[2]{};
  auto& bus = M5.In_I2C;
  if (!bus.readRegister(ltrAddress, 0x8C, &status, 1, busHz)) {
    proximityConfigured_ = false;
    lastProbe_ = now;
    return;
  }
  if (!(status & 0x01)) {
    // A chip reset can put the emitter in standby while I2C still responds.
    if (now - lastProbe_ >= retryMs) {
      lastProbe_ = now;
      proximityConfigured_ = configureProximity();
    }
    return;
  }
  if (!bus.readRegister(ltrAddress, 0x8D, sample, 2, busHz)) {
    proximityConfigured_ = false;
    lastProbe_ = now;
    return;
  }
  lastProbe_ = now;
  if (!(sample[1] & 0x80)) {
    const unsigned counts = (unsigned(sample[1] & 7) << 8) | sample[0];
    proximity_.observe(static_cast<float>(counts), now, 1.0f / 8.0f, 127);
  }
}
