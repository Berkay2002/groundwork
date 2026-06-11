#include "sim/Mining.h"

#include <algorithm>
#include <cmath>

namespace mining {

MiningTool miningToolForStack(const ItemStack& stack) {
    if (stack.empty() || !isValidItemId(stack.item)) return {};
    const ItemDef& d = itemDef(stack.item);
    MiningTool tool;
    tool.toolClass = d.toolClass;
    tool.tier = d.toolTier;
    tool.speed = d.miningSpeed > 0.0f ? d.miningSpeed : 1.0f;
    tool.hasDurability = d.maxDurability > 0;
    if (tool.toolClass == ToolClass::None) {
        tool.tier = ToolTier::Hand;
        tool.speed = 1.0f;
        tool.hasDurability = false;
    }
    return tool;
}

static bool correctTool(Block block, MiningTool tool) {
    const BlockDef& d = blockDef(block);
    if (d.preferredTool == ToolClass::None) return false;
    return tool.toolClass == d.preferredTool;
}

bool canHarvestUsefulDrop(Block block, MiningTool tool) {
    const BlockDef& d = blockDef(block);
    if (d.hardness == UNBREAKABLE) return false;
    if (d.minHarvestTier == ToolTier::Hand) return true;
    return correctTool(block, tool) && tierLevel(tool.tier) >= tierLevel(d.minHarvestTier);
}

int requiredBreakTicks(Block block, MiningTool tool) {
    const BlockDef& d = blockDef(block);
    if (!isBreakable(block)) return NEVER_BREAKS;
    if (d.hardness <= 0.0f) return INSTANT_BREAK;

    if (correctTool(block, tool) && canHarvestUsefulDrop(block, tool)) {
        return std::max(1, int(std::ceil(d.hardness * 30.0f / tool.speed)));
    }
    if (canHarvestUsefulDrop(block, tool)) {
        return std::max(1, int(std::ceil(d.hardness * 30.0f)));
    }
    return std::max(1, int(std::ceil(d.hardness * 100.0f)));
}

ItemStack miningDrop(Block block, MiningTool tool) {
    const BlockDef& d = blockDef(block);
    ItemId item = canHarvestUsefulDrop(block, tool) ? d.dropItem : d.wrongToolDropItem;
    uint8_t count = canHarvestUsefulDrop(block, tool) ? d.dropCount : d.wrongToolDropCount;
    return makeItemStack(item, count);
}

bool applyDurabilityUse(ItemStack& stack, DurabilityUseReason) {
    if (stack.empty() || !isValidItemId(stack.item)) return true;
    const ItemDef& d = itemDef(stack.item);
    if (d.maxDurability == 0) return true;
    if (stack.durability <= 1) {
        stack = {};
        return false;
    }
    --stack.durability;
    return true;
}

bool shouldUseDurabilityForBreak(Block block, MiningTool tool) {
    return tool.hasDurability && requiredBreakTicks(block, tool) > INSTANT_BREAK;
}

static void reset(BreakProgressState& state) {
    state = {};
}

BreakProgressEvent advanceBreakProgress(BreakProgressState& state,
                                         bool held,
                                         bool targetValid,
                                         const glm::ivec3& target,
                                         Block block,
                                         const ItemStack& heldStack) {
    BreakProgressEvent ev;
    if (!held || !targetValid || !isBreakable(block)) {
        if (state.active || state.ticks != 0) ev.reset = true;
        reset(state);
        return ev;
    }

    MiningTool tool = miningToolForStack(heldStack);
    int required = requiredBreakTicks(block, tool);
    if (required == NEVER_BREAKS) {
        if (state.active || state.ticks != 0) ev.reset = true;
        reset(state);
        return ev;
    }

    bool changed = !state.active || state.target != target || state.block != block ||
                   state.requiredTicks != required || state.heldItem != heldStack.item ||
                   state.heldDurability != heldStack.durability;
    if (changed) {
        ev.reset = state.active || state.ticks != 0;
        state.active = true;
        state.target = target;
        state.block = block;
        state.heldItem = heldStack.item;
        state.heldDurability = heldStack.durability;
        state.ticks = 0;
        state.requiredTicks = required;
    }

    if (required == INSTANT_BREAK) {
        ev.removed = true;
        ev.useDurability = false;
        ev.progress = 1.0f;
        reset(state);
        return ev;
    }

    ++state.ticks;
    ev.progress = std::min(1.0f, float(state.ticks) / float(required));
    if (state.ticks >= required) {
        ev.removed = true;
        ev.useDurability = shouldUseDurabilityForBreak(block, tool);
        reset(state);
    }
    return ev;
}

}
