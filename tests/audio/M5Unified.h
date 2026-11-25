#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <vector>
#include <deque>
struct MicType {
    void* owner=nullptr;
    void (*cb)(void*,void*,size_t)=nullptr;
    int16_t* dst=nullptr;
    size_t length=0;
    uint32_t value=0;
    bool broken=false;
    void setBufferReleaseCallback(void* o,void(*f)(void*,void*,size_t)){owner=o;cb=f;}
    void setSampleRate(uint32_t){}
    bool begin(){return true;}
    void end(){dst=nullptr;length=0;}
    bool record(int16_t* d,size_t n) {if(broken)return false; assert(!dst); dst=d;length=n;return true;}
    bool record(int16_t* d,size_t n,uint32_t rate){assert(rate==16000);return record(d,n);}
    void pump(){assert(dst); auto* d=dst;auto n=length;dst=nullptr; for(size_t i=0;i<n;++i)d[i]=int16_t(++value);cb(owner,d,n);}
};
struct Spk {
    std::deque<std::pair<const int16_t*,size_t>> pending;
    std::vector<int16_t> played;
    bool active=false;int last=-1;
    bool begin(){return active=true;}
    void end(){active=false;pending.clear();}
    bool isRunning(){return active;}
    void stop(uint8_t channel){if(channel==6)pending.clear();}
    unsigned isPlaying(uint8_t channel){return channel==6?pending.size():0;}
    void pump(){assert(!pending.empty());auto block=pending.front();played.insert(played.end(),block.first,block.first+block.second);pending.pop_front();}
    void setChannelVolume(uint8_t,uint8_t){}
    bool playRaw(const int16_t* d, size_t n, uint32_t rate,bool stereo,int repeat,int channel,bool replace=false){
        assert(d && n && rate==16000 && !stereo && repeat==1);
        if(channel==6){assert(!replace && pending.size()<2);for(const auto& block:pending)assert(d!=block.first);pending.push_back({d,n});}
        else assert(replace);
        last=channel;return begin();
    }
};
struct Board { MicType Mic; Spk Speaker; };
inline Board M5;
inline unsigned nowMs=0;
inline uint32_t millis(){return nowMs;}
