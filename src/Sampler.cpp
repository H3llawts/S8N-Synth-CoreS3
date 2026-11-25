#include "Sampler.h"

#include <M5Unified.h>
#include <SD.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif
#include <utility>

namespace {
constexpr uint16_t Block = 320;

struct SlotPaths {
    char wav[20], temp[20], backup[20];
    explicit SlotPaths(uint8_t slot) {
        snprintf(wav, sizeof(wav), "/S8N/S%02u.WAV", unsigned(slot + 1));
        snprintf(temp, sizeof(temp), "/S8N/S%02u.TMP", unsigned(slot + 1));
        snprintf(backup, sizeof(backup), "/S8N/S%02u.BAK", unsigned(slot + 1));
    }
};

uint16_t u16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t u32(const uint8_t* p) {
    return uint32_t(u16(p)) | (uint32_t(u16(p + 2)) << 16);
}
void put32(uint8_t* p, uint32_t n) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i));
}

// Locate the first data chunk, allowing padded metadata and extended fmt chunks.
const char* findAudio(File& file, uint32_t& frames) {
    uint8_t h[16];
    if (file.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return "Invalid WAV";
    const uint64_t end = uint64_t(u32(h + 4)) + 8;
    if (end < 12 || end > file.size()) return "Truncated WAV";
    bool pcm = false;
    uint32_t samplePos = 0, bytes = 0;
    uint64_t next = 12;
    while (next + 8 <= end) {
        if (!file.seek(static_cast<uint32_t>(next)) || file.read(h, 8) != 8)
            return "WAV read error";
        uint32_t size = u32(h + 4);
        uint32_t start = file.position();
        next += 8ULL + size + (size & 1U);
        if (next > end) return "Truncated chunk";
        if (!memcmp(h, "fmt ", 4)) {
            if (size < 16 || file.read(h, 16) != 16 || u16(h) != 1 ||
                u16(h + 2) != 1 || u32(h + 4) != Sampler::Rate ||
                u16(h + 12) != 2 || u16(h + 14) != 16)
                return "Use 16k mono PCM16";
            pcm = true;
        } else if (!memcmp(h, "data", 4) && !samplePos) {
            bytes = size;
            samplePos = start;
        }
        if (pcm && samplePos) break;
    }
    if (!pcm || !samplePos || !bytes || (bytes & 1)) return "Invalid PCM data";
    if (bytes / 2 > Sampler::MaxFrames) return "Clip exceeds 4s";
    if (!file.seek(samplePos)) return "WAV seek error";
    frames = bytes / 2;
    return nullptr;
}

bool writeHeader(File& file, uint32_t count) {
    uint8_t h[44] = {};
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36U + count * 2U);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    h[20] = 1;
    h[22] = 1;
    put32(h + 24, Sampler::Rate);
    put32(h + 28, 2 * Sampler::Rate);
    h[32] = 2;
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    put32(h + 40, count * 2U);

    return file.write(h, sizeof(h)) == sizeof(h);
}

bool restoreInterruptedSave(const SlotPaths& paths) {
    return SD.exists(paths.wav) || !SD.exists(paths.backup) ||
           SD.rename(paths.backup, paths.wav);
}
}

Sampler::Audio Sampler::allocate(uint32_t frames) {
    const size_t bytes = sizeof(int16_t) * frames;
#if defined(ESP_PLATFORM)
    // M5Unified copies PCM to/from its internal I2S DMA buffers.
    // Keeping long clips in PSRAM leaves internal RAM for those buffers.
    void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr && frames <= 8192) ptr = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    void* ptr = std::malloc(bytes);
#endif
    return Audio(static_cast<int16_t*>(ptr));
}

bool Sampler::fail(const char* message) {
    message_ = message;
    return false;
}

void Sampler::begin(bool sdReady) {
    sdReady_ = sdReady && (SD.exists("/S8N") || SD.mkdir("/S8N"));
    M5.Mic.end();
    const char* warning = nullptr;
    for (uint8_t slot = 0; slot < Slots; ++slot) {
        SlotPaths paths(slot);
        if (sdReady_ && (SD.exists(paths.wav) || SD.exists(paths.backup))) {
            if (load(slot)) continue;
            warning = message_;
        }
        factorySound(slot);
        if (!sounds_[slot]) warning = "Low sample memory";
    }
    if (!M5.Speaker.begin()) warning = "Speaker unavailable";
    message_ = warning ? warning : (sdReady_ ? "Pads ready" : "Pads ready / RAM only");
}

void Sampler::factorySound(uint8_t slot) {
    static constexpr uint16_t length[] = {4000, 3200, 1600, 4800};
    Audio sound = allocate(length[slot]);
    if (!sound) return;
    uint32_t rng = 0x58314bU + slot;
    float phase = 0, previousNoise = 0;
    constexpr float tau = 6.28318530718f;
    for (uint16_t i = 0; i < length[slot]; ++i) {
        float t = float(i) / Rate;
        rng = 1664525U * rng + 1013904223U;
        float noise = (float(rng >> 8) / 8388608.0f) - 1.0f;
        float sample;
        switch (slot) {
            case 0: // A short pitch dive gives the kick its initial knock.
                phase += (56.0f + 128.0f * expf(-t * 55)) * tau / Rate;
                sample = sinf(phase) * expf(-t * 23);
                break;
            case 1:
                sample = 0.68f * noise * expf(-t * 34) +
                         0.30f * sinf(tau * 180 * t) * expf(-t * 44);
                break;
            case 2:
                sample = 0.50f * (noise - previousNoise) * expf(-t * 80);
                break;
            default:
                phase += (99.0f + 78.0f * expf(-t * 34)) * tau / Rate;
                sample = sinf(phase) * expf(-t * 18);
                break;
        }
        previousNoise = noise;
        sound[i] = int16_t(28000.0f * sample);
    }
    sounds_[slot] = std::move(sound);
    lengths_[slot] = length[slot];
}

bool Sampler::trigger(uint8_t slot, uint8_t velocity) {
    if (suspended_ || capturing() || !frames(slot) || !velocity) return false;
    uint8_t volume = uint8_t(std::min<int>(velocity, 127) * 255 / 127);
    M5.Speaker.setChannelVolume(slot, volume);
    return M5.Speaker.playRaw(sounds_[slot].get(), lengths_[slot],
                                      Rate, false, 1, slot, true);
}

void Sampler::stop() {
    if (!capturing() && !suspended_) {
        for (uint8_t slot = 0; slot < Slots; ++slot) M5.Speaker.stop(slot);
    }
}

void Sampler::suspendAudio() {
    if (suspended_) return;
    if (capturing()) {
        stopRequested_ = true;
        releaseMic();
        scratch_.reset();
    }
    M5.Speaker.end();
    suspended_ = true;
}

void Sampler::resumeAudio() {
    if (!suspended_) return;
    suspended_ = false;
    if (!M5.Speaker.begin()) message_ = "Speaker unavailable";
}

bool Sampler::startCapture(uint8_t slot) {
    if (saving()) return fail("Wait for pad save");
    if (slot >= Slots || suspended_ || capturing()) return fail("Audio busy");
    scratch_ = allocate(MaxFrames);
    if (!scratch_) return fail("Low sample memory");
    M5.Speaker.end(); // The AW88298 and ES7210 share the I2S1 clock pins on CoreS3.
    recordingSlot_ = slot;
    completed_ = 0;
    stopRequested_ = false;
    captureDone_ = captureError_ = false;
    lastProgress_ = 0;
    lastProgressMs_ = millis();
    auto& mic = M5.Mic;
    mic.setSampleRate(Rate);
    mic.setBufferReleaseCallback(this, released);
    if (!mic.begin() || !mic.record(scratch_.get(), Block)) {
        releaseMic();
        scratch_.reset();
        M5.Speaker.begin();
        return fail("Mic unavailable");
    }
    message_ = "Recording pad";
    return true;
}

// Runs in the M5Unified mic task. Chain the next segment here so SD/UI work
// on the Arduino task cannot introduce gaps into the recorded one-shot.
void Sampler::released(void* context, void* data, size_t length) {
    auto* self = static_cast<Sampler*>(context);
    int16_t* base = self->scratch_.get();
    uint32_t end = uint32_t(static_cast<int16_t*>(data) - base + length);
    self->completed_.store(end);
    if (end < MaxFrames && !self->stopRequested_.load()) {
        const size_t next = std::min<size_t>(Block, MaxFrames - end);
        if (M5.Mic.record(base + end, next)) return;
        self->captureError_.store(true);
    }
    self->captureDone_.store(true);
}

void Sampler::stopCapture() {
    if (capturing()) stopRequested_ = true;
}

void Sampler::releaseMic() {
    M5.Mic.end();
    M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
    recordingSlot_ = Slots;
}

void Sampler::service() {
    if (saving()) serviceSave();
    if (!capturing()) return;
    uint32_t done = completed_.load();
    if (done != lastProgress_) {
        lastProgress_ = done;
        lastProgressMs_ = millis();
    }
    bool finished = captureDone_.load();
    bool timeout = !finished && millis() - lastProgressMs_ > 1500;
    if (!finished && !timeout) return;

    const uint8_t slot = recordingSlot_;
    stopRequested_ = true;
    releaseMic(); // Fence the final callback before reading or moving scratch_.
    done = completed_.load();
    if (timeout || captureError_.load() || !done) {
        scratch_.reset();
        message_ = "Mic error / pad kept";
    } else {
        sounds_[slot] = std::move(scratch_);
        lengths_[slot] = done;
        if (sdReady_) {
            if (!save(slot)) message_ = "Pad in RAM / SD error";
        } else message_ = "Captured / RAM only";
    }
    if (!M5.Speaker.begin()) message_ = "Speaker unavailable";
}

bool Sampler::load(uint8_t slot) {
    if (saving()) return fail("Wait for pad save");
    if (capturing() || suspended_) return fail("Audio busy");
    if (slot >= Slots || !sdReady_) return fail("SD unavailable");
    SlotPaths paths(slot);
    if (!restoreInterruptedSave(paths)) return fail("SD recovery error");
    File file = SD.open(paths.wav, FILE_READ);
    if (!file) return fail("Slot WAV missing");
    uint32_t count = 0;
    const char* error = findAudio(file, count);
    if (error) return fail(error);
    Audio staged = allocate(count);
    if (!staged) return fail("Low sample memory");
    const size_t bytes = count * sizeof(int16_t);
    if (file.read(reinterpret_cast<uint8_t*>(staged.get()), bytes) != bytes)
        return fail("WAV read error");
    // playRaw borrows its buffer. Join the driver before freeing the old clip.
    if (sounds_[slot] && M5.Speaker.isRunning()) M5.Speaker.end();
    sounds_[slot] = std::move(staged);
    lengths_[slot] = count;
    message_ = "Pad loaded";
    return true;
}

bool Sampler::save(uint8_t slot) {
    if (saving()) return fail("Wait for pad save");
    if (capturing() || suspended_) return fail("Audio busy");
    if (!frames(slot) || !sdReady_) return fail("SD unavailable");
    SlotPaths paths(slot);
    if (!restoreInterruptedSave(paths)) return fail("SD recovery error");
    if (SD.exists(paths.temp) && !SD.remove(paths.temp)) return fail("SD temp busy");
    saveFile_ = SD.open(paths.temp, FILE_WRITE);
    if (!saveFile_ || !writeHeader(saveFile_, lengths_[slot])) {
        saveFile_.close();
        return fail("WAV write error");
    }
    saveOffset_ = 0;
    saveSlot_ = slot;
    saveState_ = SaveState::Write;
    message_ = "Saving pad";
    return true;
}

void Sampler::finishSave(const char* message) {
    saveFile_.close();
    saveState_ = SaveState::Idle;
    message_ = message;
}

// Keep transport, touch and MIDI serviceable between SD transactions. The
// published pad remains available for playback throughout the staged save.
void Sampler::serviceSave() {
    constexpr uint32_t chunkBytes = 2048;
    const uint32_t bytes = lengths_[saveSlot_] * sizeof(int16_t);
    const uint32_t n = std::min(chunkBytes, bytes - saveOffset_);
    const uint8_t* pcm = reinterpret_cast<const uint8_t*>(sounds_[saveSlot_].get());
    SlotPaths paths(saveSlot_);

    if (saveState_ == SaveState::Write) {
        if (saveFile_.write(pcm + saveOffset_, n) != n) {
            finishSave("SD write error / pad in RAM"); return;
        }
        saveOffset_ += n;
        if (saveOffset_ == bytes) {
            saveFile_.flush(); saveFile_.close();
            saveFile_ = SD.open(paths.temp, FILE_READ);
            uint32_t stored = 0;
            if (!saveFile_ || findAudio(saveFile_, stored) || stored != lengths_[saveSlot_]) {
                finishSave("WAV header verify failed"); return;
            }
            saveOffset_ = 0;
            saveState_ = SaveState::Verify;
        }
        return;
    }
    if (saveState_ == SaveState::Verify) {
        uint8_t block[chunkBytes];
        if (saveFile_.read(block,n) != n || memcmp(block,pcm + saveOffset_,n)) {
            finishSave("WAV data verify failed"); return;
        }
        saveOffset_ += n;
        if (saveOffset_ == bytes) {
            saveFile_.close();
            saveState_ = SaveState::Commit;
        }
        return;
    }

    const bool hadFile = SD.exists(paths.wav);
    if (hadFile) {
        if (SD.exists(paths.backup) && !SD.remove(paths.backup)) {
            finishSave("SD backup busy"); return;
        }
        if (!SD.rename(paths.wav, paths.backup)) {
            finishSave("SD backup failed"); return;
        }
    }
    if (!SD.rename(paths.temp, paths.wav)) {
        if (hadFile) SD.rename(paths.backup, paths.wav);
        finishSave("SD rename failed"); return;
    }
    if (hadFile) SD.remove(paths.backup);
    finishSave("Pad saved");
}

uint8_t Sampler::peak(uint8_t slot, uint8_t column, uint8_t columns) const {
    if (slot >= Slots || !columns || column >= columns) return 0;
    bool live = recordingSlot_ == slot;
    const int16_t* samples = live ? scratch_.get() : sounds_[slot].get();
    const uint32_t count = live ? capturedFrames() : lengths_[slot];
    if (!samples || !count) return 0;
    const size_t first = size_t(column) * count / columns;
    const size_t last = std::max(first + 1, size_t(column + 1) * count / columns);
    int maximum = 0;
    for (size_t i = first; i < last; ++i) maximum = std::max(maximum, std::abs(int(samples[i])));
    return uint8_t(std::min(255, maximum >> 7));
}

void Sampler::waveform(uint8_t slot, uint8_t out[64]) const {
    for (uint8_t i = 0; i < 64; ++i) out[i] = peak(slot, i, 64) >> 1;
}
