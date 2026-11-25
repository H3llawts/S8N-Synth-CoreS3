#include "Arduino.h"
#include "Engine.h"
#include <assert.h>
#include <stdio.h>

HardwareSerial uart;
Engine e;
unsigned callbacks = 0;
uint8_t lastSlot, lastVelocity;
void sample(uint8_t slot, uint8_t velocity) {
  ++callbacks;
  lastSlot = slot;
  lastVelocity = velocity;
}
void blank() {
  e.begin(uart, sample);
  for (auto& pattern : e.project.steps)
    for (auto& track : pattern)
      for (Step& step : track)
        step.velocity = 0;
  e.project.bpm = 120;
  e.project.swing = 60;
  uart.count = 0;
  callbacks = 0;
}
void hasNote(unsigned index, uint8_t status, uint8_t pitch, uint8_t velocity) {
  assert(index + 3 <= uart.count);
  assert(uart.out[index] == status && uart.out[index+1] == pitch && uart.out[index+2] == velocity);
}
int main() {
  blank(); e.track=4;
  e.noteOn(60,100,0xfffffff0U);
  e.tap(60,100,0xfffffff0U,100);
  e.tick(85); assert(uart.count==3);  // Finger still holds the same pitch.
  e.noteOff(60); assert(uart.count==6);
  e.tap(62,100,100,150); e.tick(249); assert(uart.count==9);
  e.tick(250); hasNote(9,0x81,62,0);
  uart.count=0;
  e.tone(4,80,54); assert(uart.count==12);
  e.tone(4,80,54); assert(uart.count==12);
  assert(e.project.tracks[4].cutoff==64);
  e.begin(uart, sample);
  assert(Engine::valid(e.project));
  for (const auto& pattern : e.project.steps) {
    for (uint8_t i=0; i<6; ++i) {
      unsigned hits = 0;
      for (auto s : pattern[i]) if (s.velocity) ++hits;
      assert(hits);
    }
  }
  auto bad = e.project;
  bad.steps[3][7][15].gate = 0;
  assert(!Engine::valid(bad));
  bad = e.project; bad.tracks[7].sampleSlot = 4; assert(!Engine::valid(bad));
  bad = e.project; bad.bpm = 0; assert(!Engine::valid(bad));
  bad = e.project; bad.bpm = 241; assert(!Engine::valid(bad));
  bad = e.project; bad.swing = 76; assert(!Engine::valid(bad));
  bad = e.project; bad.tracks[0].muted = 2; assert(!Engine::valid(bad));
  bad = e.project; bad.tracks[4].cutoff = 128; assert(!Engine::valid(bad));
  bad = e.project; bad.tracks[5].program = 255; assert(!Engine::valid(bad));
  bad = e.project; bad.steps[1][2][5].note = 255; assert(!Engine::valid(bad));
  blank();
  e.project.steps[0][3][0] = {60,120,50};
  const uint32_t offset = 0xffffff00U;
  e.start(offset);
  e.tick(offset);
  assert(e.playhead == 0 && uart.count == 3);
  hasNote(0,0x90,60,120);
  e.tick(offset+74999); assert(uart.count == 3);
  e.tick(offset+75000); hasNote(3,0x80,60,0);
  e.tick(offset+149999); assert(e.playhead == 0);
  e.tick(offset+150000); assert(e.playhead == 1);
  e.tick(offset+249999); assert(e.playhead == 1);
  e.tick(offset+250000); assert(e.playhead == 2);
  e.tick(offset+8200000); assert(e.playhead == 1 && uart.count == 6);
  blank();
  e.track = 5;
  e.noteOn(60,100,0); e.noteOn(64,99,0);
  // The pressed triads share the E. Its note-off must wait for both keys.
  assert(uart.count == 15);
  e.track = 4; e.noteOff(60);
  hasNote(15,0x82,60,0); hasNote(18,0x82,67,0);
  e.noteOff(64); hasNote(21,0x82,64,0);
  e.track = 5; uart.count = 0;
  e.noteOn(126,88,0); assert(uart.count == 3); e.noteOff(126); assert(uart.count == 6);
  blank();
  e.track = 4;
  const uint8_t midiIn[] = {0x93,60,0xf8,91,61,90,0xdc,4,0x93,62,0,0x83,60,30,0xb3,123,0};
  uart.feed(midiIn); e.tick(0);
  assert(uart.count == 12);
  hasNote(0,0x91,60,91); hasNote(3,0x91,61,90);
  hasNote(6,0x81,60,0); hasNote(9,0x81,61,0);
  const uint8_t isolated[] = {0x90,60,80,0x91,60,80,0x80,60,0};
  uart.count = 0; uart.feed(isolated); e.tick(0);
  assert(uart.count == 3);
  const uint8_t off[] = {0x81,60,0}; uart.feed(off); e.tick(0); assert(uart.count == 6);
  const uint8_t sysEx[] = {0x90,0xf0,60,100,0xf7,62,99}; uart.feed(sysEx);
  e.tick(0); assert(uart.count == 6);
  blank();
  e.track = 6;
  e.project.tracks[6].sampleSlot = 3;
  e.project.tracks[6].volume = 64;
  e.start(0); e.tick(0); e.recording = true;
  e.noteOn(64,100,90000);
  assert(e.project.steps[0][6][1].velocity == 100);
  assert(callbacks == 1 && lastSlot == 3 && lastVelocity == 50);
  e.tick(150000); assert(callbacks == 1); // Recently monitored hit does not flam.
  e.tick(2150000); assert(callbacks == 2);
  e.setMute(6,true); e.tick(4150000); assert(callbacks == 2);
  e.selectPattern(9); assert(e.pattern == 0);
  e.selectPattern(2); assert(e.pattern == 2);
  e.stop(); assert(!e.playing && !e.recording && uart.count == 144);
  blank();
  e.track = 0; e.noteOn(36,100,0);
  hasNote(0,0xb9,99,0x1d); hasNote(3,0xb9,98,36); hasNote(6,0xb9,6,18);
  e.setMute(0,true); hasNote(12,0x89,36,0);
  e.noteOn(36,100,0); assert(uart.count == 15);
  puts("PASS: factory variations and validation, transport and gate rollover, swing, stall catchup, chord ownership, input MIDI running status, sample routes, live quantization, mute and 16-channel panic.");
}
