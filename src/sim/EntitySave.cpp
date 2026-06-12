#include "sim/EntitySave.h"

#include "platform/SaveIO.h"
#include "sim/ItemSave.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
constexpr char ENTITY_MAGIC[4] = {'M', 'C', 'E', 'N'};
constexpr uint32_t ENTITY_VERSION = 1;
constexpr uint8_t ENTITY_DROPPED_ITEM = 1;
constexpr uint32_t MAX_ENTITIES_PER_CHUNK = 4096;
constexpr uint32_t MAX_ENTITY_PAYLOAD_BYTES = 65536;
constexpr uint32_t DROPPED_ITEM_PAYLOAD_BYTES = 37;

template <typename T>
bool readRaw(std::istream& in, T& out) {
    in.read(reinterpret_cast<char*>(&out), sizeof(T));
    return bool(in);
}

template <typename T>
void writeRaw(std::ostream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

bool finiteVec(glm::vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool belongsToChunk(glm::vec3 pos, ChunkKey key) {
    int wx = (int)std::floor(pos.x);
    int wz = (int)std::floor(pos.z);
    return World::floorDiv(wx, CHUNK_SIZE) == key.x &&
           World::floorDiv(wz, CHUNK_SIZE) == key.z;
}

std::string droppedPayload(const SavedDroppedItem& item) {
    std::ostringstream out(std::ios::binary);
    writeRaw(out, item.pos.x);
    writeRaw(out, item.pos.y);
    writeRaw(out, item.pos.z);
    writeRaw(out, item.vel.x);
    writeRaw(out, item.vel.y);
    writeRaw(out, item.vel.z);
    writeRaw(out, item.ageTicks);
    writeRaw(out, item.spinSeed);
    writeItemStack(out, item.stack);
    return out.str();
}

bool readDroppedPayload(const std::string& payload, ChunkKey expectedKey,
                        SavedDroppedItem& out) {
    if (payload.size() != DROPPED_ITEM_PAYLOAD_BYTES) return false;
    std::istringstream in(payload, std::ios::binary);
    SavedDroppedItem item;
    if (!readRaw(in, item.pos.x) || !readRaw(in, item.pos.y) ||
        !readRaw(in, item.pos.z) || !readRaw(in, item.vel.x) ||
        !readRaw(in, item.vel.y) || !readRaw(in, item.vel.z) ||
        !readRaw(in, item.ageTicks) || !readRaw(in, item.spinSeed) ||
        !readItemStack(in, item.stack)) {
        return false;
    }
    if (item.stack.empty()) return false;
    if (!finiteVec(item.pos) || !finiteVec(item.vel)) return false;
    if (!belongsToChunk(item.pos, expectedKey)) return false;
    out = item;
    return true;
}
}

std::string entityChunkPath(const std::string& saveDir, ChunkKey key) {
    return saveDir + "/entities/e_" + std::to_string(key.x) + "_" +
           std::to_string(key.z) + ".bin";
}

EntityChunkLoadStatus loadEntityChunkFile(const std::string& path,
                                          ChunkKey expectedKey,
                                          std::vector<SavedDroppedItem>& out) {
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return EntityChunkLoadStatus::Missing;

    auto reject = [&]() {
        out.clear();
        return EntityChunkLoadStatus::Rejected;
    };

    char magic[4];
    uint32_t version = 0, count = 0;
    f.read(magic, 4);
    if (!readRaw(f, version) || !readRaw(f, count)) return reject();
    if (std::memcmp(magic, ENTITY_MAGIC, 4) != 0 || version != ENTITY_VERSION ||
        count > MAX_ENTITIES_PER_CHUNK) {
        return reject();
    }

    for (uint32_t i = 0; i < count; ++i) {
        uint8_t type = 0;
        uint32_t payloadSize = 0;
        if (!readRaw(f, type) || !readRaw(f, payloadSize)) return reject();
        if (payloadSize > MAX_ENTITY_PAYLOAD_BYTES) return reject();
        std::string payload(payloadSize, '\0');
        if (payloadSize > 0)
            f.read(&payload[0], (std::streamsize)payloadSize);
        if (!f) return reject();

        if (type == ENTITY_DROPPED_ITEM) {
            SavedDroppedItem item;
            if (payloadSize != DROPPED_ITEM_PAYLOAD_BYTES) return reject();
            if (readDroppedPayload(payload, expectedKey, item))
                out.push_back(item);
        }
    }

    char trailing = 0;
    if (f.read(&trailing, 1)) return reject();
    if (!f.eof()) return reject();
    return EntityChunkLoadStatus::Loaded;
}

bool deleteEntityChunkFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
}

bool saveEntityChunkFile(const std::string& path,
                         const std::vector<SavedDroppedItem>& items) {
    std::vector<SavedDroppedItem> filtered;
    filtered.reserve(items.size());
    for (const SavedDroppedItem& item : items) {
        if (item.stack.empty()) continue;
        if (!finiteVec(item.pos) || !finiteVec(item.vel)) continue;
        filtered.push_back(item);
    }
    if (filtered.empty()) return deleteEntityChunkFile(path);

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) return false;

    return atomicSave(path, [&](std::ofstream& f) {
        uint32_t count = uint32_t(filtered.size());
        f.write(ENTITY_MAGIC, 4);
        writeRaw(f, ENTITY_VERSION);
        writeRaw(f, count);
        for (const SavedDroppedItem& item : filtered) {
            std::string payload = droppedPayload(item);
            uint8_t type = ENTITY_DROPPED_ITEM;
            uint32_t payloadSize = uint32_t(payload.size());
            writeRaw(f, type);
            writeRaw(f, payloadSize);
            f.write(payload.data(), (std::streamsize)payload.size());
        }
    });
}
