#include "sim/EntitySave.h"

#include "platform/SaveIO.h"
#include "sim/ItemSave.h"
#include "sim/Mob.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
constexpr char ENTITY_MAGIC[4] = {'M', 'C', 'E', 'N'};
constexpr uint32_t ENTITY_VERSION = 1;
// Record type tags are append-only, like block and item ids. The loader
// skips unknown tags, so new entity kinds stay readable by older data.
constexpr uint8_t ENTITY_DROPPED_ITEM = 1;
constexpr uint8_t ENTITY_LIVING = 2; // legacy: read-only, u8 = ambient flag
constexpr uint8_t ENTITY_AMBIENT_SPAWN_MARKER = 3;
// Living v2: the type-2 layout with the u8 reinterpreted as SpawnReason
// (Staged/Ambient values match the old flag) plus a trailing MobKind byte
// after the model id. The writer always emits v2; type 2 stays loadable.
constexpr uint8_t ENTITY_LIVING_V2 = 4;
constexpr uint32_t MAX_ENTITIES_PER_CHUNK = 4096;
constexpr uint32_t MAX_ENTITY_PAYLOAD_BYTES = 65536;
constexpr uint32_t DROPPED_ITEM_PAYLOAD_BYTES = 37;
constexpr uint32_t MAX_MODEL_ID_BYTES = 128;
constexpr int32_t MAX_SAVED_HEALTH = 1000;

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

std::string livingPayload(const SavedLivingEntity& e) {
    std::ostringstream out(std::ios::binary);
    writeRaw(out, e.pos.x);
    writeRaw(out, e.pos.y);
    writeRaw(out, e.pos.z);
    writeRaw(out, e.vel.x);
    writeRaw(out, e.vel.y);
    writeRaw(out, e.vel.z);
    writeRaw(out, e.health);
    writeRaw(out, e.ageTicks);
    writeRaw(out, e.movePhase);
    writeRaw(out, e.facingYaw);
    writeRaw(out, e.reason);
    writeRaw(out, int32_t(e.homeChunk.x));
    writeRaw(out, int32_t(e.homeChunk.z));
    writeRaw(out, uint16_t(e.modelId.size()));
    out.write(e.modelId.data(), (std::streamsize)e.modelId.size());
    writeRaw(out, e.kind);
    return out.str();
}

bool readLivingPayload(const std::string& payload, ChunkKey expectedKey,
                       bool v2, SavedLivingEntity& out) {
    std::istringstream in(payload, std::ios::binary);
    SavedLivingEntity e;
    uint8_t reason = 0;
    int32_t homeX = 0, homeZ = 0;
    uint16_t modelLen = 0;
    if (!readRaw(in, e.pos.x) || !readRaw(in, e.pos.y) || !readRaw(in, e.pos.z) ||
        !readRaw(in, e.vel.x) || !readRaw(in, e.vel.y) || !readRaw(in, e.vel.z) ||
        !readRaw(in, e.health) || !readRaw(in, e.ageTicks) ||
        !readRaw(in, e.movePhase) || !readRaw(in, e.facingYaw) ||
        !readRaw(in, reason) || !readRaw(in, homeX) || !readRaw(in, homeZ) ||
        !readRaw(in, modelLen)) {
        return false;
    }
    if (modelLen == 0 || modelLen > MAX_MODEL_ID_BYTES) return false;
    std::string modelId(modelLen, '\0');
    in.read(&modelId[0], modelLen);
    if (!in) return false;
    uint8_t kind = 0;
    if (v2 && !readRaw(in, kind)) return false;
    char trailing = 0;
    if (in.read(&trailing, 1)) return false; // payload size must match exactly
    if (!finiteVec(e.pos) || !finiteVec(e.vel) || !std::isfinite(e.facingYaw))
        return false;
    if (e.health <= 0 || e.health > MAX_SAVED_HEALTH) return false;
    if (!belongsToChunk(e.pos, expectedKey)) return false;
    // The legacy u8 was an ambient flag; 0/1 map exactly onto Staged/Ambient.
    if (!isValidSpawnReason(reason) || (!v2 && reason > 1)) return false;
    if (!isValidMobKind(kind)) return false;
    e.reason = reason;
    e.kind = kind;
    e.homeChunk = ChunkKey{homeX, homeZ};
    e.modelId = std::move(modelId);
    out = std::move(e);
    return true;
}
}

std::string entityChunkPath(const std::string& saveDir, ChunkKey key) {
    return saveDir + "/entities/e_" + std::to_string(key.x) + "_" +
           std::to_string(key.z) + ".bin";
}

EntityChunkLoadStatus loadEntityChunkFile(const std::string& path,
                                          ChunkKey expectedKey,
                                          SavedEntityChunk& out) {
    out = SavedEntityChunk{};
    std::ifstream f(path, std::ios::binary);
    if (!f) return EntityChunkLoadStatus::Missing;

    auto reject = [&]() {
        out = SavedEntityChunk{};
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
                out.items.push_back(item);
        } else if (type == ENTITY_LIVING || type == ENTITY_LIVING_V2) {
            SavedLivingEntity living;
            if (readLivingPayload(payload, expectedKey, type == ENTITY_LIVING_V2,
                                  living))
                out.living.push_back(std::move(living));
        } else if (type == ENTITY_AMBIENT_SPAWN_MARKER) {
            if (payloadSize != 0) return reject();
            out.ambientSpawnConsumed = true;
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
                         const SavedEntityChunk& chunk) {
    SavedEntityChunk filtered;
    filtered.ambientSpawnConsumed = chunk.ambientSpawnConsumed;
    filtered.items.reserve(chunk.items.size());
    for (const SavedDroppedItem& item : chunk.items) {
        if (item.stack.empty()) continue;
        if (!finiteVec(item.pos) || !finiteVec(item.vel)) continue;
        filtered.items.push_back(item);
    }
    filtered.living.reserve(chunk.living.size());
    for (const SavedLivingEntity& e : chunk.living) {
        if (e.modelId.empty() || e.modelId.size() > MAX_MODEL_ID_BYTES) continue;
        if (e.health <= 0 || e.health > MAX_SAVED_HEALTH) continue;
        if (!finiteVec(e.pos) || !finiteVec(e.vel)) continue;
        if (!isValidMobKind(e.kind) || !isValidSpawnReason(e.reason)) continue;
        filtered.living.push_back(e);
    }
    if (filtered.empty()) return deleteEntityChunkFile(path);
    size_t records = filtered.items.size() + filtered.living.size() +
                     (filtered.ambientSpawnConsumed ? 1 : 0);
    if (records > MAX_ENTITIES_PER_CHUNK) return false;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) return false;

    return atomicSave(path, [&](std::ofstream& f) {
        f.write(ENTITY_MAGIC, 4);
        writeRaw(f, ENTITY_VERSION);
        writeRaw(f, uint32_t(records));
        auto writeRecord = [&](uint8_t type, const std::string& payload) {
            writeRaw(f, type);
            writeRaw(f, uint32_t(payload.size()));
            f.write(payload.data(), (std::streamsize)payload.size());
        };
        for (const SavedDroppedItem& item : filtered.items)
            writeRecord(ENTITY_DROPPED_ITEM, droppedPayload(item));
        for (const SavedLivingEntity& e : filtered.living)
            writeRecord(ENTITY_LIVING_V2, livingPayload(e));
        if (filtered.ambientSpawnConsumed)
            writeRecord(ENTITY_AMBIENT_SPAWN_MARKER, std::string());
    });
}
