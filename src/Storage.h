#pragma once
#include "Engine.h"

// Each slot alternates two complete generations. A short write can only
// damage the older generation. A load changes live state after validation.
class Storage {
public:
    bool save(uint8_t slot, const Project& data);
    bool load(uint8_t slot, Project& data);
private:
    Project scratch_{};
    bool read(uint8_t slot, char bank, uint32_t& generation);
};
