#pragma once
#include <algorithm>
#include <map>
#include <vector>
#include <cstring>
#include <string>
#include <cstdint>
#include <set>
#include <memory>
#define FILE_READ "r"
#define FILE_WRITE "w"
struct File {
    std::vector<uint8_t>* data=nullptr;
    size_t pos=0;
    std::string path;
    std::vector<std::string> entries;
    bool directory=false;
    bool* writeFault=nullptr;
    const char* name(){return path.c_str();}
    bool isDirectory(){return directory;}
    File openNextFile();
    bool seek(size_t p){if(!data || p>data->size())return false;pos=p;return true;}
    uint32_t position(){return pos;}
    size_t size(){return data?data->size():0;}
    explicit operator bool() const{return data!=nullptr;}
    void close(){data=nullptr;}
    void flush(){}
    size_t read(uint8_t* out,size_t n){
        if(!data)return 0;n=std::min(n,data->size()-pos);memcpy(out,data->data()+pos,n);pos+=n;return n;
    }
    size_t write(const uint8_t* in,size_t n){
        if(!data || (writeFault && *writeFault))return 0;data->resize(std::max(data->size(),pos+n));memcpy(data->data()+pos,in,n);pos+=n;return n;
    }
};
struct Storage {
    std::map<std::string,std::vector<uint8_t>> files;
    std::set<std::string> directories;
    bool writeFault=false;
    std::string badRename;
    bool exists(const char* path){return files.count(path);}
    bool mkdir(const char* path){files[path]={};directories.insert(path);return true;}
    bool remove(const char* path){return files.erase(path);}
    bool rename(const char* from,const char* to){
        if(badRename==from || !exists(from) || exists(to))return false;
        files[to]=files[from];files.erase(from);return true;
    }
    File open(const char* path,const char* mode=FILE_READ){
        if(mode[0]=='w')files[path]={}; if(!exists(path))return {};
        File f;f.data=&files[path];f.path=path;f.directory=directories.count(path);f.writeFault=&writeFault;
        if(f.directory){std::string prefix=f.path+"/";for(auto &x:files)if(x.first.find(prefix)==0 && x.first.find('/',prefix.size())==std::string::npos)f.entries.push_back(x.first);}
        return f;
    }
};
inline Storage SD;
inline File File::openNextFile(){return pos<entries.size()?SD.open(entries[pos++].c_str()):File{};}
