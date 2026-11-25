#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
class HardwareSerial {
 public:
  uint8_t out[8192]{}, in[256]{};
  size_t count = 0, readHead = 0, readEnd = 0;
  size_t write(const uint8_t* data, size_t n) {
    if (count + n > sizeof(out)) return 0;
    memcpy(out + count, data, n);
    count += n;
    return n;
  }
  int available() { return readEnd - readHead; }
  int read() { return available() ? in[readHead++] : -1; }
  template<size_t N> void feed(const uint8_t (&data)[N]) {
    memcpy(in, data, N);
    readHead = 0;
    readEnd = N;
  }
};
