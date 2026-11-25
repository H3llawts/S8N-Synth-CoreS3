#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr const char* FILE_WRITE = "w";
struct Entry {
  char path[24]{};
  uint8_t data[2048]{};
  size_t length = 0;
};
struct File {
  Entry* item = nullptr;
  size_t* budget = nullptr;
  size_t pos = 0;
  explicit operator bool() { return item != nullptr; }
  size_t size() { return item->length; }
  size_t read(uint8_t* target, size_t n) {
    if (n > item->length-pos) n = item->length-pos;
    memcpy(target,item->data+pos,n);
    pos += n;
    return n;
  }
  size_t write(const uint8_t* data, size_t n) {
    if (n>*budget) n=*budget;
    if (n>sizeof(item->data)-pos) n=sizeof(item->data)-pos;
    memcpy(item->data+pos,data,n);
    pos += n;
    item->length=pos;
    *budget -= n;
    return n;
  }
  void flush() {}
  void close() { item=nullptr; }
};
struct SDStub {
  Entry nodes[6];
  size_t budget = 1000000;
  bool directory = false;
  Entry* find(const char* path) {
    for (auto& entry : nodes)
      if (!strcmp(entry.path,path)) return &entry;
    return nullptr;
  }
  bool exists(const char* path) { return strcmp(path,"/S8N") ? find(path)!=nullptr : directory; }
  bool mkdir(const char*) { return directory=true; }
  File open(const char* path, const char* mode="r") {
    Entry* f=find(path);
    if (!strcmp(mode,"w")) {
      if (!f) for (auto& entry : nodes) if (!entry.path[0]) { f=&entry; strcpy(f->path,path); break; }
      if (f) f->length=0;
    }
    return File{f,&budget,0};
  }
};
extern SDStub SD;
