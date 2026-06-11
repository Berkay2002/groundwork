#pragma once

#include "sim/Item.h"
#include "world/Block.h"
#include <glm/glm.hpp>

namespace mining {

constexpr int NEVER_BREAKS = -1;
constexpr int INSTANT_BREAK = 0;

enum class DurabilityUseReason { Mining };

struct MiningTool {
    ToolClass toolClass = ToolClass::None;
    ToolTier tier = ToolTier::Hand;
    float speed = 1.0f;
    bool hasDurability = false;
};

struct BreakProgressState {
    bool active = false;
    glm::ivec3 target{0};
    Block block = Block::Air;
    ItemId heldItem = ItemId::None;
    uint16_t heldDurability = 0;
    int ticks = 0;
    int requiredTicks = NEVER_BREAKS;
};

struct BreakProgressEvent {
    bool removed = false;
    bool reset = false;
    bool useDurability = false;
    float progress = 0.0f;
};

MiningTool miningToolForStack(const ItemStack& stack);
bool canHarvestUsefulDrop(Block block, MiningTool tool);
int requiredBreakTicks(Block block, MiningTool tool);
ItemStack miningDrop(Block block, MiningTool tool);
bool applyDurabilityUse(ItemStack& stack, DurabilityUseReason reason);
bool shouldUseDurabilityForBreak(Block block, MiningTool tool);

BreakProgressEvent advanceBreakProgress(BreakProgressState& state,
                                         bool held,
                                         bool targetValid,
                                         const glm::ivec3& target,
                                         Block block,
                                         const ItemStack& heldStack);

}
