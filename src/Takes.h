#pragma once
#include <FS.h>
#include <atomic>

class Sampler;

// Streams mono PCM in bounded RAM. Only the foreground accesses the SD card.
class Takes {
public:
    void begin(Sampler& sampler, bool sdReady);
    bool record();
    bool playLatest();
    void stop();
    void service();
    bool recording() const { return mode_ == Record; }
    bool playing() const { return mode_ == Play; }
    uint32_t seconds() const { return transferred_ / 32000; }
    const char* status() const { return status_; }
private:
    enum Mode { Idle, Record, Play } mode_ = Idle;
    static constexpr uint32_t Rate = 16000, Block = 512, Blocks = 8;
    int16_t audio_[Blocks][Block]{};
    std::atomic<uint32_t> filled_{0}, drained_{0};
    std::atomic<bool> accepting_{false}, overrun_{false};
    uint32_t transferred_ = 0, audioBytes_ = 0, lastWriteMs_ = 0;
    uint16_t last_ = 0, next_ = 1, active_ = 0;
    uint8_t playBuffer_ = 0;
    bool ready_ = false;
    char status_[48] = "Ready";
    Sampler* sampler_ = nullptr;
    File file_;
    static void released(void* context, void* data, size_t count);
    bool drain();
    void finishRecord(const char* reason);
    void finishPlay(const char* message);
    bool fail(const char* message);
};
