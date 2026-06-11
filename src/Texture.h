#pragma once

// Builds the procedural block texture atlas and returns the GL texture id.
// Atlas layout: horizontal strip of 16x16 tiles (see Block.h for indices).
// Used by the HUD (hotbar icons); chunks use the texture array below.
unsigned createBlockAtlas();

// Same tiles as a GL_TEXTURE_2D_ARRAY (tile index = layer) with REPEAT
// wrapping, so greedy-merged chunk faces can tile their texture.
unsigned createBlockTextureArray();
