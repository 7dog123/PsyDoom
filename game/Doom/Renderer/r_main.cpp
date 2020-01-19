#include "r_main.h"

#include "Doom/Base/i_drawcmds.h"
#include "Doom/Base/i_main.h"
#include "Doom/Game/doomdata.h"
#include "Doom/Game/g_game.h"
#include "Doom/Game/p_setup.h"
#include "PsxVm/PsxVm.h"
#include "PsyQ/LIBETC.h"
#include "PsyQ/LIBGPU.h"
#include "PsyQ/LIBGTE.h"
#include "r_bsp.h"
#include "r_data.h"
#include "r_draw.h"
#include "r_local.h"
#include "r_sky.h"
#include "r_things.h"

// View properties
const VmPtr<VmPtr<player_t>>    gpViewPlayer(0x80077F34);
const VmPtr<fixed_t>            gViewX(0x80077EE0);
const VmPtr<fixed_t>            gViewY(0x80077EE4);
const VmPtr<fixed_t>            gViewZ(0x80077EEC);
const VmPtr<angle_t>            gViewAngle(0x80078294);
const VmPtr<fixed_t>            gViewCos(0x8007809C);
const VmPtr<fixed_t>            gViewSin(0x800780B8);
const VmPtr<bool32_t>           gbIsSkyVisible(0x800781F4);
const VmPtr<MATRIX>             gDrawMatrix(0x80086530);

// Light properties
const VmPtr<bool32_t>               gbDoViewLighting(0x80078264);
const VmPtr<VmPtr<const light_t>>   gpCurLight(0x80078054);
const VmPtr<uint32_t>               gCurLightValR(0x80077E8C);
const VmPtr<uint32_t>               gCurLightValG(0x80078034);
const VmPtr<uint32_t>               gCurLightValB(0x80077F70);

// The list of subsectors to draw and current position in the list.
// The draw subsector count does not appear to be used for anything however... Maybe used in debug builds for stat tracking?
const VmPtr<VmPtr<subsector_t>[MAX_DRAW_SUBSECTORS]>    gpDrawSubsectors(0x800A91B4);
const VmPtr<VmPtr<VmPtr<subsector_t>>>                  gppEndDrawSubsector(0x80078064);
const VmPtr<int32_t>                                    gNumDrawSubsectors(0x800780EC);

// What sector is currently being drawn
const VmPtr<VmPtr<sector_t>>    gpCurDrawSector(0x8007800C);

//------------------------------------------------------------------------------------------------------------------------------------------
// One time setup for the 3D view renderer
//------------------------------------------------------------------------------------------------------------------------------------------
void R_Init() noexcept {
    // Initialize texture lists, palettes etc.
    R_InitData();

    // Initialize the transform matrix used for drawing and upload it to the GTE
    gDrawMatrix->t[0] = 0;
    gDrawMatrix->t[1] = 0;
    gDrawMatrix->t[2] = 0;
    LIBGTE_SetTransMatrix(*gDrawMatrix);

    gDrawMatrix->m[0][0] = 0;
    gDrawMatrix->m[0][1] = 0;
    gDrawMatrix->m[0][2] = 0;
    gDrawMatrix->m[1][0] = 0;
    gDrawMatrix->m[1][1] = GTE_ROTFRAC_UNIT;    // This part of the matrix never changes, so assign here
    gDrawMatrix->m[1][2] = 0;
    gDrawMatrix->m[2][0] = 0;
    gDrawMatrix->m[2][1] = 0;
    gDrawMatrix->m[2][2] = 0;
    LIBGTE_SetRotMatrix(*gDrawMatrix);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Render the 3D view and also player weapons
//------------------------------------------------------------------------------------------------------------------------------------------
void R_RenderPlayerView() noexcept {
    // If currently in fullbright mode (no lighting) then setup the light params now
    if (!*gbDoViewLighting) {
        *gCurLightValR = 128;
        *gCurLightValG = 128;
        *gCurLightValB = 128;        
        *gpCurLight = &(*gpLightsLump)[0];
    }

    // Store view parameters before drawing
    player_t& player = gPlayers[*gCurPlayerIndex];

    *gpViewPlayer = &player;
    *gViewX = player.mo->x & (~FRACMASK);
    *gViewY = player.mo->y & (~FRACMASK);
    *gViewZ = player.viewz & (~FRACMASK);
    *gViewAngle = player.mo->angle;
    *gViewCos = gFineCosine[*gViewAngle >> ANGLETOFINESHIFT];
    *gViewSin = gFineSine[*gViewAngle >> ANGLETOFINESHIFT];
    
    // Set the draw matrix and upload to the GTE
    gDrawMatrix->m[0][0] = (int16_t)( *gViewSin >> GTE_ROTFRAC_SHIFT);
    gDrawMatrix->m[0][2] = (int16_t)(-*gViewCos >> GTE_ROTFRAC_SHIFT);
    gDrawMatrix->m[2][0] = (int16_t)( *gViewCos >> GTE_ROTFRAC_SHIFT);
    gDrawMatrix->m[2][2] = (int16_t)( *gViewSin >> GTE_ROTFRAC_SHIFT);
    LIBGTE_SetRotMatrix(*gDrawMatrix);

    // Traverse the BSP tree to determine what needs to be drawn and in what order.
    R_BSP();
    
    // Stat tracking: how many subsectors will we draw?
    *gNumDrawSubsectors = (int32_t)(gppEndDrawSubsector->get() - gpDrawSubsectors.get());

    // Finish up the previous draw before we continue and draw the sky if currently visible
    I_DrawPresent();

    if (*gbIsSkyVisible) {
        R_DrawSky();
    }
    
    // Draw all subsectors emitted during BSP traversal.
    // Draw them in back to front order.
    while (*gppEndDrawSubsector > gpDrawSubsectors) {
        --*gppEndDrawSubsector;

        // Set the current draw sector
        subsector_t& subsec = ***gppEndDrawSubsector;
        sector_t& sec = *subsec.sector;
        *gpCurDrawSector = &sec;

        // Setup the lighting values to use for the sector
        if (*gbDoViewLighting) {
            // Compute basic light values
            const light_t& light = (*gpLightsLump)[sec.colorid];

            *gpCurLight = &light;
            *gCurLightValR = ((uint32_t) sec.lightlevel * (uint32_t) light.r) >> 8;
            *gCurLightValG = ((uint32_t) sec.lightlevel * (uint32_t) light.g) >> 8;
            *gCurLightValB = ((uint32_t) sec.lightlevel * (uint32_t) light.b) >> 8;

            // Contribute the player muzzle flash to the light and saturate
            if (player.extralight != 0) {
                *gCurLightValR += player.extralight;
                *gCurLightValG += player.extralight;
                *gCurLightValB += player.extralight;

                if (*gCurLightValR > 255) { *gCurLightValR = 255; }
                if (*gCurLightValG > 255) { *gCurLightValG = 255; }                
                if (*gCurLightValB > 255) { *gCurLightValB = 255; }
            }
        }
        
        R_DrawSubsector(subsec);
    }

    // Draw any player sprites
    R_DrawWeapon();

    // Clearing the texture window: this is probably not required?
    // Not sure what the reason is for this, but everything seems to work fine without doing this.
    // I'll leave the code as-is for now though until I have a better understanding of this, just in case something breaks.
    {
        DR_TWIN& setTexWinCmd = *(DR_TWIN*) getScratchAddr(128);
        RECT texWin = { 0, 0, 0, 0 };
        LIBGPU_SetTexWindow(setTexWinCmd, texWin);
        I_AddPrim(&setTexWinCmd);
    }
}

void R_SlopeDiv() noexcept {
    v0 = (a1 < 0x200);
    {
        const bool bJump = (v0 != 0);
        v0 = 0x800;                                     // Result = 00000800
        if (bJump) goto loc_80030B98;
    }
    v1 = a0 << 3;
    v0 = a1 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030B7C;
    _break(0x1C00);
loc_80030B7C:
    v1 = lo;
    v0 = (v1 < 0x801);
    {
        const bool bJump = (v0 != 0);
        v0 = v1;
        if (bJump) goto loc_80030B98;
    }
    v1 = 0x800;                                         // Result = 00000800
    v0 = v1;                                            // Result = 00000800
loc_80030B98:
    return;
}

void R_PointToAngle2() noexcept {
loc_80030BA0:
    a2 -= a0;
    a3 -= a1;
    if (a2 != 0) goto loc_80030BB4;
    v0 = 0;                                             // Result = 00000000
    if (a3 == 0) goto loc_80030EAC;
loc_80030BB4:
    if (i32(a2) < 0) goto loc_80030D30;
    v0 = (i32(a3) < i32(a2));
    if (i32(a3) < 0) goto loc_80030C68;
    {
        const bool bJump = (v0 == 0);
        v0 = (a2 < 0x200);
        if (bJump) goto loc_80030C24;
    }
    {
        const bool bJump = (v0 != 0);
        v0 = 0x800;                                     // Result = 00000800
        if (bJump) goto loc_80030C08;
    }
    v1 = a3 << 3;
    v0 = a2 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030BEC;
    _break(0x1C00);
loc_80030BEC:
    v1 = lo;
    v0 = (v1 < 0x801);
    {
        const bool bJump = (v0 != 0);
        v0 = v1;
        if (bJump) goto loc_80030C08;
    }
    v1 = 0x800;                                         // Result = 00000800
    v0 = v1;                                            // Result = 00000800
loc_80030C08:
    v0 <<= 2;
    at = 0x80070000;                                    // Result = 80070000
    at += 0x1958;                                       // Result = TanToAngle[0] (80071958)
    at += v0;
    v0 = lw(at);
    goto loc_80030EAC;
loc_80030C24:
    v0 = (a3 < 0x200);
    v1 = 0x800;                                         // Result = 00000800
    if (v0 != 0) goto loc_80030C60;
    v1 = a2 << 3;
    v0 = a3 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030C48;
    _break(0x1C00);
loc_80030C48:
    v1 = lo;
    v0 = (v1 < 0x801);
    {
        const bool bJump = (v0 != 0);
        v0 = 0x3FFF0000;                                // Result = 3FFF0000
        if (bJump) goto loc_80030E90;
    }
    v1 = 0x800;                                         // Result = 00000800
loc_80030C60:
    v0 = 0x3FFF0000;                                    // Result = 3FFF0000
    goto loc_80030E90;
loc_80030C68:
    a3 = -a3;
    v0 = (i32(a3) < i32(a2));
    {
        const bool bJump = (v0 == 0);
        v0 = (a2 < 0x200);
        if (bJump) goto loc_80030CD0;
    }
    {
        const bool bJump = (v0 != 0);
        v0 = 0x800;                                     // Result = 00000800
        if (bJump) goto loc_80030CB4;
    }
    v1 = a3 << 3;
    v0 = a2 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030C98;
    _break(0x1C00);
loc_80030C98:
    v1 = lo;
    v0 = (v1 < 0x801);
    if (v0 != 0) goto loc_80030CB0;
    v1 = 0x800;                                         // Result = 00000800
loc_80030CB0:
    v0 = v1;
loc_80030CB4:
    v0 <<= 2;
    at = 0x80070000;                                    // Result = 80070000
    at += 0x1958;                                       // Result = TanToAngle[0] (80071958)
    at += v0;
    v0 = lw(at);
    v0 = -v0;
    goto loc_80030EAC;
loc_80030CD0:
    v0 = (a3 < 0x200);
    {
        const bool bJump = (v0 != 0);
        v0 = 0x800;                                     // Result = 00000800
        if (bJump) goto loc_80030D10;
    }
    v1 = a2 << 3;
    v0 = a3 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030CF4;
    _break(0x1C00);
loc_80030CF4:
    v1 = lo;
    v0 = (v1 < 0x801);
    if (v0 != 0) goto loc_80030D0C;
    v1 = 0x800;                                         // Result = 00000800
loc_80030D0C:
    v0 = v1;
loc_80030D10:
    v0 <<= 2;
    at = 0x80070000;                                    // Result = 80070000
    at += 0x1958;                                       // Result = TanToAngle[0] (80071958)
    at += v0;
    v1 = lw(at);
    v0 = 0xC0000000;                                    // Result = C0000000
    v0 += v1;
    goto loc_80030EAC;
loc_80030D30:
    a2 = -a2;
    if (i32(a3) < 0) goto loc_80030DE4;
    v0 = (i32(a3) < i32(a2));
    {
        const bool bJump = (v0 == 0);
        v0 = (a2 < 0x200);
        if (bJump) goto loc_80030D84;
    }
    v1 = 0x800;                                         // Result = 00000800
    if (v0 != 0) goto loc_80030D7C;
    v1 = a3 << 3;
    v0 = a2 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030D64;
    _break(0x1C00);
loc_80030D64:
    v1 = lo;
    v0 = (v1 < 0x801);
    {
        const bool bJump = (v0 != 0);
        v0 = 0x7FFF0000;                                // Result = 7FFF0000
        if (bJump) goto loc_80030E90;
    }
    v1 = 0x800;                                         // Result = 00000800
loc_80030D7C:
    v0 = 0x7FFF0000;                                    // Result = 7FFF0000
    goto loc_80030E90;
loc_80030D84:
    v0 = (a3 < 0x200);
    {
        const bool bJump = (v0 != 0);
        v0 = 0x800;                                     // Result = 00000800
        if (bJump) goto loc_80030DC4;
    }
    v1 = a2 << 3;
    v0 = a3 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030DA8;
    _break(0x1C00);
loc_80030DA8:
    v1 = lo;
    v0 = (v1 < 0x801);
    if (v0 != 0) goto loc_80030DC0;
    v1 = 0x800;                                         // Result = 00000800
loc_80030DC0:
    v0 = v1;
loc_80030DC4:
    v0 <<= 2;
    at = 0x80070000;                                    // Result = 80070000
    at += 0x1958;                                       // Result = TanToAngle[0] (80071958)
    at += v0;
    v1 = lw(at);
    v0 = 0x40000000;                                    // Result = 40000000
    v0 += v1;
    goto loc_80030EAC;
loc_80030DE4:
    a3 = -a3;
    v0 = (i32(a3) < i32(a2));
    {
        const bool bJump = (v0 == 0);
        v0 = (a2 < 0x200);
        if (bJump) goto loc_80030E50;
    }
    {
        const bool bJump = (v0 != 0);
        v0 = 0x800;                                     // Result = 00000800
        if (bJump) goto loc_80030E30;
    }
    v1 = a3 << 3;
    v0 = a2 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030E14;
    _break(0x1C00);
loc_80030E14:
    v1 = lo;
    v0 = (v1 < 0x801);
    if (v0 != 0) goto loc_80030E2C;
    v1 = 0x800;                                         // Result = 00000800
loc_80030E2C:
    v0 = v1;
loc_80030E30:
    v0 <<= 2;
    at = 0x80070000;                                    // Result = 80070000
    at += 0x1958;                                       // Result = TanToAngle[0] (80071958)
    at += v0;
    v1 = lw(at);
    v0 = 0x80000000;                                    // Result = 80000000
    v0 = v1 - v0;
    goto loc_80030EAC;
loc_80030E50:
    v0 = (a3 < 0x200);
    v1 = 0x800;                                         // Result = 00000800
    if (v0 != 0) goto loc_80030E8C;
    v1 = a2 << 3;
    v0 = a3 >> 8;
    divu(v1, v0);
    if (v0 != 0) goto loc_80030E74;
    _break(0x1C00);
loc_80030E74:
    v1 = lo;
    v0 = (v1 < 0x801);
    {
        const bool bJump = (v0 != 0);
        v0 = 0xBFFF0000;                                // Result = BFFF0000
        if (bJump) goto loc_80030E90;
    }
    v1 = 0x800;                                         // Result = 00000800
loc_80030E8C:
    v0 = 0xBFFF0000;                                    // Result = BFFF0000
loc_80030E90:
    v1 <<= 2;
    at = 0x80070000;                                    // Result = 80070000
    at += 0x1958;                                       // Result = TanToAngle[0] (80071958)
    at += v1;
    v1 = lw(at);
    v0 |= 0xFFFF;
    v0 -= v1;
loc_80030EAC:
    return;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Tells what side of a line a point is on, with accuracy in terms of integer units.
// Returns '0' if the point is on the 'front' side of the line, otherwise '1' if on the back side.
//------------------------------------------------------------------------------------------------------------------------------------------
int32_t R_PointOnSide(const fixed_t x, const fixed_t y, const node_t& node) noexcept {
    // Special case shortcut for vertical lines
    if (node.dx == 0) {
        if (x <= node.x) {
            return (node.dy > 0);
        } else {
            return (node.dy < 0);
        }
    }

    // Special case shortcut for horizontal lines
    if (node.dy == 0) {
        if (y <= node.y) {
            return (node.dx < 0);
        } else {
            return (node.dx > 0);
        }
    }

    // Compute which side of the line the point is on using the cross product
    const fixed_t dx = x - node.x;
    const fixed_t dy = y - node.y;
    const int32_t lprod = (node.dy >> FRACBITS) * (dx >> FRACBITS);
    const int32_t rprod = (node.dx >> FRACBITS) * (dy >> FRACBITS);
    return (rprod >= lprod);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Returns the subsector that a 2D point is in.
// Note: there should always be a subsector returned for a valid DOOM level.
//------------------------------------------------------------------------------------------------------------------------------------------
subsector_t* R_PointInSubsector(const fixed_t x, const fixed_t y) noexcept {
    // Not sure why there would ever be '0' BSP nodes - that does not seem like a valid DOOM level to me?
    // The same logic can also be found in other versions of DOOM...
    if (*gNumBspNodes == 0) {
        return gpSubsectors->get();
    }
    
    // Traverse the BSP tree starting at the root node, using the given position to decide which half-spaces to visit.
    // Once we reach a subsector stop and return it.
    int32_t nodeNum = *gNumBspNodes - 1;
    
    while ((nodeNum & NF_SUBSECTOR) == 0) {
        node_t& node = (*gpBspNodes)[nodeNum];
        const int32_t side = R_PointOnSide(x, y, node);
        nodeNum = node.children[side];
    }

    const int32_t actualNodeNum = nodeNum & (~NF_SUBSECTOR);
    return &(*gpSubsectors)[actualNodeNum];
}

void _thunk_R_PointInSubsector() noexcept {
    v0 = ptrToVmAddr(R_PointInSubsector(a0, a1));
}
