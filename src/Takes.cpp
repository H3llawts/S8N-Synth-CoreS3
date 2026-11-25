#include "Takes.h"
#include "Sampler.h"
#include <M5Unified.h>
#include <SD.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace {
constexpr char Dir[] = "/S8N/TAKES";
constexpr int Channel = 6;
struct PcmHeader {
    char riff[4]; uint32_t size; char wave[4], fmt[4]; uint32_t fmtSize;
    uint16_t encoding, channels; uint32_t rate, byteRate; uint16_t align, bits;
    char data[4]; uint32_t bytes;
};
static_assert(sizeof(PcmHeader) == 44, "WAV layout");
PcmHeader header(uint32_t bytes) {
    return {{'R','I','F','F'},36+bytes,{'W','A','V','E'},{'f','m','t',' '},16,
            1,1,16000,32000,2,16,{'d','a','t','a'},bytes};
}
void path(char* dest, uint16_t id, const char* suffix) {
    snprintf(dest, 32, "%s/T%04u.%s", Dir, id, suffix);
}
}

bool Takes::fail(const char* message) {
    snprintf(status_, sizeof(status_), "%s", message);
    return false;
}

void Takes::begin(Sampler& sampler, bool sdReady) {
    sampler_ = &sampler;
    if (!sdReady) return;
    if (!SD.exists("/S8N") && !SD.mkdir("/S8N")) return;
    if (!SD.exists(Dir) && !SD.mkdir(Dir)) return;
    File directory = SD.open(Dir);
    if (!directory || !directory.isDirectory()) return;
    for (File f = directory.openNextFile(); f; f = directory.openNextFile()) {
        const char* name = strrchr(f.name(), '/');
        name = name ? name + 1 : f.name();
        unsigned id; char ext[4] = {}, tail;
        if (strlen(name) == 9 && sscanf(name, "T%4u.%3s%c", &id, ext, &tail) == 2 && id <= 9999) {
            if (!strcmp(ext, "WAV") && id > last_) last_ = id;
            if ((!strcmp(ext, "TMP") || !strcmp(ext, "WAV")) && id >= next_) next_ = id+1;
        }
        f.close();
    }
    ready_ = true;
}

bool Takes::record() {
    if (!ready_) return fail("Need writable microSD");
    if (sampler_->saving()) return fail("Wait for pad save");
    if (mode_ != Idle || sampler_->capturing()) return fail("Audio is busy");
    char tmp[32], wav[32];
    active_ = 0;
    while (next_ <= 9999) {
        active_ = next_++;
        path(tmp,active_,"TMP"); path(wav,active_,"WAV");
        if (!SD.exists(tmp) && !SD.exists(wav)) break;
        active_ = 0;
    }
    if (!active_) return fail("Take directory is full");
    file_ = SD.open(tmp, FILE_WRITE);
    PcmHeader h = header(0);
    if (!file_ || file_.write((const uint8_t*)&h, sizeof(h)) != sizeof(h)) {
        file_.close(); active_ = 0; return fail("Cannot create take");
    }
    sampler_->suspendAudio();
    auto& mic = M5.Mic;
    mic.end();
    mic.setSampleRate(Rate);
    mic.setBufferReleaseCallback(this, released);
    transferred_ = 0; filled_ = 0; drained_ = 0; overrun_ = false;
    accepting_ = true;
    if (!mic.begin() || !mic.record(audio_[0],Block,Rate)) {
        accepting_ = false; mic.end(); mic.setBufferReleaseCallback(nullptr,nullptr);
        sampler_->resumeAudio(); file_.close(); SD.remove(tmp); active_ = 0;
        return fail("Mic did not start");
    }
    mode_ = Record;
    lastWriteMs_ = millis();
    snprintf(status_,sizeof(status_),"Recording T%04u",active_);
    return true;
}

void Takes::released(void* context, void*, size_t) {
    auto& self = *static_cast<Takes*>(context);
    const uint32_t next = self.filled_.fetch_add(1) + 1;
    if (!self.accepting_.load()) return;
    if (next - self.drained_.load() >= Blocks ||
        !M5.Mic.record(self.audio_[next % Blocks],Block,Rate))
        self.overrun_ = true;
}

bool Takes::drain() {
    uint32_t i = drained_.load();
    while (i != filled_.load()) {
        const size_t expected = sizeof(audio_[0]);
        if (file_.write((const uint8_t*)audio_[i % Blocks], expected) != expected) return false;
        transferred_ += expected;
        drained_.store(++i);
        lastWriteMs_ = millis();
    }
    return true;
}

void Takes::finishRecord(const char* reason) {
    accepting_ = false;
    M5.Mic.end();
    M5.Mic.setBufferReleaseCallback(nullptr,nullptr);
    bool ok = strcmp(reason,"SD write failed") && drain() && transferred_;
    PcmHeader h = header(transferred_);
    ok = ok && file_.seek(0) && file_.write((const uint8_t*)&h,sizeof(h)) == sizeof(h);
    file_.flush(); file_.close();
    char tmp[32], wav[32];
    path(tmp,active_,"TMP"); path(wav,active_,"WAV");
    ok = ok && SD.rename(tmp,wav);
    if (ok) {
        last_ = active_;
        snprintf(status_,sizeof(status_),"%s T%04u.WAV",reason,active_);
    } else if (!transferred_ && strcmp(reason,"SD write failed")) {
        SD.remove(tmp); fail("Take too short");
    } else fail("SD write failed; TMP retained");
    active_ = 0; mode_ = Idle;
    sampler_->resumeAudio();
}

bool Takes::playLatest() {
    if (!last_) return fail("No WAV take yet");
    if (sampler_->saving()) return fail("Wait for pad save");
    if (mode_ != Idle || sampler_->capturing()) return fail("Audio is busy");
    char filename[32]; path(filename,last_,"WAV");
    file_ = SD.open(filename);
    PcmHeader h{};
    if (!file_ || file_.read((uint8_t*)&h,sizeof(h)) != sizeof(h)) {
        file_.close(); return fail("Cannot read take");
    }
    const auto expected = header(h.bytes);
    if (!h.bytes || h.bytes % 2 || memcmp(&h,&expected,sizeof(h)) ||
        file_.size()-sizeof(h) != h.bytes) { file_.close(); return fail("Invalid take WAV"); }
    sampler_->suspendAudio();
    if (!M5.Speaker.begin()) {
        file_.close(); sampler_->resumeAudio(); return fail("Speaker unavailable");
    }
    M5.Speaker.setChannelVolume(Channel,255);
    mode_ = Play; audioBytes_ = h.bytes; transferred_ = 0; playBuffer_ = 0;
    snprintf(status_,sizeof(status_),"Playing T%04u.WAV",last_);
    return true;
}

void Takes::finishPlay(const char* message) {
    M5.Speaker.stop(Channel);
    M5.Speaker.end();
    file_.close(); mode_ = Idle; sampler_->resumeAudio(); fail(message);
}

void Takes::stop() {
    if (recording()) finishRecord("Saved");
    else if (playing()) finishPlay("Take stopped");
}

void Takes::service() {
    if (recording()) {
        if (!drain()) finishRecord("SD write failed");
        else if (overrun_.load() || millis()-lastWriteMs_ > 1000) finishRecord("Capture paused; kept");
        else if (transferred_ >= 32000UL * 3600) finishRecord("1h limit; saved");
    } else if (playing()) {
        auto& out = M5.Speaker;
        while (transferred_ < audioBytes_ && out.isPlaying(Channel) < 2) {
            const size_t n = std::min<uint32_t>(sizeof(audio_[0]), audioBytes_ - transferred_);
            auto* buf = audio_[playBuffer_];
            if (file_.read((uint8_t*)buf,n) != n || !out.playRaw(buf,n/2,Rate,false,1,Channel)) {
                finishPlay("Take playback failed"); return;
            }
            playBuffer_ = (playBuffer_ + 1) % 3;
            transferred_ += n;
        }
        if (transferred_ == audioBytes_ && !out.isPlaying(Channel)) finishPlay("Take finished");
    }
}
