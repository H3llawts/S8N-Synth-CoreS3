#include "SD.h"
#include "Storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
SDStub SD;
uint32_t crc(const Project& project, uint32_t gen) {
  uint32_t n=~gen;
  for(size_t i=0;i<sizeof(project);++i) {
    n ^= reinterpret_cast<const uint8_t*>(&project)[i];
    for(unsigned bit=0;bit<8;++bit) n = n&1 ? (n>>1)^0xedb88320U : n>>1;
  }
  return ~n;
}
void stamp(Entry& file, const Project& p, uint32_t generation) {
  uint32_t hash=crc(p,generation);
  memcpy(file.data+8,&generation,4);
  memcpy(file.data+12,&hash,4);
  memcpy(file.data+16,&p,sizeof(p));
}
int main() {
  Project one{},two{},three{},destination{};
  Engine::factory(one);
  two=one; two.bpm+=10;
  three=one; three.bpm+=20;
  destination=three;
  Storage s;
  assert(!s.load(0,destination) && destination.bpm==three.bpm);
  assert(!s.save(8,one));
  assert(s.save(0,one)); assert(s.save(0,two));
  assert(s.load(0,destination) && destination.bpm==two.bpm);
  Entry* a=SD.find("/S8N/P01A.S8N"); Entry* b=SD.find("/S8N/P01B.S8N");
  assert(a && b);
  b->data[20]^=1;
  assert(s.load(0,destination) && destination.bpm==one.bpm);
  b->data[20]^=1;
  SD.budget=sizeof(Project)/3;
  assert(!s.save(0,three)); // truncated A
  assert(s.load(0,destination) && destination.bpm==two.bpm);
  SD.budget=1000000;
  assert(s.save(0,three));
  assert(s.load(0,destination) && destination.bpm==three.bpm);
  auto bad=one; bad.steps[3][7][15].velocity=255;
  assert(!s.save(0,bad));
  stamp(*a,bad,3); // internally consistent checksum but invalid field data
  assert(s.load(0,destination) && destination.bpm==two.bpm);
  b->length=4;
  assert(!s.load(0,destination) && destination.bpm==two.bpm);
  b->length=16+sizeof(two);
  stamp(*a,one,0xffffffffu);
  stamp(*b,two,0u);
  assert(s.load(0,destination) && destination.bpm==two.bpm);
  assert(s.save(0,three));
  assert(s.load(0,destination) && destination.bpm==three.bpm);
  puts("PASS: empty slot, alternating banks, newest recovery, short-write fallback, valid CRC with invalid fields, generation rollover, unchanged destination on failure.");
}
