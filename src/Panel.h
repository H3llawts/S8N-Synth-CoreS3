#pragma once
#include "Engine.h"

enum class Page : uint8_t { Grid, Sound, Pads, Sensor, Project };
enum class Cmd : uint8_t {
    None, Page, Transport, Panic, Arm, Pattern, Lane, Cell, Mute, Tempo,
    SoundValue, Key, Pad, Capture, SavePad, LoadPad, ProjectSlot, SaveProject,
    LoadProject, RecordTake, PlayTake, SensorEnable, Calibrate
};

struct Action {
    Cmd cmd = Cmd::None;
    int a = 0, b = 0;
};

struct PanelState {
    Page page = Page::Grid;
    const Project* project = nullptr;
    uint8_t pattern = 0, track = 0, step = 0, playhead = 0, slot = 0, projectSlot = 0;
    uint8_t octave = 4, sensorTrack = 4;
    bool playing = false, armed = false, sdReady = false, cardKB = false;
    bool capturing = false, takeRecording = false, takePlaying = false;
    bool sensorEnabled = false, imuReady = false, proximityReady = false;
    uint8_t tilt = 0, proximity = 0, wave[64]{};
    uint16_t padMs = 0, captureMs = 0;
    uint32_t takeSeconds = 0;
    int8_t heldKey = -1;
    const char* notice = nullptr;
};

class Panel {
public:
    void begin();
    void draw(const PanelState& state);
    // Tap commands fire once on contact. Key is a semitone from the base of
    // the touch octave. Release the previous key on a lift or a slide away.
    static Action hit(const PanelState& state, int x, int y);
};
