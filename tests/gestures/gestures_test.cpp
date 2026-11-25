#include "Gestures.h"
#include "M5Unified.h"
#include <cassert>
#include <cstdio>

uint32_t fakeMillis = UINT32_MAX - 110;
GestureTestBoard M5;

static void advance(Gestures& gestures, unsigned count = 1) {
  while (count--) {
    fakeMillis += 40;
    gestures.update(fakeMillis);
  }
}

int main() {
  Gestures gestures;
  gestures.begin();
  assert(M5.In_I2C.configWrites == 3);
  assert(!gestures.enabled() && !gestures.calibrate());
  gestures.update(fakeMillis);
  assert(gestures.tiltAvailable() && gestures.proximityAvailable());
  gestures.update(fakeMillis + 20);
  assert(M5.In_I2C.dataReads == 1);

  M5.Imu.reading.accel = {0.6f, 0, 0.8f};
  M5.In_I2C.counts = 400;
  advance(gestures, 6);  // Cross the millis() rollover with both channels.
  assert(gestures.tiltOffset() == 0 && gestures.proximityOffset() == 0);
  gestures.enable(true);
  assert(gestures.tiltOffset() == 0 && gestures.proximityOffset() == 0);
  M5.Imu.reading.accel = {0, 0, 1};
  M5.In_I2C.counts = 2000;
  advance(gestures);
  assert(gestures.tiltOffset() < 0 && gestures.tiltOffset() > -55);
  assert(gestures.proximityOffset() > 0 && gestures.proximityOffset() < 100);
  advance(gestures, 14);
  assert(gestures.tiltOffset() <= -58 && gestures.proximityOffset() == 127);
  assert(gestures.calibrate());
  M5.In_I2C.counts = 2005;
  advance(gestures, 5);
  assert(!gestures.tiltOffset() && !gestures.proximityOffset());

  M5.Imu.reading.accel = {-0.8f, 0, 0.6f};
  M5.In_I2C.counts = 16;
  advance(gestures, 20);
  assert(gestures.tiltOffset() == -64 && gestures.proximityOffset() == -127);
  M5.In_I2C.saturated = true;
  advance(gestures, 10);
  assert(gestures.tiltAvailable() && !gestures.proximityAvailable());
  assert(gestures.tiltOffset() == -64 && !gestures.proximityOffset());
  M5.In_I2C.saturated = false;
  advance(gestures);
  assert(gestures.proximityAvailable() && !gestures.proximityOffset());
  M5.Imu.reading.accel = {2, 1, 1};
  M5.In_I2C.fresh = false;
  advance(gestures, 51);
  assert(!gestures.tiltAvailable() && !gestures.proximityAvailable());
  assert(!gestures.tiltOffset() && !gestures.proximityOffset());
  assert(M5.In_I2C.configWrites == 6);

  M5.Imu.reading.accel = {0, 0, 1};
  M5.In_I2C.fresh = true;
  advance(gestures);
  assert(gestures.tiltAvailable() && gestures.proximityAvailable());
  assert(!gestures.tiltOffset() && !gestures.proximityOffset());
  M5.In_I2C.online = false;
  advance(gestures, 11);
  assert(!gestures.proximityAvailable() && gestures.tiltAvailable());
  M5.In_I2C.online = true;
  advance(gestures, 50);
  assert(gestures.proximityAvailable());
  gestures.enable(false);
  assert(!gestures.enabled() && !gestures.tiltOffset() && !gestures.proximityOffset());

  M5.In_I2C.manufacturer = 0;
  M5.In_I2C.configWrites = 0;
  gestures.begin();
  advance(gestures, 55);
  assert(!gestures.proximityAvailable() && M5.In_I2C.configWrites == 0);
  std::puts("PASS: gesture calibration, timing, smoothing, drift, saturation and recovery");
}
