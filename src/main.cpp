#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <driver/gpio.h>
#include <algorithm>
#include <cstring>
#include "Panel.h"
#include "Gestures.h"
#include "Sampler.h"
#include "Storage.h"
#include "Takes.h"

namespace {
HardwareSerial unit(1);
Engine engine;
Panel panel;
PanelState ui;
Gestures gestures;
Sampler sampler;
Storage storage;
Takes takes;
char message[56]{};
uint32_t messageAt = 0, renderedAt = 0, kbAt = 0;
int touchPitch = -1;
uint8_t gestureLane = 4;
bool dirty = true, touchDown = false, sliding = false, kbBus = false;

int clamp(int n, int lo, int hi) { return std::max(lo,std::min(n,hi)); }
int wrap(int n, int count) { return (n+count)%count; }
Step& cell() { return engine.project.steps[engine.pattern][engine.track][ui.step]; }
void say(const char* text) {
    snprintf(message,sizeof(message),"%s",text);
    messageAt = millis(); dirty = true;
}
void playPad(uint8_t slot, uint8_t velocity) { sampler.trigger(slot,velocity); }
bool takeBusy() { return takes.recording() || takes.playing(); }
void liftKey() {
    if (touchPitch >= 0) engine.noteOff(touchPitch);
    touchPitch = -1; ui.heldKey = -1;
}

void lane(int n) {
    if (engine.track == n) return;
    liftKey(); engine.track = uint8_t(n);
    if (n >= 3 && n <= 5 && n != gestureLane) {
        const Track& old = engine.project.tracks[gestureLane];
        engine.tone(gestureLane,old.cutoff,old.reverb);
        gestureLane = n;
    }
}

void modulation() {
    const auto& track = engine.project.tracks[gestureLane];
    ui.tilt = clamp(int(track.cutoff)+gestures.tiltOffset(),0,127);
    ui.proximity = clamp(int(track.reverb)+gestures.proximityOffset(),0,127);
    engine.tone(gestureLane,ui.tilt,ui.proximity);
}

void editSound(int field, int by) {
    auto& track = engine.project.tracks[engine.track];
    if (field == 0) {
        if (engine.track < 3) cell().note = clamp(int(cell().note)+by,0,127);
        else if (engine.track < 6) track.program = wrap(track.program+by,128);
        else track.sampleSlot = wrap(int(track.sampleSlot)+by,4);
    } else if (field == 1) track.volume = clamp(int(track.volume)+by,0,127);
    else if (field == 2) track.cutoff = clamp(int(track.cutoff)+by,0,127);
    else if (field == 3) track.reverb = clamp(int(track.reverb)+by,0,127);
    engine.applyTrack(engine.track);
    modulation();
}

int pitch(int offset) {
    int base = engine.track < 3 ? Engine::defaultNote(engine.track) : 12*(ui.octave+1);
    return clamp(base+offset,0,127);
}

void transport(bool panic) {
    if (panic || engine.playing) {
        liftKey(); engine.stop(); sampler.stop();
        if (panic && takes.playing()) takes.stop();
        if (panic) say("Panic / voices released");
    } else engine.start(micros());
}

void padFile(Cmd action) {
    if (takeBusy()) { say("Finish the mic take first"); return; }
    if (action == Cmd::Capture) {
        if (sampler.capturing()) { sampler.stopCapture(); return; }
        sampler.startCapture(ui.slot);
    } else if (action == Cmd::SavePad) sampler.save(ui.slot);
    else sampler.load(ui.slot);
    say(sampler.status());
}

void projectFile(Cmd action) {
    if (!ui.sdReady) { say("Insert FAT32 microSD and restart"); return; }
    if (takeBusy() || sampler.capturing() || sampler.saving()) { say("Finish audio file operation first"); return; }
    if (action == Cmd::SaveProject) {
        say(storage.save(ui.projectSlot,engine.project) ? "Project saved" : "Project save failed");
    } else if (storage.load(ui.projectSlot,engine.project)) {
        liftKey(); sampler.stop(); engine.applyProject(); engine.selectPattern(0); modulation();
        say("Project loaded / pattern A");
    } else say("Empty or unreadable slot");
}

void takeAction(Cmd action) {
    if (takeBusy()) takes.stop();
    else if (sampler.capturing() || sampler.saving()) { say("Finish the pad first"); return; }
    else if (action == Cmd::RecordTake) takes.record();
    else takes.playLatest();
    say(takes.status());
}

void apply(Action a) {
    if (a.cmd == Cmd::None) return;
    dirty = true;
    switch (a.cmd) {
    case Cmd::Page: liftKey(); ui.page = Page(a.a); message[0] = 0; break;
    case Cmd::Transport: transport(false); break;
    case Cmd::Panic: transport(true); break;
    case Cmd::Arm: engine.recording = !engine.recording; break;
    case Cmd::Pattern: engine.selectPattern(a.a); break;
    case Cmd::Lane: lane(a.a); break;
    case Cmd::Cell:
        lane(a.a); ui.step = a.b;
        cell().velocity = cell().velocity ? 0 : 100;
        break;
    case Cmd::Mute: engine.setMute(engine.track,!engine.project.tracks[engine.track].muted); break;
    case Cmd::Tempo: engine.project.bpm = clamp(engine.project.bpm+a.a,40,240); break;
    case Cmd::SoundValue: editSound(a.a,a.b); break;
    case Cmd::Key:
        liftKey(); ui.heldKey = a.a; touchPitch = pitch(a.a);
        engine.noteOn(touchPitch,100,micros()); break;
    case Cmd::Pad:
        if (sampler.capturing()) { say("Pad recording in progress"); break; }
        ui.slot = a.a;
        if (!sampler.trigger(ui.slot,110)) say(takeBusy() ? "Mic take owns local audio" : "Pad unavailable");
        break;
    case Cmd::Capture: case Cmd::SavePad: case Cmd::LoadPad: padFile(a.cmd); break;
    case Cmd::ProjectSlot: ui.projectSlot = a.a; break;
    case Cmd::SaveProject: case Cmd::LoadProject: projectFile(a.cmd); break;
    case Cmd::RecordTake: case Cmd::PlayTake: takeAction(a.cmd); break;
    case Cmd::SensorEnable: gestures.enable(!gestures.enabled()); modulation(); break;
    case Cmd::Calibrate: say(gestures.calibrate() ? "Rest position captured" : "Waiting for sensor readings"); modulation(); break;
    default: break;
    }
}

// CardKB sends one translated byte per press. Timed gates and the touch
// keyboard have independent note ownership in Engine. The SD project stores
// the base patch settings, while sensor modulation remains a live control.
void key(uint8_t raw) {
    bool upper = raw >= 'A' && raw <= 'Z';
    int c = upper ? raw-'A'+'a' : raw;
    if (c == 0x1b) { apply({Cmd::Panic}); return; }
    if (c == ' ') { apply({Cmd::Transport}); return; }
    if (c == 9) { apply({Cmd::Page,(int(ui.page)+1)%5}); return; }
    const int horizontal = (c==0xb7)-(c==0xb4);
    const int vertical = (c==0xb6)-(c==0xb5);
    dirty = true;
    if (ui.page == Page::Grid || ui.page == Page::Sound) {
        if (vertical) { apply({Cmd::Lane,wrap(engine.track+vertical,8)}); return; }
        if (horizontal) {
            if (ui.page == Page::Grid) ui.step = wrap(ui.step+horizontal,16);
            else editSound(0,horizontal);
            return;
        }
        if (c == 'z' || c == 'x') {
            liftKey(); ui.octave = clamp(int(ui.octave)+(c=='x'?1:-1),0,8);
            char msg[24]; snprintf(msg,sizeof(msg),"Keyboard octave %u",ui.octave); say(msg); return;
        }
        if (c == 'r') { apply({Cmd::Arm}); return; }
        if (c == 'm') { apply({Cmd::Mute}); return; }
        constexpr char keys[] = "awsedftgyhujk";
        const char* k = strchr(keys,c);
        if (k && *k) {
            int note = pitch(k-keys);
            if (upper && ui.page==Page::Grid && !engine.playing) {
                cell().note = note; if (!cell().velocity) cell().velocity = 100;
            }
            const uint32_t gate = (15000000UL/engine.project.bpm)*cell().gate/100;
            engine.tap(note,100,micros(),std::max<uint32_t>(30000,gate));
            return;
        }
        if (c >= '1' && c <= '4') { apply({Cmd::Pattern,c-'1'}); return; }
        if (c == '-' || c == '+' || c == '=') { apply({Cmd::Tempo,c=='-'?-1:1}); return; }
        if (ui.page == Page::Grid) {
            if (c == 13 || c == 10) apply({Cmd::Cell,engine.track,ui.step});
            else if (c == '[' || c == ']') cell().note = clamp(int(cell().note)+(c==']'?1:-1),0,127);
            else if (c == '5' || c == '6') cell().velocity = clamp((cell().velocity?cell().velocity:96)+(c=='6'?8:-8),1,127);
            else if (c == '7' || c == '8') cell().gate = clamp(int(cell().gate)+(c=='8'?5:-5),1,100);
            else if (c == '9' || c == '0') {
                engine.project.swing = clamp(int(engine.project.swing)+(c=='0'?1:-1),50,75);
                char msg[24]; snprintf(msg,sizeof(msg),"Swing %u%%",engine.project.swing); say(msg);
            }
        }
    } else if (ui.page == Page::Pads) {
        if (c>='1' && c<='4') apply({Cmd::Pad,c-'1'});
        else if (horizontal) apply({Cmd::Pad,wrap(ui.slot+horizontal,4)});
        else if (c==13 || c==10) apply({Cmd::Pad,ui.slot});
        else if (c=='r') apply({Cmd::Capture});
        else if (c=='s') apply({Cmd::SavePad});
        else if (c=='l') apply({Cmd::LoadPad});
    } else if (ui.page == Page::Sensor) {
        if (c=='e') apply({Cmd::SensorEnable});
        if (c=='c') apply({Cmd::Calibrate});
    } else if (ui.page == Page::Project) {
        if (horizontal) apply({Cmd::ProjectSlot,wrap(ui.projectSlot+horizontal,8)});
        else if (c=='s') apply({Cmd::SaveProject});
        else if (c=='l') apply({Cmd::LoadProject});
        else if (c=='r') apply({Cmd::RecordTake});
        else if (c=='p') apply({Cmd::PlayTake});
    }
}

void pollCardKB(uint32_t now) {
    if (now-kbAt < (ui.cardKB ? 10U : 800U)) return;
    kbAt = now;
    if (!kbBus) kbBus = M5.Ex_I2C.begin();
    uint8_t value = 0;
    const bool ok = kbBus && m5gfx::i2c::transactionRead(M5.Ex_I2C.getPort(),0x5f,&value,1,100000).has_value();
    if (ok != ui.cardKB) dirty = true;
    ui.cardKB = ok;
    if (ok && value) key(value);
}

void pollTouch() {
    const bool down = M5.Touch.getCount() && M5.Touch.getDetail().isPressed();
    if (down) {
        const auto& pt = M5.Touch.getDetail();
        Action action = Panel::hit(ui,pt.x,pt.y);
        if (!touchDown) {
            sliding = action.cmd == Cmd::Key; apply(action);
        } else if (sliding && action.cmd == Cmd::Key && ui.heldKey != action.a) apply(action);
        else if (sliding && action.cmd != Cmd::Key && touchPitch >= 0) { liftKey(); dirty=true; }
    } else if (touchDown) { liftKey(); sliding=false; dirty=true; }
    touchDown = down;
}

void syncState() {
    ui.track=engine.track; ui.pattern=engine.pattern; ui.playhead=engine.playhead;
    ui.playing=engine.playing; ui.armed=engine.recording;
    ui.capturing=sampler.capturing(); ui.takeRecording=takes.recording(); ui.takePlaying=takes.playing();
    ui.sensorEnabled=gestures.enabled(); ui.imuReady=gestures.tiltAvailable();
    ui.proximityReady=gestures.proximityAvailable(); ui.sensorTrack=gestureLane;
}
}

void setup() {
    pinMode(4,OUTPUT); digitalWrite(4,HIGH); // Deselect SD while the shared LCD bus starts.
    auto cfg = M5.config();
    cfg.internal_imu = true; cfg.internal_mic = true; cfg.internal_spk = true; cfg.output_power = true;
    M5.begin(cfg);
    if (M5.Display.width() < M5.Display.height()) M5.Display.setRotation((M5.Display.getRotation()+1)%4);
    M5.Power.setExtOutput(true);
    panel.begin();
    kbBus = M5.Ex_I2C.begin();
    unit.begin(31250,SERIAL_8N1,18,17); gpio_set_pull_mode(GPIO_NUM_18,GPIO_PULLUP_ONLY);
    SPI.begin(36,35,37,4);
    ui.sdReady = SD.begin(4,SPI,25000000) && SD.cardType()!=CARD_NONE;
    sampler.begin(ui.sdReady); takes.begin(sampler,ui.sdReady);
    M5.Speaker.setVolume(140);
    gestures.begin();
    delay(300);
    const uint8_t reset[] = {0xf0,0x7e,0x7f,9,1,0xf7};
    unit.write(reset,sizeof(reset)); unit.flush(); delay(100);
    engine.begin(unit,playPad);
    engine.track=gestureLane; ui.project=&engine.project; kbAt=millis()-800;
}

void loop() {
    engine.tick(micros());
    bool capturing = sampler.capturing(), saving = sampler.saving(), taking = takeBusy();
    sampler.service(); takes.service();
    if ((capturing && !sampler.capturing()) || (saving && !sampler.saving())) say(sampler.status());
    if (taking && !takeBusy()) say(takes.status());
    M5.update();
    uint32_t now = millis();
    gestures.update(now); modulation(); syncState();
    pollCardKB(now); syncState(); pollTouch();
    engine.tick(micros());
    const bool active = engine.playing || sampler.capturing() || takeBusy() || ui.page==Page::Sensor;
    if (now-renderedAt >= ((dirty || active) ? 40U : 200U)) {
        syncState();
        if (ui.page==Page::Pads) {
            ui.captureMs=sampler.capturedFrames()*1000UL/Sampler::Rate;
            ui.padMs=sampler.frames(ui.slot)*1000UL/Sampler::Rate;
            sampler.waveform(ui.slot,ui.wave);
        }
        ui.takeSeconds=takes.seconds();
        ui.notice = message[0] && now-messageAt<2000 ? message : nullptr;
        panel.draw(ui); renderedAt=now; dirty=false;
    }
    delay(1);
}
