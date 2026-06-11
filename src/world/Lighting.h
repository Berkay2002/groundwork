#pragma once
#include "world/Block.h"
#include "world/Chunk.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace lighting {

enum class Channel { Sun, Block };

class ReadAccess {
public:
    virtual ~ReadAccess() = default;
    virtual bool hasChunkAt(int wx, int wz) const = 0;
    virtual Block blockAt(int wx, int wy, int wz) const = 0;
    virtual uint8_t cellLightAt(Channel ch, int wx, int wy, int wz) const = 0;
};

class Accessor : public ReadAccess {
public:
    virtual void setCellLight(Channel ch, int wx, int wy, int wz, uint8_t v) = 0;
};

uint8_t lightAt(const ReadAccess& a, Channel ch, int wx, int wy, int wz);
void onBlockChanged(Accessor& a, Block oldBlock, Block newBlock, const glm::ivec3& pos);
void onChunkAdded(Accessor& a, int cx, int cz);

// Per-chunk light bootstrap used by generation workers on fresh chunks.
// Cross-chunk propagation stays on the main thread through onChunkAdded().
void computeInitialLight(Chunk& c);

}
