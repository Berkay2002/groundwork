#include "world/WorldSave.h"
#include "world/Block.h"
#include "platform/SaveIO.h"
#include <cstring>
#include <fstream>

namespace worldsave {
namespace {
constexpr char CHUNK_MAGIC[4] = {'M', 'C', 'C', 'H'};
constexpr uint32_t CHUNK_VERSION = 1;

constexpr char LEVEL_MAGIC[4] = {'M', 'C', 'L', 'V'};
// v1: magic + version + seed. v2 appends dayTime.
constexpr uint32_t LEVEL_VERSION = 2;
}

std::string chunkPath(const std::string& saveDir, int cx, int cz) {
    return saveDir + "/c_" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
}

bool loadChunkFile(const std::string& path, uint8_t* blocks, size_t blockCount) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4];
    uint32_t version = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    if (!f || std::memcmp(magic, CHUNK_MAGIC, 4) != 0 || version != CHUNK_VERSION)
        return false;

    f.read(reinterpret_cast<char*>(blocks), (std::streamsize)blockCount);
    if (!f || f.gcount() != (std::streamsize)blockCount) return false;

    for (size_t i = 0; i < blockCount; ++i)
        if (blocks[i] >= BLOCK_TYPES) blocks[i] = uint8_t(Block::Air);
    return true;
}

bool saveChunkFile(const std::string& path, const uint8_t* blocks, size_t blockCount) {
    return atomicSave(path, [&](std::ofstream& f) {
        f.write(CHUNK_MAGIC, 4);
        f.write(reinterpret_cast<const char*>(&CHUNK_VERSION), 4);
        f.write(reinterpret_cast<const char*>(blocks), (std::streamsize)blockCount);
    });
}

LevelFile loadLevelFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    char magic[4];
    uint32_t version = 0, seed = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    f.read(reinterpret_cast<char*>(&seed), 4);
    if (!f || std::memcmp(magic, LEVEL_MAGIC, 4) != 0 ||
        version < 1 || version > LEVEL_VERSION)
        return {};

    LevelFile out;
    out.ok = true;
    out.seed = seed;
    if (version >= 2) {
        f.read(reinterpret_cast<char*>(&out.dayTime), 4);
        if (!f) return {};
    }
    return out;
}

bool saveLevelFile(const std::string& path, uint32_t seed, float dayTime) {
    return atomicSave(path, [&](std::ofstream& f) {
        f.write(LEVEL_MAGIC, 4);
        f.write(reinterpret_cast<const char*>(&LEVEL_VERSION), 4);
        f.write(reinterpret_cast<const char*>(&seed), 4);
        f.write(reinterpret_cast<const char*>(&dayTime), 4);
    });
}

}
