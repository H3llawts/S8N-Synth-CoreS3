#include "Sampler.h"
#include "Takes.h"
#include "M5Unified.h"
#include <cassert>
#include <cstdio>

int main(){
    Sampler s;s.begin(true);
    Takes t; t.begin(s,true);
    assert(!t.playLatest() && s.startCapture(0));
    assert(!t.record());s.stopCapture();M5.Mic.pump();s.service();
    assert(s.saving() && !t.record()); while(s.saving()) s.service();
    assert(t.record() && !s.trigger(1) && !s.startCapture(0));
    assert(!t.record() && !t.playLatest());
    auto& mic=M5.Mic;
    mic.value=0;
    for(unsigned i=0;i<110;++i){mic.pump();nowMs+=32;t.service();}
    assert(t.seconds()==3);
    t.stop();assert(!t.recording() && s.trigger(0));
    auto wav = SD.files.at("/S8N/TAKES/T0001.WAV");
    assert(wav.size()==110*1024+44 && wav[44]==1 && wav[46]==2);
    auto& spk=M5.Speaker;
    assert(t.playLatest() && !t.record() && !s.trigger(2));
    for(unsigned i=0;t.playing();++i){
        assert(i<150);t.service();if(!spk.pending.empty())spk.pump();
    }
    assert(spk.played.size()==110*512);
    for(size_t i=0;i<spk.played.size();++i)assert(spk.played[i]==int16_t(i+1));

    SD.files["/S8N/TAKES/T0002.TMP"]={0xde,0xad};
    Takes reboot;reboot.begin(s,true);
    assert(reboot.record());
    for(int i=0;i<8;++i)mic.pump();
    assert(!mic.dst);
    reboot.service();
    assert(!reboot.recording() && std::strstr(reboot.status(),"kept"));
    assert(SD.files.at("/S8N/TAKES/T0003.WAV").size()==8*1024+44);
    assert(SD.files.at("/S8N/TAKES/T0001.WAV")==wav && SD.files.at("/S8N/TAKES/T0002.TMP").size()==2);
    assert(reboot.playLatest());reboot.service();reboot.stop();assert(!reboot.playing() && spk.pending.empty());
    assert(reboot.record());mic.pump();reboot.service();mic.pump();SD.writeFault=true;reboot.service();SD.writeFault=false;
    assert(!reboot.recording() && !SD.exists("/S8N/TAKES/T0004.WAV") && SD.exists("/S8N/TAKES/T0004.TMP"));
    assert(reboot.record());reboot.stop();assert(!SD.exists("/S8N/TAKES/T0005.TMP"));
    assert(reboot.record());mic.pump();reboot.service();nowMs+=1100;reboot.service();
    assert(!reboot.recording() && SD.exists("/S8N/TAKES/T0006.WAV") && s.trigger(0));
    assert(!SD.exists("/S8N/TAKES/T0004.WAV") && SD.files.at("/S8N/TAKES/T0001.WAV")==wav);
    Takes unavailable;unavailable.begin(s,false);assert(!unavailable.record());
    puts("Long-take checks passed: continuous three-second capture, PCM byte-for-byte playback through triple buffers, exclusive audio, stop, collision handling, reboot, SD failure, stalled queue and overrun.");
}
