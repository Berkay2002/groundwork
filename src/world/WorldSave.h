#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace worldsave {

struct LevelFile {
    bool ok = false;
    uint32_t seed = 0;
    float dayTime = 0.0f; // seconds; v1 files migrate to morning
};

std::string chunkPath(const std::string& saveDir, int cx, int cz);

bool loadChunkFile(const std::string& path, uint8_t* blocks, size_t blockCount);
bool saveChunkFile(const std::string& path, const uint8_t* blocks, size_t blockCount);

LevelFile loadLevelFile(const std::string& path);
bool saveLevelFile(const std::string& path, uint32_t seed, float dayTime);

}
