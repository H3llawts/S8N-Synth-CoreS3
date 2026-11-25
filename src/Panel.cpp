#include "Panel.h"
#include "Patches.h"
#include <M5Unified.h>
#include <cstdio>
#include <cstring>

namespace {
// Rendering and touch decoding share this fixed landscape geometry.
constexpr int Width = 320, Height = 240, HeaderH = 30, TabsY = 211;
constexpr int GridX = 46, GridY = 62, CellW = 17, CellH = 16;
constexpr int KnobY = 79, KnobH = 29, PianoX = 8, PianoY = 142, PianoH = 53;
constexpr int WhiteW = 38, BlackW = 20, BlackH = 32;
constexpr int WhiteNotes[] = {0,2,4,5,7,9,11,12};
constexpr const char* LaneNames[] = {"KICK","SNARE","HAT","BASS","LEAD","CHORD","SMP A","SMP B"};
constexpr const char* PageNames[] = {"GRID","SOUND","PADS","SENSORS","PROJECT"};
constexpr uint32_t Back = 0x101617, Surface = 0x1c2628, Raised = 0x293638;
constexpr uint32_t Line = 0x3d4b4d, Light = 0xf1f1e8, Muted = 0xb2bfbd;
constexpr uint32_t Lime = 0xc7fa64, Olive = 0x809955, Pink = 0xff7faf;
constexpr uint32_t Aqua = 0x79d8c9, Yellow = 0xffcd76;
M5Canvas canvas;
lgfx::LGFXBase* surface = nullptr;
bool buffered = false;

void rect(int x,int y,int w,int h,uint32_t color) {
    surface->fillRect(x,y,w,h,color);
}
void label(int x,int y,const char* text,uint32_t color,int scale=1) {
    surface->setTextSize(scale);
    surface->setTextColor(color);
    surface->drawString(text,x,y);
}
void center(int x,int y,int w,int h,const char* text,uint32_t color,int scale=1) {
    const int length = static_cast<int>(strlen(text));
    label(x+(w-length*6*scale)/2,y+(h-8*scale)/2,text,color,scale);
}
void button(int x,int y,int w,int h,const char* text,uint32_t bg=Raised,uint32_t fg=Light) {
    rect(x,y,w,h,bg);
    center(x,y,w,h,text,fg);
}
void noteName(uint8_t n,char (&out)[8]) {
    const char* notes[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    snprintf(out,sizeof(out),"%s%d",notes[n%12],static_cast<int>(n)/12-1);
}
void header(const PanelState& s) {
    rect(0,0,49,HeaderH,Lime);
    center(0,0,49,HeaderH,"S8N",Back,2);
    char bpm[8]; snprintf(bpm,sizeof(bpm),"%u",s.project->bpm);
    button(54,3,22,24,"-",Surface,Muted);
    center(76,3,34,24,bpm,Light);
    button(110,3,22,24,"+",Surface,Muted);
    button(137,3,62,24,s.playing?"STOP":"PLAY",s.playing?Lime:Raised,s.playing?Back:Light);
    button(204,3,52,24,"DUB",s.armed?Pink:Surface,s.armed?Back:Muted);
    button(261,3,55,24,"PANIC",Surface,Pink);
    rect(0,HeaderH,Width,1,Line);
}
void footer(const PanelState& s,const char* contextual) {
    const char* text = s.notice && s.notice[0] ? s.notice : contextual;
    char clipped[52]; snprintf(clipped,sizeof(clipped),"%.50s",text);
    label(6,200,clipped,s.notice && s.notice[0] ? Yellow : Muted);
    rect(0,TabsY,Width,1,Line);
    for (int i=0;i<5;++i) {
        const int x=i*64;
        bool chosen=static_cast<int>(s.page)==i;
        button(x,TabsY+1,63,28,PageNames[i],chosen?Raised:Back,chosen?Lime:Muted);
        if (chosen) rect(x+7,TabsY+1,50,2,Lime);
    }
}
void grid(const PanelState& s) {
    const auto& p=*s.project;
    char buf[52];
    label(6,44,"PAT",Muted);
    for (int i=0;i<4;++i) {
        char pat[]={static_cast<char>('A'+i),0};
        button(36+i*31,35,28,23,pat,s.pattern==i?Lime:Surface,s.pattern==i?Back:Muted);
    }
    snprintf(buf,sizeof(buf),"SW %u%%",p.swing);
    center(164,35,70,23,buf,Muted);
    bool muted = p.tracks[s.track].muted;
    button(241,35,74,23,muted?"UNMUTE":"MUTE",muted?Pink:Raised,muted?Back:Light);
    for (int lane=0;lane<8;++lane) {
        const int y=GridY+lane*CellH;
        const bool chosen = lane==s.track;
        const bool off = p.tracks[lane].muted;
        if (chosen) rect(3,y,40,CellH-2,Lime);
        center(3,y,40,CellH-2,LaneNames[lane],chosen?Back:(off?Line:Muted));
        if (off) rect(5,y+7,36,1,chosen?Back:Line);
        for (int step=0;step<16;++step) {
            const int x=GridX+step*CellW;
            const uint8_t v=p.steps[s.pattern][lane][step].velocity;
            uint32_t fill = step/4%2==0?Surface:Raised;
            if (v) fill=off?Line:(v>=104?Lime:Olive);
            rect(x+1,y+1,CellW-3,CellH-4,fill);
            if (v && !off) rect(x+1,y+1,2,CellH-4,v>=104?Light:Lime);
            if (s.playing && s.playhead==step) rect(x+6,y+1,3,CellH-4,Pink);
            if (chosen && s.step==step) surface->drawRect(x,y,CellW-1,CellH-2,Light);
        }
    }
    char note[8]; const auto& step=p.steps[s.pattern][s.track][s.step]; noteName(step.note,note);
    snprintf(buf,sizeof(buf),"%s  /  %02u   %s   VEL %u   GATE %u%%",
             LaneNames[s.track],s.step+1,note,step.velocity,step.gate);
    footer(s,buf);
}
void field(int i,const char* name,int value,int maximum) {
    const int x=6+(i%2)*155, y=KnobY+(i/2)*KnobH;
    char text[24]; snprintf(text,sizeof(text),"%s %d",name,value);
    rect(x,y,150,25,Surface);
    center(x,y,19,22,"-",Muted);
    center(x+19,y,112,20,text,Light);
    center(x+131,y,19,22,"+",Lime);
    rect(x+25,y+20,100,2,Line);
    if (value>0) rect(x+25,y+20,value*100/maximum,2,Aqua);
}
bool hasBlack(int white) { return WhiteNotes[white+1]-WhiteNotes[white]==2; }
void piano(const PanelState& s) {
    const char* names[] = {"C","D","E","F","G","A","B","C"};
    for (int i=0;i<8;++i) {
        int x=PianoX+i*WhiteW;
        rect(x,PianoY,WhiteW-1,PianoH,s.heldKey==WhiteNotes[i]?Lime:Light);
        center(x,PianoY+33,WhiteW-1,19,names[i],Back);
    }
    for (int i=0;i<7;++i) if (hasBlack(i)) {
        const int x=PianoX+(i+1)*WhiteW-BlackW/2;
        rect(x,PianoY,BlackW,BlackH,s.heldKey==WhiteNotes[i]+1?Pink:Surface);
    }
}
void sound(const PanelState& s) {
    const auto& t=s.project->tracks[s.track];
    const auto& step=s.project->steps[s.pattern][s.track][s.step];
    const bool drum=s.track<3, pad=s.track>=6;
    button(6,36,24,35,"<");
    button(290,36,24,35,">");
    char meta[36]; snprintf(meta,sizeof(meta),"%s   /   %s",LaneNames[s.track],pad?"LOCAL SAMPLE":"UNIT SYNTH");
    label(38,37,meta,Muted);
    char voice[25];
    if (pad) snprintf(voice,sizeof(voice),"Sample pad %u",t.sampleSlot+1);
    else if (drum) snprintf(voice,sizeof(voice),"Drum note %u",step.note);
    else snprintf(voice,sizeof(voice),"%s",patches[t.program]);
    label(37,50,voice,Light,2);
    field(0,pad?"PAD":(drum?"NOTE":"PATCH"),pad?t.sampleSlot+1:(drum?step.note:t.program+1),pad?4:(drum?127:128));
    field(1,"LEVEL",t.volume,127);
    if (!drum && !pad) { field(2,"CUTOFF",t.cutoff,127); field(3,"REVERB",t.reverb,127); }
    piano(s);
    char info[52];
    if (drum) snprintf(info,sizeof(info),"Kbd/touch base %u  /  UART drum channel 10",Engine::defaultNote(s.track));
    else if (pad) snprintf(info,sizeof(info),"Tap keys to trigger local pad %u",t.sampleSlot+1);
    else snprintf(info,sizeof(info),"Touch keys  C%u - C%u",s.octave,s.octave+1);
    footer(s,info);
}
void pads(const PanelState& s) {
    char text[32];
    for (int i=0;i<4;++i) {
        const int x=6+i*77;
        snprintf(text,sizeof(text),"PAD %02d",i+1);
        button(x,37,73,32,text,i==s.slot?Lime:Surface,i==s.slot?Back:Muted);
    }
    rect(6,75,307,65,Surface);
    for (int i=1;i<8;++i) rect(8+i*38,77,1,61,Raised);
    rect(8,107,303,1,Line);
    for (int i=0;i<64;++i) {
        int amp=s.wave[i]*28/127;
        if (amp) rect(10+i*47/10,107-amp,3,2*amp+1,s.capturing?Pink:Aqua);
    }
    snprintf(text,sizeof(text),"%u ms",s.capturing?s.captureMs:s.padMs);
    label(7,147,s.capturing?"MIC CAPTURE":"MONO PCM  /  16 KHZ",s.capturing?Pink:Muted);
    label(254,147,text,Light);
    button(6,163,130,32,s.capturing?"FINISH CAPTURE":"CAPTURE MIC",s.capturing?Pink:Raised,s.capturing?Back:Pink);
    button(140,163,84,32,"SAVE WAV");
    button(228,163,85,32,"LOAD WAV");
    footer(s,"Tap a pad to select and audition");
}
void sensorMeter(int x,const char* name,const char* route,int value,bool available) {
    rect(x,42,149,99,Surface);
    label(x+9,51,name,Muted);
    char text[12]; snprintf(text,sizeof(text),"%03d",value);
    label(x+8,69,available?text:"---",available?Light:Muted,3);
    int segments=(value*15+126)/127;
    for (int i=0;i<15;++i) rect(x+8+i*9,105,7,8,available && i<segments?Aqua:Line);
    label(x+8,123,available?route:"SENSOR OFFLINE",available?Muted:Yellow);
}
void sensors(const PanelState& s) {
    sensorMeter(6,"CUTOFF","SOURCE / TILT",s.tilt,s.imuReady);
    sensorMeter(163,"REVERB","SOURCE / PROX",s.proximity,s.proximityReady);
    button(6,153,149,38,s.sensorEnabled?"MOTION ON":"MOTION OFF",s.sensorEnabled?Lime:Raised,s.sensorEnabled?Back:Light);
    button(163,153,149,38,"CALIBRATE");
    char target[52];
    snprintf(target,sizeof(target),"Target %s   /   Hold level to calibrate",LaneNames[s.sensorTrack&7]);
    footer(s,target);
}
void project(const PanelState& s) {
    label(7,39,"PROJECT SLOT",Muted);
    label(195,39,s.cardKB?"KB ON":"KB --",s.cardKB?Aqua:Muted);
    label(249,39,s.sdReady?"SD READY":"NO SD",s.sdReady?Aqua:Yellow);
    for (int i=0;i<8;++i) {
        char n[]={static_cast<char>('1'+i),0};
        button(6+i*39,54,35,28,n,s.projectSlot==i?Lime:Surface,s.projectSlot==i?Back:Muted);
    }
    button(6,89,149,29,"SAVE PROJECT");
    button(163,89,149,29,"LOAD PROJECT");
    rect(6,128,307,1,Line);
    label(7,137,"MICROPHONE TAKE",Muted);
    char time[20]; snprintf(time,sizeof(time),"%02lu:%02lu",
         static_cast<unsigned long>(s.takeSeconds/60),static_cast<unsigned long>(s.takeSeconds%60));
    if (s.takeRecording || s.takePlaying) label(265,137,time,s.takeRecording?Pink:Aqua);
    button(6,157,149,35,s.takeRecording?"FINISH TAKE":"RECORD TAKE",s.takeRecording?Pink:Raised,s.takeRecording?Back:Pink);
    button(163,157,149,35,s.takePlaying?"STOP PLAYBACK":"PLAY LATEST",s.takePlaying?Aqua:Raised,s.takePlaying?Back:Light);
    footer(s,s.sdReady?"/S8N/TAKES  /  WAV 16 kHz":"Insert a FAT32 microSD and restart for storage");
}
} // namespace

void Panel::begin() {
    canvas.setColorDepth(16);
    canvas.setPsram(true);
    buffered = canvas.createSprite(Width,Height)!=nullptr;
    surface = buffered ? static_cast<lgfx::LGFXBase*>(&canvas) : static_cast<lgfx::LGFXBase*>(&M5.Display);
}
void Panel::draw(const PanelState& s) {
    if (!surface || !s.project) return;
    surface->fillScreen(Back);
    header(s);
    switch(s.page) {
        case Page::Grid: grid(s); break;
        case Page::Sound: sound(s); break;
        case Page::Pads: pads(s); break;
        case Page::Sensor: sensors(s); break;
        case Page::Project: project(s); break;
    }
    if (buffered) canvas.pushSprite(&M5.Display,0,0);
}

Action Panel::hit(const PanelState& s,int x,int y) {
    if (x<0 || x>=Width || y<0 || y>=Height) return {};
    if (y>=TabsY) return {Cmd::Page,x/64};
    if (y<HeaderH) {
        if (x>=54 && x<76) return {Cmd::Tempo,-1};
        if (x>=110 && x<132) return {Cmd::Tempo,1};
        if (x>=137 && x<199) return {Cmd::Transport};
        if (x>=204 && x<256) return {Cmd::Arm};
        if (x>=261 && x<316) return {Cmd::Panic};
        return {};
    }
    switch (s.page) {
    case Page::Grid:
        if (y>=35 && y<58) {
            if (x>=36 && x<160) return {Cmd::Pattern,(x-36)/31};
            if (x>=241 && x<315) return {Cmd::Mute};
        }
        if (y>=GridY && y<GridY+CellH*8) {
            const int lane=(y-GridY)/CellH;
            if (x<GridX) return {Cmd::Lane,lane};
            if (x<GridX+CellW*16) return {Cmd::Cell,lane,(x-GridX)/CellW};
        }
        break;
    case Page::Sound:
        if (y>=36 && y<71) {
            if (x>=6 && x<30) return {Cmd::Lane,(s.track+7)%8};
            if (x>=290 && x<314) return {Cmd::Lane,(s.track+1)%8};
        }
        if (y>=KnobY && y<KnobY+2*KnobH && x>=6 && x<311) {
            const int column=(x-6)/155, u=(x-6)%155, row=(y-KnobY)/KnobH;
            if (u>=150 || (y-KnobY)%KnobH>=25 || (row && (s.track<3 || s.track>=6))) return {};
            return {Cmd::SoundValue,row*2+column,u<75?-1:1};
        }
        if (y>=PianoY && y<PianoY+PianoH && x>=PianoX && x<PianoX+8*WhiteW) {
            if (y<PianoY+BlackH) for (int i=0;i<7;++i) {
                const int left=PianoX+(i+1)*WhiteW-BlackW/2;
                if (hasBlack(i) && x>=left && x<left+BlackW) return {Cmd::Key,WhiteNotes[i]+1};
            }
            return {Cmd::Key,WhiteNotes[(x-PianoX)/WhiteW]};
        }
        break;
    case Page::Pads:
        if (y>=37 && y<69 && x>=6 && x<310) return {Cmd::Pad,(x-6)/77};
        if (y>=163 && y<195) {
            if (x>=6 && x<136) return {Cmd::Capture};
            if (x>=140 && x<224) return {Cmd::SavePad};
            if (x>=228 && x<313) return {Cmd::LoadPad};
        }
        break;
    case Page::Sensor:
        if (y>=153 && y<191) {
            if (x>=6 && x<155) return {Cmd::SensorEnable};
            if (x>=163 && x<312) return {Cmd::Calibrate};
        }
        break;
    case Page::Project:
        if (y>=54 && y<82 && x>=6 && x<314) return {Cmd::ProjectSlot,(x-6)/39};
        if (y>=89 && y<118) {
            if (x>=6 && x<155) return {Cmd::SaveProject};
            if (x>=163 && x<312) return {Cmd::LoadProject};
        }
        if (y>=157 && y<192) {
            if (x>=6 && x<155) return {Cmd::RecordTake};
            if (x>=163 && x<312) return {Cmd::PlayTake};
        }
        break;
    }
    return {};
}
