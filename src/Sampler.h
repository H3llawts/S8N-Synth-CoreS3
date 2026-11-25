#pragma once

#include <FS.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cstdlib>

class Sampler {
public:
    static constexpr uint8_t Slots = 4;
    static constexpr uint16_t Rate = 16000;
    static constexpr uint32_t MaxFrames = 4U * Rate;

    void begin(bool sdReady);
    void service();
    bool startCapture(uint8_t slot);
    void stopCapture();
    bool capturing() const { return recordingSlot_ < Slots; }
    uint32_t capturedFrames() const { return completed_.load(); }
    bool trigger(uint8_t slot, uint8_t velocity = 100);
    void stop();
    void suspendAudio();
    void resumeAudio();

    bool load(uint8_t slot);
    // Starts a staged WAV write. Call service() until saving() clears.
    bool save(uint8_t slot);
    bool saving() const { return saveState_ != SaveState::Idle; }
    uint32_t frames(uint8_t slot) const { return slot < Slots ? lengths_[slot] : 0; }
    uint8_t peak(uint8_t slot, uint8_t column, uint8_t columns) const;
    void waveform(uint8_t slot, uint8_t out[64]) const;
    const char* status() const { return message_; }

private:
    struct AudioDeleter {
        void operator()(int16_t* ptr) const { std::free(ptr); }
    };
    using Audio = std::unique_ptr<int16_t[], AudioDeleter>;
    static Audio allocate(uint32_t frames);
    Audio sounds_[Slots];
    uint32_t lengths_[Slots] = {};
    Audio scratch_;
    uint8_t recordingSlot_ = Slots;
    bool sdReady_ = false;
    bool suspended_ = false;
    const char* message_ = "Ready";
    std::atomic<uint32_t> completed_{0};
    std::atomic<bool> stopRequested_{false}, captureDone_{false}, captureError_{false};
    uint32_t lastProgress_ = 0;
    uint32_t lastProgressMs_ = 0;

    enum class SaveState { Idle, Write, Verify, Commit };
    SaveState saveState_ = SaveState::Idle;
    uint8_t saveSlot_ = 0;
    uint32_t saveOffset_ = 0;
    File saveFile_;

    void serviceSave();
    void finishSave(const char* message);
    void factorySound(uint8_t slot);
    bool fail(const char* message);
    void releaseMic();
    static void released(void* context, void* data, size_t length);
};
