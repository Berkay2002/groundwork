#pragma once

#include "sim/Item.h"

// Builds the procedural block texture atlas and returns the GL texture id.
// Atlas layout: horizontal strip of 16x16 tiles (see Block.h for indices).
// Used by the HUD (hotbar icons); chunks use the texture array below.
unsigned createBlockAtlas();

// Same tiles as a GL_TEXTURE_2D_ARRAY (tile index = layer) with REPEAT
// wrapping, so greedy-merged chunk faces can tile their texture.
unsigned createBlockTextureArray();

// Procedural RGBA crack stages for the mining overlay. Alpha carries the
// cracks; RGB is unused by the shader.
unsigned createBreakTextureArray();

inline bool itemUsesBlockCube(ItemId item) {
    return placeBlockForItem(item) != Block::Air;
}

inline TileId itemIconTile(ItemId item) {
    switch (item) {
        case ItemId::Stick: return TileId::ItemStick;
        case ItemId::Coal: return TileId::ItemCoal;
        case ItemId::RawIron: return TileId::ItemRawIron;
        case ItemId::IronIngot: return TileId::ItemIronIngot;
        case ItemId::Diamond: return TileId::ItemDiamond;
        case ItemId::WoodPickaxe: return TileId::ItemWoodPickaxe;
        case ItemId::WoodAxe: return TileId::ItemWoodAxe;
        case ItemId::WoodShovel: return TileId::ItemWoodShovel;
        case ItemId::StonePickaxe: return TileId::ItemStonePickaxe;
        case ItemId::StoneAxe: return TileId::ItemStoneAxe;
        case ItemId::StoneShovel: return TileId::ItemStoneShovel;
        case ItemId::IronPickaxe: return TileId::ItemIronPickaxe;
        case ItemId::IronAxe: return TileId::ItemIronAxe;
        case ItemId::IronShovel: return TileId::ItemIronShovel;
        case ItemId::DiamondPickaxe: return TileId::ItemDiamondPickaxe;
        case ItemId::DiamondAxe: return TileId::ItemDiamondAxe;
        case ItemId::DiamondShovel: return TileId::ItemDiamondShovel;
        default: return TileId::Error;
    }
}
