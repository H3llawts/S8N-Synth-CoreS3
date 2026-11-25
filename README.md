# S8N Synth for CoreS3

A touch groovebox for the M5Stack CoreS3 IoT kit (K128). Plug CardKB into red Port A and **one Unit Synth or Unit MIDI into blue Port C** using stock Grove cables. Both units use the same firmware. Insert a FAT32 microSD before boot to save your work.

[Download the merged CoreS3 firmware](firmware/S8N-Synth-CoreS3-merged.bin?raw=1)

## First groove

Tap Play on the Grid. The factory groove starts at 110 BPM. Tap a cell to add or remove a hit. Tap a lane name to select it without changing any steps. A–D switch between four patterns. Sound opens the selected lane's controls and a touch keyboard. Hold a key to sustain it or slide across keys. Arm overdub and play to record into the nearest sixteenth.

Unit Synth plays through its speaker. On Unit MIDI use the AUDIO jack for headphones or powered speakers. FROM CORE / Separate also sends notes to MIDI OUT. BYPASS lets an external MIDI keyboard play the selected lane through MIDI IN. The sequencer uses its own clock.

## Play with the sensors

Select Bass, Lead or Chord, open Sensor, and enable modulation with the CoreS3 at a comfortable angle and your hand clear of the front sensor. Tilt sideways to change filter cutoff. Bring a hand toward the sensor below the display to change reverb. Calibrate sets a new resting position. Switching modulation off restores the stored sound. Sensor motion follows the most recently selected melodic lane and leaves your saved patch settings intact.

## Sample and record

On Pads, tap one of the four slots to select and hear it. Capture records up to four seconds from the onboard mics. Tap again to finish early. With a card installed, captured pads save automatically. Wait for the save to finish before powering down. The four slots are shared between projects. To import sounds, use /S8N/S01.WAV through S04.WAV, mono 16-bit PCM at 16 kHz, up to 64,000 frames. Select a sample lane on Grid, assign its slot on Sound, and add steps.

On Project, Record Take streams the room microphone to /S8N/TAKES. Finish the take before removing power. Play Latest auditions it on the CoreS3 speaker. The local pads pause while the mic runs. The external synth keeps playing. To capture a Unit MIDI performance acoustically, use a speaker on its AUDIO jack. Each minute of audio uses about 1.92 MB.

Project Save stores all four patterns and track settings in one of eight slots. Load recalls the selected slot and stops transport. Cardputer v0.2 project files and short pad WAVs use the same formats. Without a card, the sequencer and sampler work in RAM.

## CardKB

| Keys | Action |
|---|---|
| Space / Esc / Tab | Transport / panic / next page |
| A W S E D F T G Y H U J K | Chromatic notes on Grid or Sound. CardKB sends presses, so each note gets a timed release. |
| Z / X | Octave down / up |
| Up / Down | Select lane on Grid or Sound |
| Left / Right | Move step on Grid, change patch, drum note or sample assignment on Sound |
| 1–4 / R / M | Select pattern / arm overdub / mute lane on Grid or Sound |
| Enter / uppercase piano key | Toggle selected Grid step / write pitch while stopped |
| [ and ] / 5 and 6 / 7 and 8 | Step pitch / velocity / gate on Grid |
| 9 and 0 / minus and equals | Swing on Grid / tempo on Grid or Sound |
| 1–4 / R / S / L on Pads | Audition slot / capture / save / reload |
| E / C on Sensor | Enable modulation / calibrate |
| Left or Right / S / L on Project | Select slot / save / load |
| R / P on Project | Record or finish a mic take / play latest or stop |

## Install or build

Installing replaces the CoreS3 firmware. Connect the CoreS3 by USB, open the [Espressif serial flasher](https://espressif.github.io/esptool-js/) in desktop Chrome or Edge, and select the ESP32-S3 port. Program the **CoreS3 merged binary at address 0x0** with image defaults, wait for verification, then reset. For manual download mode, hold the CoreS3 reset button until its green LED lights, then release. This image targets 16 MB flash and quad PSRAM.

From source, run `pio run -e cores3` with PlatformIO. Use `pio run -e cores3 -t upload` to install over USB. The build-generated firmware.bin alone is an application image at 0x10000. The downloadable merged file includes the bootloader and partitions.

The host tests in tests/ exercise MIDI, storage, sensor recovery, sampling and WAV takes. Physical touch alignment, sensor sensitivity, speaker levels and SD write timing require a bench check on the kit. Start by listening to the factory groove, recording a clap, then recording and replaying a mic take.

Based on [S8N Synth for Cardputer](https://github.com/H3llawts/Cardputer-S8N-Synth/tree/groovebox-v0.2). Hardware references: [CoreS3](https://docs.m5stack.com/en/core/CoreS3), [CardKB](https://docs.m5stack.com/en/unit/Unit_CardKB_v1.1), [Unit Synth](https://docs.m5stack.com/en/unit/Unit_Synth), [Unit MIDI](https://docs.m5stack.com/en/unit/Unit_MIDI).
