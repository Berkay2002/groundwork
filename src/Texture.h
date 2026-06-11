#pragma once

// Builds the procedural block texture atlas and returns the GL texture id.
// Atlas layout: horizontal strip of 16x16 tiles (see Block.h for indices).
unsigned createBlockAtlas();
