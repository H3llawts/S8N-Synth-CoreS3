#include "Storage.h"
#include <SD.h>
#include <cstdio>
#include <cstring>

namespace {
struct Header {
    char magic[4]; uint16_t version, length; uint32_t generation, crc;
};
static_assert(sizeof(Header) == 16 && sizeof(Project) == 1588, "S8N project format V1");
void filename(char* out, uint8_t slot, char bank) {
    snprintf(out,24,"/S8N/P%02u%c.S8N",slot+1,bank);
}
uint32_t checksum(const Project& data, uint32_t generation) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
    uint32_t crc = ~generation;
    for (size_t i=0; i<sizeof(data); ++i) {
        crc ^= bytes[i];
        for (int bit=0; bit<8; ++bit) crc = (crc>>1) ^ (0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}
}

bool Storage::read(uint8_t slot, char bank, uint32_t& generation) {
    char name[24]; filename(name,slot,bank);
    if (!SD.exists(name)) return false;
    File f = SD.open(name);
    Header h{};
    if (!f || f.size() != sizeof(h)+sizeof(Project) ||
        f.read((uint8_t*)&h,sizeof(h)) != sizeof(h) || memcmp(h.magic,"S8NP",4) ||
        h.version != 1 || h.length != sizeof(Project) ||
        f.read((uint8_t*)&scratch_,sizeof(Project)) != sizeof(Project) ||
        h.crc != checksum(scratch_,h.generation) || !Engine::valid(scratch_)) return false;
    generation = h.generation;
    return true;
}

bool Storage::load(uint8_t slot, Project& data) {
    if (slot >= 8) return false;
    uint32_t a=0,b=0;
    bool hasA = read(slot,'A',a);
    bool hasB = read(slot,'B',b);
    if (hasA && (!hasB || int32_t(a-b) > 0)) {
        if (!read(slot,'A',a)) return false;
    } else if (!hasB) return false;
    data = scratch_;
    return true;
}

bool Storage::save(uint8_t slot, const Project& data) {
    if (slot >= 8 || !Engine::valid(data)) return false;
    if (!SD.exists("/S8N") && !SD.mkdir("/S8N")) return false;
    uint32_t a=0,b=0;
    bool hasA = read(slot,'A',a), hasB = read(slot,'B',b);
    bool aIsNewest = hasA && (!hasB || int32_t(a-b) > 0);
    char bank = aIsNewest ? 'B' : 'A';
    uint32_t generation = (aIsNewest ? a : b)+1;
    Header h{{'S','8','N','P'},1,sizeof(Project),generation,checksum(data,generation)};
    char name[24]; filename(name,slot,bank);
    File f = SD.open(name,FILE_WRITE);
    if (!f || f.write((const uint8_t*)&h,sizeof(h)) != sizeof(h) ||
        f.write((const uint8_t*)&data,sizeof(data)) != sizeof(data)) return false;
    f.flush(); f.close();
    uint32_t check;
    return read(slot,bank,check) && check == generation;
}
