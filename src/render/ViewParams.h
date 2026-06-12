#pragma once

#include <algorithm>

#include "platform/Settings.h"
#include "world/Block.h"

inline float projectionFarPlaneForRenderDistance(int renderDistance) {
    float fogEnd = float(clampRenderDistance(renderDistance) * CHUNK_SIZE);
    return std::max(600.0f, fogEnd * 1.05f + float(CHUNK_SIZE));
}
