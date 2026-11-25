#pragma once
#include <stdint.h>
#include <stddef.h>

extern uint32_t fakeMillis;
inline uint32_t millis() { return fakeMillis; }

namespace m5 {
struct IMU_Class {
  enum { sensor_mask_accel = 1 };
  struct Reading { struct { float x = 0, y = 0, z = 1; } accel; } reading;
  bool online = true;
  bool fresh = true;
  bool isEnabled() const { return online; }
  unsigned update() const { return fresh ? 1 : 0; }
  const Reading& getImuData() const { return reading; }
};
}

struct GestureTestBus {
  bool online = true, fresh = true, saturated = false;
  uint8_t part = 0x92, manufacturer = 5;
  unsigned counts = 32, dataReads = 0, configWrites = 0;
  bool readRegister(uint8_t address, uint8_t reg, uint8_t* data, size_t len, uint32_t hz) {
    if (!online || address != 0x23 || hz != 100000) return false;
    switch (reg) {
      case 0x86:
        if (len != 2) return false;
        data[0] = part; data[1] = manufacturer; return true;
      case 0x8C:
        if (len != 1) return false;
        data[0] = fresh; return true;
      case 0x8D:
        if (len != 2) return false;
        ++dataReads; data[0] = counts & 255;
        data[1] = (counts >> 8) | (saturated ? 0x80 : 0); return true;
      default: return false;
    }
  }
  bool writeRegister8(uint8_t address, uint8_t reg, uint8_t value, uint32_t hz) {
    if (!online || address != 0x23 || hz != 100000) return false;
    ++configWrites;
    return (reg == 0x83 && value == 4) || (reg == 0x84 && value == 0) ||
           (reg == 0x81 && value == 0x22);
  }
};
struct GestureTestBoard { m5::IMU_Class Imu; GestureTestBus In_I2C; };
extern GestureTestBoard M5;
