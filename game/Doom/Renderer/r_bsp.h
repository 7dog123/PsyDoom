#pragma once

#include "Doom/doomdef.h"

struct seg_t;

void R_BSP() noexcept;
void R_RenderBSPNode(const int32_t bspnum) noexcept;
bool R_CheckBBox(const fixed_t bspcoord[4]) noexcept;
void R_Subsector(const int32_t subsecNum) noexcept;
void R_AddLine(seg_t& seg) noexcept;
