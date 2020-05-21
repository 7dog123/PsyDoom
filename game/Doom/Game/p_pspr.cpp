#include "p_pspr.h"

#include "Doom/Base/i_main.h"
#include "Doom/Base/m_random.h"
#include "Doom/Base/s_sound.h"
#include "Doom/Base/sounds.h"
#include "Doom/Base/w_wad.h"
#include "Doom/d_main.h"
#include "Doom/doomdef.h"
#include "Doom/Renderer/r_local.h"
#include "Doom/Renderer/r_main.h"
#include "doomdata.h"
#include "info.h"
#include "p_inter.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "PsxVm/PsxVm.h"

const weaponinfo_t gWeaponInfo[NUMWEAPONS] = {
    {   // Fist
        am_noammo,              // ammo
        S_PUNCHUP,              // upstate
        S_PUNCHDOWN,            // downstate
        S_PUNCH,                // readystate
        S_PUNCH1,               // atkstate
        S_NULL                  // flashstate
    },
    {   // Pistol
        am_clip,                // ammo
        S_PISTOLUP,             // upstate
        S_PISTOLDOWN,           // downstate
        S_PISTOL,               // readystate
        S_PISTOL2,              // atkstate
        S_PISTOLFLASH           // flashstate
    },
    {   // Shotgun
        am_shell,               // ammo
        S_SGUNUP,               // upstate
        S_SGUNDOWN,             // downstate
        S_SGUN,                 // readystate
        S_SGUN2,                // atkstate
        S_SGUNFLASH1            // flashstate
    },
    {   // Super Shotgun
        am_shell,               // ammo
        S_DSGUNUP,              // upstate
        S_DSGUNDOWN,            // downstate
        S_DSGUN,                // readystate
        S_DSGUN1,               // atkstate
        S_DSGUNFLASH1           // flashstate
    },
    {   // Chaingun
        am_clip,                // ammo
        S_CHAINUP,              // upstate
        S_CHAINDOWN,            // downstate
        S_CHAIN,                // readystate
        S_CHAIN1,               // atkstate
        S_CHAINFLASH1           // flashstate
    },
    {   // Rocket Launcher
        am_misl,                // ammo
        S_MISSILEUP,            // upstate
        S_MISSILEDOWN,          // downstate
        S_MISSILE,              // readystate
        S_MISSILE1,             // atkstate
        S_MISSILEFLASH1         // flashstate
    },
    {   // Plasma Rifle
        am_cell,                // ammo
        S_PLASMAUP,             // upstate
        S_PLASMADOWN,           // downstate
        S_PLASMA,               // readystate
        S_PLASMA1,              // atkstate
        S_PLASMAFLASH1          // flashstate
    },
    {   // BFG
        am_cell,                // ammo
        S_BFGUP,                // upstate
        S_BFGDOWN,              // downstate
        S_BFG,                  // readystate
        S_BFG1,                 // atkstate
        S_BFGFLASH1             // flashstate
    },
    {   // Chainsaw
        am_noammo,              // ammo
        S_SAWUP,                // upstate
        S_SAWDOWN,              // downstate
        S_SAW,                  // readystate
        S_SAW1,                 // atkstate
        S_NULL                  // flashstate
    }
};

static constexpr int32_t BFGCELLS       = 40;               // Number of cells in a BFG shot
static constexpr int32_t WEAPONX        = 1 * FRACUNIT;     // TODO: COMMENT
static constexpr int32_t WEAPONBOTTOM   = 96 * FRACUNIT;    // TODO: COMMENT
static constexpr int32_t WEAPONTOP      = 0 * FRACUNIT;     // TODO: COMMENT

// The current thing making noise
static const VmPtr<VmPtr<mobj_t>>   gpSoundTarget(0x80077FFC);

//------------------------------------------------------------------------------------------------------------------------------------------
// Recursively flood fill sound starting from the given sector to other neighboring sectors considered valid for sound transfer.
// Aside from closed doors, walls etc. stopping sound propagation sound will also be stopped after two sets of 'ML_SOUNDBLOCK' 
// lines are encountered.
//------------------------------------------------------------------------------------------------------------------------------------------
static void P_RecursiveSound(sector_t& sector, const bool bStopOnSoundBlock) noexcept {
    // Don't flood the sector if it's already done and it didn't have sound coming into it blocked
    const int32_t soundTraversed = (bStopOnSoundBlock) ? 2 : 1;

    if ((sector.validcount == *gValidCount) && (sector.soundtraversed <= soundTraversed))
        return;
    
    // Flood fill this sector and save the thing that made noise and whether sound was blocked
    sector.validcount = *gValidCount;
    sector.soundtraversed = soundTraversed;
    sector.soundtarget = *gpSoundTarget;

    // Recurse into adjoining sectors and flood fill with noise
    for (int32_t lineIdx = 0; lineIdx < sector.linecount; ++lineIdx) {
        line_t& line = *sector.lines[lineIdx];
        sector_t* const pBackSector = line.backsector.get();

        // Sound can't pass single sided lines
        if (!pBackSector)
            continue;
        
        sector_t& frontSector = *line.frontsector.get();

        // If the sector is a closed door then sound can't pass through it
        if (frontSector.floorheight >= pBackSector->ceilingheight)
            continue;

        if (frontSector.ceilingheight <= pBackSector->floorheight)
            continue;

        // Need to recurse into the sector on the opposite side of this sector's line
        sector_t& checkSector = (&frontSector == &sector) ? *pBackSector : frontSector;
        
        if (line.flags & ML_SOUNDBLOCK) {
            if (!bStopOnSoundBlock) {
                P_RecursiveSound(checkSector, true);
            }
        }
        else {
            P_RecursiveSound(checkSector, bStopOnSoundBlock);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Called after using weapons: make noise to alert sleeping monsters of the given player
//------------------------------------------------------------------------------------------------------------------------------------------
static void P_NoiseAlert(player_t& player) noexcept {
    // Optimization: don't bother doing a noise alert again if we are still in the same sector as the last one
    mobj_t& playerMobj = *player.mo;
    sector_t& curSector = *playerMobj.subsector->sector.get();

    if (player.lastsoundsector.get() == &curSector) 
        return;

    player.lastsoundsector = &curSector;
    *gValidCount += 1;
    *gpSoundTarget = &playerMobj;

    // Recursively flood fill sectors with sound
    P_RecursiveSound(curSector, false);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Set the specified player sprite to the given state and invoke the state action.
// Note that if the state is over in an instant, multiple states might be transitioned to.
//------------------------------------------------------------------------------------------------------------------------------------------
void P_SetPsprite(player_t& player, const int32_t spriteIdx, const statenum_t stateNum) noexcept {
    pspdef_t& sprite = player.psprites[spriteIdx];
    statenum_t nextStateNum = stateNum;

    do {
        // Did the object remove itself?
        if (nextStateNum == S_NULL) {
            sprite.state = nullptr;
            return;
        }

        // Advance to the next state
        state_t& state = gStates[nextStateNum];
        sprite.state = &state;
        sprite.tics = state.tics;

        // Perform the state action
        if (state.action) {
            // FIXME: convert to native function call
            a0 = ptrToVmAddr(&player);
            a1 = ptrToVmAddr(&sprite);
            void (* const pActionFunc)() = PsxVm::getVmFuncForAddr(state.action);
            pActionFunc();

            // Finish if we no longer have a state
            if (!sprite.state)
                break;
        }
        
        // Execute the next state if the tics left is zero (state is an instant cycle through)
        nextStateNum = sprite.state->nextstate;

    }  while (sprite.tics == 0);
}

void P_BringUpWeapon() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    sw(ra, sp + 0x18);
    sw(s0, sp + 0x10);
    v1 = lw(s1 + 0x70);
    v0 = 0xA;                                           // Result = 0000000A
    {
        const bool bJump = (v1 != v0);
        v0 = 8;                                         // Result = 00000008
        if (bJump) goto loc_8001FC50;
    }
    v0 = lw(s1 + 0x6C);
    sw(v0, s1 + 0x70);
    v1 = lw(s1 + 0x70);
    v0 = 8;                                             // Result = 00000008
loc_8001FC50:
    s0 = s1 + 0xF0;
    if (v1 != v0) goto loc_8001FC78;
    v0 = *gbIsLevelDataCached;
    if (v0 == 0) goto loc_8001FC78;
    a0 = lw(s1);
    a1 = sfx_sawup;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
loc_8001FC78:
    v1 = lw(s1 + 0x70);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F8;                                       // Result = WeaponInfo_Fist[1] (800670F8)
    at += v0;
    v1 = lw(at);
    v0 = 0xA;                                           // Result = 0000000A
    sw(v0, s1 + 0x70);
    v0 = 0x10000;                                       // Result = 00010000
    sw(v0, s1 + 0xF8);
    v0 = 0x600000;                                      // Result = 00600000
    a0 = v1;
    sw(v0, s1 + 0xFC);
    if (a0 != 0) goto loc_8001FCC4;
    sw(0, s1 + 0xF0);
    goto loc_8001FD34;
loc_8001FCC4:
    v0 = a0 << 3;
loc_8001FCC8:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_8001FD14;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_8001FD34;
loc_8001FD14:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_8001FD34;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_8001FCC8;
    sw(0, s0);
loc_8001FD34:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Check if the player has enough ammo to fire the current weapon and return 'true' if that is case.
// If there is not enough ammo then this function also attempts to switch to an appropriate weapon with ammo.
//------------------------------------------------------------------------------------------------------------------------------------------
static bool P_CheckAmmo(player_t& player) noexcept {
    // Get how much ammo the shot takes
    int32_t ammoForShot;

    if (player.readyweapon == wp_bfg) {
        ammoForShot = BFGCELLS;
    } else if (player.readyweapon == wp_supershotgun) {
        ammoForShot = 2;    // Double barrel shotgun
    } else {
        ammoForShot = 1;
    }

    // Can shoot if the weapon has no ammo or if we have enough ammo for the shot
    const ammotype_t ammoType = gWeaponInfo[player.readyweapon].ammo;

    if ((ammoType == am_noammo) || (player.ammo[ammoType] >= ammoForShot))
        return true;

    // Not enough ammo: figure out what weapon to switch to next
    if (player.weaponowned[wp_plasma] && (player.ammo[am_cell] != 0)) {
        player.pendingweapon = wp_plasma;
    }
    else if (player.weaponowned[wp_supershotgun] && (player.ammo[am_shell] > 2)) {  // Bug? Won't switch when ammo is '2', even though a shot can be taken...
        player.pendingweapon = wp_supershotgun;
    }
    else if (player.weaponowned[wp_chaingun] && (player.ammo[am_clip] != 0)) {
        player.pendingweapon = wp_chaingun;
    }
    else if (player.weaponowned[wp_shotgun] && (player.ammo[am_shell] != 0)) {
        player.pendingweapon = wp_shotgun;
    }
    else if (player.ammo[am_clip] != 0) {
        player.pendingweapon = wp_pistol;
    }
    else if (player.weaponowned[wp_chainsaw]) {
        player.pendingweapon = wp_chainsaw;
    } 
    else if (player.weaponowned[wp_missile] && (player.ammo[am_misl] != 0)) {
        player.pendingweapon = wp_missile;
    }
    else if (player.weaponowned[wp_bfg] && (player.ammo[am_cell] > BFGCELLS)) {     // Bug? Won't switch when ammo is 'BFGCELLS', even though a shot can be taken...
        player.pendingweapon = wp_bfg;
    }
    else {
        player.pendingweapon = wp_fist;
    }
    
    // Start lowering the current weapon
    P_SetPsprite(player, ps_weapon, gWeaponInfo[player.readyweapon].downstate);
    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Fires the player's weapon
//------------------------------------------------------------------------------------------------------------------------------------------
void P_FireWeapon(player_t& player) noexcept {
    // If there is not enough ammo then you can't fire
    if (!P_CheckAmmo(player))
        return;
    
    // Player is now in the attacking state
    P_SetMObjState(*player.mo, S_PLAY_ATK1);

    // Switch the player sprite into the attacking state and ensure the weapon sprite offset is correct
    pspdef_t& weaponSprite = player.psprites[ps_weapon];
    weaponSprite.sx = WEAPONX;
    weaponSprite.sy = WEAPONTOP;

    P_SetPsprite(player, ps_weapon, gWeaponInfo[player.readyweapon].atkstate);

    // Alert monsters to the noise
    P_NoiseAlert(player);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Drops down the player's currently equipped weapon: used when the player dies
//------------------------------------------------------------------------------------------------------------------------------------------
void P_DropWeapon(player_t& player) noexcept {
    P_SetPsprite(player, ps_weapon, gWeaponInfo[player.readyweapon].downstate);
}

void A_WeaponReady() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    sw(s0, sp + 0x10);
    sw(ra, sp + 0x18);
    v1 = lw(s1 + 0x6C);
    v0 = 8;                                             // Result = 00000008
    s0 = a1;
    if (v1 != v0) goto loc_800202DC;
    v1 = lw(s0);
    v0 = 0x80060000;                                    // Result = 80060000
    v0 -= 0x6B20;                                       // Result = State_S_SAW[0] (800594E0)
    if (v1 != v0) goto loc_800202DC;
    a0 = lw(s1);
    a1 = sfx_sawidl;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
loc_800202DC:
    v1 = lw(s1 + 0x70);
    v0 = 0xA;                                           // Result = 0000000A
    if (v1 != v0) goto loc_800202FC;
    v0 = lw(s1 + 0x24);
    if (v0 != 0) goto loc_800203A8;
loc_800202FC:
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70FC;                                       // Result = WeaponInfo_Fist[2] (800670FC)
    at += v0;
    a0 = lw(at);
    s0 = s1 + 0xF0;
    if (a0 != 0) goto loc_80020334;
    sw(0, s1 + 0xF0);
    goto loc_80020468;
loc_80020334:
    v0 = a0 << 3;
loc_80020338:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_80020384;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_80020468;
loc_80020384:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_80020468;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80020338;
    sw(0, s0);
    goto loc_80020468;
loc_800203A8:
    v0 = *gPlayerNum;
    v0 <<= 2;
    at = ptrToVmAddr(&gpPlayerCtrlBindings[0]);
    at += v0;
    v1 = lw(at);
    at = 0x80070000;                                    // Result = 80070000
    at += 0x7F44;                                       // Result = gTicButtons[0] (80077F44)
    at += v0;
    v0 = lw(at);
    v1 = lw(v1);
    v0 &= v1;
    if (v0 == 0) goto loc_800203FC;
    a0 = s1;
    P_FireWeapon(*vmAddrToPtr<player_t>(a0));
    goto loc_80020468;
loc_800203FC:
    v1 = *gTicCon;
    a0 = 0x80070000;                                    // Result = 80070000
    a0 = lw(a0 + 0x7BD0);                               // Load from: gpFineCosine (80077BD0)
    v1 <<= 6;
    a1 = v1 & 0x1FFF;
    v0 = a1 << 2;
    v0 += a0;
    a0 = lh(s1 + 0x22);
    v0 = lw(v0);
    mult(a0, v0);
    a1 = v1 & 0xFFF;
    v1 = 0x10000;                                       // Result = 00010000
    v0 = lo;
    v0 += v1;
    sw(v0, s0 + 0x8);
    v0 = a1 << 2;
    v1 = lh(s1 + 0x22);
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7958;                                       // Result = FineSine[0] (80067958)
    at += v0;
    v0 = lw(at);
    mult(v1, v0);
    v0 = lo;
    sw(v0, s0 + 0xC);
loc_80020468:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Re-fire the current weapon if the appropriate button is pressed and if the conditions are right.
// Otherwise switch to another weapon if out of ammo after firing.
//------------------------------------------------------------------------------------------------------------------------------------------
void A_ReFire(player_t& player, [[maybe_unused]] pspdef_t& sprite) noexcept {
    const padbuttons_t fireBtn = gpPlayerCtrlBindings[*gPlayerNum][cbind_attack];

    if ((gTicButtons[*gPlayerNum] & fireBtn) && (player.pendingweapon == wp_nochange) && (player.health != 0)) {
        player.refire++;
        P_FireWeapon(player);
    } else {
        player.refire = 0;
        P_CheckAmmo(player);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Check if we have enough ammo to fire before reloading, and if there is not enough switch weapons
//------------------------------------------------------------------------------------------------------------------------------------------
void A_CheckReload(player_t& player, [[maybe_unused]] pspdef_t& sprite) noexcept {
    P_CheckAmmo(player);
}

void A_Lower() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    v0 = 0x5F0000;                                      // Result = 005F0000
    v0 |= 0xFFFF;                                       // Result = 005FFFFF
    sw(ra, sp + 0x18);
    sw(s0, sp + 0x10);
    v1 = lw(a1 + 0xC);
    a0 = 0xC0000;                                       // Result = 000C0000
    v1 += a0;
    v0 = (i32(v0) < i32(v1));
    sw(v1, a1 + 0xC);
    if (v0 == 0) goto loc_8002069C;
    v1 = lw(s1 + 0x4);
    v0 = 1;                                             // Result = 00000001
    {
        const bool bJump = (v1 != v0);
        v0 = 0x600000;                                  // Result = 00600000
        if (bJump) goto loc_80020588;
    }
    sw(v0, a1 + 0xC);
    goto loc_8002069C;
loc_80020588:
    v0 = lw(s1 + 0x24);
    {
        const bool bJump = (v0 == 0);
        v0 = 0xA;                                       // Result = 0000000A
        if (bJump) goto loc_80020624;
    }
    a0 = lw(s1 + 0x70);
    v1 = lw(s1 + 0x70);
    sw(a0, s1 + 0x6C);
    if (v1 != v0) goto loc_800205B0;
    sw(a0, s1 + 0x70);
loc_800205B0:
    v1 = lw(s1 + 0x70);
    v0 = 8;                                             // Result = 00000008
    s0 = s1 + 0xF0;
    if (v1 != v0) goto loc_800205E4;
    v0 = *gbIsLevelDataCached;
    {
        const bool bJump = (v0 == 0);
        v0 = v1 << 1;
        if (bJump) goto loc_800205EC;
    }
    a0 = lw(s1);
    a1 = sfx_sawup;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v1 = lw(s1 + 0x70);
loc_800205E4:
    v0 = v1 << 1;
loc_800205EC:
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F8;                                       // Result = WeaponInfo_Fist[1] (800670F8)
    at += v0;
    v1 = lw(at);
    v0 = 0xA;                                           // Result = 0000000A
    sw(v0, s1 + 0x70);
    v0 = 0x10000;                                       // Result = 00010000
    sw(v0, s1 + 0xF8);
    v0 = 0x600000;                                      // Result = 00600000
    a0 = v1;
    sw(v0, s1 + 0xFC);
    if (a0 != 0) goto loc_8002062C;
loc_80020624:
    sw(0, s1 + 0xF0);
    goto loc_8002069C;
loc_8002062C:
    v0 = a0 << 3;
loc_80020630:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_8002067C;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_8002069C;
loc_8002067C:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_8002069C;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80020630;
    sw(0, s0);
loc_8002069C:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

void A_Raise() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    sw(ra, sp + 0x18);
    sw(s0, sp + 0x10);
    v0 = lw(a1 + 0xC);
    v1 = 0xFFF40000;                                    // Result = FFF40000
    v0 += v1;
    sw(v0, a1 + 0xC);
    if (i32(v0) > 0) goto loc_80020788;
    sw(0, a1 + 0xC);
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7100;                                       // Result = WeaponInfo_Fist[3] (80067100)
    at += v0;
    a0 = lw(at);
    s0 = s1 + 0xF0;
    if (a0 != 0) goto loc_80020718;
    sw(0, s1 + 0xF0);
    goto loc_80020788;
loc_80020718:
    v0 = a0 << 3;
loc_8002071C:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_80020768;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_80020788;
loc_80020768:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_80020788;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_8002071C;
    sw(0, s0);
loc_80020788:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

void A_GunFlash() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    sw(ra, sp + 0x18);
    sw(s0, sp + 0x10);
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7108;                                       // Result = WeaponInfo_Fist[5] (80067108)
    at += v0;
    a0 = lw(at);
    s0 = s1 + 0x100;
    if (a0 != 0) goto loc_800207EC;
    sw(0, s1 + 0x100);
    goto loc_8002085C;
loc_800207EC:
    v0 = a0 << 3;
loc_800207F0:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_8002083C;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_8002085C;
loc_8002083C:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_8002085C;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_800207F0;
    sw(0, s0);
loc_8002085C:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

void A_Punch() noexcept {
    sp -= 0x30;
    sw(s3, sp + 0x24);
    s3 = a0;
    sw(ra, sp + 0x28);
    sw(s2, sp + 0x20);
    sw(s1, sp + 0x1C);
    sw(s0, sp + 0x18);
    _thunk_P_Random();
    v0 &= 7;
    v0++;
    v1 = v0 << 1;
    a0 = lw(s3 + 0x34);
    s2 = v1 + v0;
    if (a0 == 0) goto loc_800208BC;
    v0 = s2 << 2;
    v0 += s2;
    s2 = v0 << 1;
loc_800208BC:
    v0 = lw(s3);
    s1 = lw(v0 + 0x24);
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    a2 = 0x460000;                                      // Result = 00460000
    a3 = 0x7FFF0000;                                    // Result = 7FFF0000
    a3 |= 0xFFFF;                                       // Result = 7FFFFFFF
    s0 -= v0;
    s0 <<= 18;
    sw(s2, sp + 0x10);
    a0 = lw(s3);
    a1 = s1 + s0;
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
    v0 = 0x80070000;                                    // Result = 80070000
    v0 = lw(v0 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    if (v0 == 0) goto loc_8002094C;
    a0 = lw(s3);
    a1 = sfx_punch;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v0 = lw(s3);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    a0 = lw(v0);
    a1 = lw(v0 + 0x4);
    a2 = lw(v1);
    a3 = lw(v1 + 0x4);
    v0 = R_PointToAngle2(a0, a1, a2, a3);
    v1 = lw(s3);
    sw(v0, v1 + 0x24);
loc_8002094C:
    ra = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x30;
    return;
}

void A_Saw() noexcept {
    sp -= 0x30;
    sw(s3, sp + 0x24);
    s3 = a0;
    sw(ra, sp + 0x28);
    sw(s2, sp + 0x20);
    sw(s1, sp + 0x1C);
    sw(s0, sp + 0x18);
    _thunk_P_Random();
    v0 &= 7;
    v0++;
    s1 = v0 << 1;
    v1 = lw(s3);
    s2 = lw(v1 + 0x24);
    s1 += v0;
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    a2 = 0x460000;                                      // Result = 00460000
    a2 |= 1;                                            // Result = 00460001
    a3 = 0x7FFF0000;                                    // Result = 7FFF0000
    a3 |= 0xFFFF;                                       // Result = 7FFFFFFF
    s0 -= v0;
    s0 <<= 18;
    sw(s1, sp + 0x10);
    a0 = lw(s3);
    a1 = s2 + s0;
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
    v0 = 0x80070000;                                    // Result = 80070000
    v0 = lw(v0 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    if (v0 != 0) goto loc_80020A04;
    a0 = lw(s3);
    a1 = sfx_sawful;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    goto loc_80020AC4;
loc_80020A04:
    a0 = lw(s3);
    a1 = sfx_sawhit;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v0 = lw(s3);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    a0 = lw(v0);
    a1 = lw(v0 + 0x4);
    a2 = lw(v1);
    a3 = lw(v1 + 0x4);
    v0 = R_PointToAngle2(a0, a1, a2, a3);
    a2 = lw(s3);
    s2 = v0;
    a1 = lw(a2 + 0x24);
    v0 = 0x80000000;                                    // Result = 80000000
    v1 = s2 - a1;
    v0 = (v0 < v1);
    {
        const bool bJump = (v0 == 0);
        v0 = 0xFCCC0000;                                // Result = FCCC0000
        if (bJump) goto loc_80020A84;
    }
    v0 |= 0xCCCC;                                       // Result = FCCCCCCC
    v0 = (v0 < v1);
    if (v0 != 0) goto loc_80020A74;
    v0 = 0x30C0000;                                     // Result = 030C0000
    v0 |= 0x30C3;                                       // Result = 030C30C3
    v0 += s2;
    goto loc_80020AA8;
loc_80020A74:
    v0 = 0xFCCC0000;                                    // Result = FCCC0000
    v0 |= 0xCCCD;                                       // Result = FCCCCCCD
    v0 += a1;
    goto loc_80020AA8;
loc_80020A84:
    a0 = 0x3330000;                                     // Result = 03330000
    a0 |= 0x3333;                                       // Result = 03333333
    v0 = (a0 < v1);
    {
        const bool bJump = (v0 == 0);
        v0 = 0xFCF30000;                                // Result = FCF30000
        if (bJump) goto loc_80020AA4;
    }
    v0 |= 0xCF3D;                                       // Result = FCF3CF3D
    v0 += s2;
    goto loc_80020AA8;
loc_80020AA4:
    v0 = a1 + a0;
loc_80020AA8:
    sw(v0, a2 + 0x24);
    v1 = lw(s3);
    v0 = lw(v1 + 0x64);
    v0 |= 0x80;
    sw(v0, v1 + 0x64);
loc_80020AC4:
    ra = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x30;
    return;
}

void A_FireMissile() noexcept {
    sp -= 0x18;
    sw(ra, sp + 0x10);
    v1 = lw(a0 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v0;
    v1 = lw(at);
    v1 <<= 2;
    v1 += a0;
    v0 = lw(v1 + 0x98);
    v0--;
    sw(v0, v1 + 0x98);
    a0 = lw(a0);
    a1 = 0x17;                                          // Result = 00000017
    P_SpawnPlayerMissile(*vmAddrToPtr<mobj_t>(a0), (mobjtype_t) a1);
    ra = lw(sp + 0x10);
    sp += 0x18;
    return;
}

void A_FireBFG() noexcept {
    sp -= 0x18;
    sw(ra, sp + 0x10);
    v1 = lw(a0 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v0;
    v1 = lw(at);
    v1 <<= 2;
    v1 += a0;
    v0 = lw(v1 + 0x98);
    v0 -= 0x28;
    sw(v0, v1 + 0x98);
    a0 = lw(a0);
    a1 = 0x19;                                          // Result = 00000019
    P_SpawnPlayerMissile(*vmAddrToPtr<mobj_t>(a0), (mobjtype_t) a1);
    ra = lw(sp + 0x10);
    sp += 0x18;
    return;
}

void A_FirePlasma() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    sw(ra, sp + 0x18);
    sw(s0, sp + 0x10);
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v0;
    v1 = lw(at);
    v1 <<= 2;
    v1 += s1;
    v0 = lw(v1 + 0x98);
    v0--;
    sw(v0, v1 + 0x98);
    _thunk_P_Random();
    a0 = lw(s1 + 0x6C);
    v0 &= 1;
    v1 = a0 << 1;
    v1 += a0;
    v1 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7108;                                       // Result = WeaponInfo_Fist[5] (80067108)
    at += v1;
    v1 = lw(at);
    a0 = v0 + v1;
    s0 = s1 + 0x100;
    if (a0 != 0) goto loc_80020C40;
    sw(0, s1 + 0x100);
    goto loc_80020CB0;
loc_80020C40:
    v0 = a0 << 3;
loc_80020C44:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_80020C90;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_80020CB0;
loc_80020C90:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_80020CB0;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80020C44;
    sw(0, s0);
loc_80020CB0:
    a0 = lw(s1);
    a1 = 0x18;                                          // Result = 00000018
    P_SpawnPlayerMissile(*vmAddrToPtr<mobj_t>(a0), (mobjtype_t) a1);
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

void P_BulletSlope() noexcept {
    sp -= 0x20;
    sw(s1, sp + 0x14);
    s1 = a0;
    sw(ra, sp + 0x18);
    sw(s0, sp + 0x10);
    s0 = lw(s1 + 0x24);
    a2 = 0x4000000;                                     // Result = 04000000
    a1 = s0;
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    sw(v0, gp + 0xA10);                                 // Store to: gSlope (80077FF0)
    v0 = 0x4000000;                                     // Result = 04000000
    if (v1 != 0) goto loc_80020D48;
    s0 += v0;
    a0 = s1;
    a1 = s0;
    a2 = 0x4000000;                                     // Result = 04000000
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    sw(v0, gp + 0xA10);                                 // Store to: gSlope (80077FF0)
    a0 = s1;
    if (v1 != 0) goto loc_80020D48;
    a1 = 0xF8000000;                                    // Result = F8000000
    a1 += s0;
    a2 = 0x4000000;                                     // Result = 04000000
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    sw(v0, gp + 0xA10);                                 // Store to: gSlope (80077FF0)
loc_80020D48:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

void P_GunShot() noexcept {
    sp -= 0x30;
    sw(s3, sp + 0x24);
    s3 = a0;
    sw(s0, sp + 0x18);
    s0 = a1;
    sw(ra, sp + 0x28);
    sw(s2, sp + 0x20);
    sw(s1, sp + 0x1C);
    _thunk_P_Random();
    v0 &= 3;
    v0++;
    s1 = lw(s3 + 0x24);
    s2 = v0 << 2;
    if (s0 != 0) goto loc_80020DB4;
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    s0 -= v0;
    s0 <<= 18;
    s1 += s0;
loc_80020DB4:
    sw(s2, sp + 0x10);
    a3 = 0x7FFF0000;                                    // Result = 7FFF0000
    a3 |= 0xFFFF;                                       // Result = 7FFFFFFF
    a0 = s3;
    a1 = s1;
    a2 = 0x8000000;                                     // Result = 08000000
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
    ra = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x30;
    return;
}

void A_FirePistol() noexcept {
    sp -= 0x30;
    sw(s1, sp + 0x1C);
    s1 = a0;
    sw(ra, sp + 0x28);
    sw(s3, sp + 0x24);
    sw(s2, sp + 0x20);
    sw(s0, sp + 0x18);
    a0 = lw(s1);
    a1 = sfx_pistol;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v0;
    v1 = lw(at);
    v1 <<= 2;
    v1 += s1;
    v0 = lw(v1 + 0x98);
    v0--;
    sw(v0, v1 + 0x98);
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7108;                                       // Result = WeaponInfo_Fist[5] (80067108)
    at += v0;
    a0 = lw(at);
    s0 = s1 + 0x100;
    if (a0 != 0) goto loc_80020E90;
    sw(0, s1 + 0x100);
    goto loc_80020F00;
loc_80020E90:
    v0 = a0 << 3;
loc_80020E94:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_80020EE0;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_80020F00;
loc_80020EE0:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_80020F00;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80020E94;
    sw(0, s0);
loc_80020F00:
    s0 = lw(s1 + 0xC4);
    s3 = lw(s1);
    s0 = (s0 < 1);
    _thunk_P_Random();
    v0 &= 3;
    v0++;
    s1 = lw(s3 + 0x24);
    s2 = v0 << 2;
    if (s0 != 0) goto loc_80020F40;
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    s0 -= v0;
    s0 <<= 18;
    s1 += s0;
loc_80020F40:
    sw(s2, sp + 0x10);
    a0 = s3;
    a1 = s1;
    a3 = 0x7FFF0000;                                    // Result = 7FFF0000
    a3 |= 0xFFFF;                                       // Result = 7FFFFFFF
    a2 = 0x8000000;                                     // Result = 08000000
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
    ra = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x30;
    return;
}

void A_FireShotgun() noexcept {
    sp -= 0x38;
    sw(s3, sp + 0x24);
    s3 = a0;
    sw(ra, sp + 0x30);
    sw(s5, sp + 0x2C);
    sw(s4, sp + 0x28);
    sw(s2, sp + 0x20);
    sw(s1, sp + 0x1C);
    sw(s0, sp + 0x18);
    a0 = lw(s3);
    a1 = sfx_shotgn;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v1 = lw(s3 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v0;
    v1 = lw(at);
    v1 <<= 2;
    v1 += s3;
    v0 = lw(v1 + 0x98);
    v0--;
    sw(v0, v1 + 0x98);
    v1 = lw(s3 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7108;                                       // Result = WeaponInfo_Fist[5] (80067108)
    at += v0;
    a0 = lw(at);
    s0 = s3 + 0x100;
    if (a0 != 0) goto loc_80021024;
    sw(0, s3 + 0x100);
    goto loc_80021094;
loc_80021024:
    v0 = a0 << 3;
loc_80021028:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s3;
    if (v0 == 0) goto loc_80021074;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_80021094;
loc_80021074:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_80021094;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80021028;
    sw(0, s0);
loc_80021094:
    a0 = lw(s3);
    a2 = 0x8000000;                                     // Result = 08000000
    a1 = lw(a0 + 0x24);
    s4 = 0;                                             // Result = 00000000
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    s5 = v0;
loc_800210AC:
    s4++;
    _thunk_P_Random();
    s0 = v0 & 3;
    s0++;
    v1 = lw(s3);
    s2 = lw(v1 + 0x24);
    s0 <<= 2;
    _thunk_P_Random();
    s1 = v0;
    _thunk_P_Random();
    a2 = 0x8000000;                                     // Result = 08000000
    s1 -= v0;
    s1 <<= 18;
    a3 = s5;
    sw(s0, sp + 0x10);
    a0 = lw(s3);
    a1 = s2 + s1;
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
    v0 = (i32(s4) < 7);
    if (v0 != 0) goto loc_800210AC;
    ra = lw(sp + 0x30);
    s5 = lw(sp + 0x2C);
    s4 = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x38;
    return;
}

void A_FireShotgun2() noexcept {
    sp -= 0x30;
    sw(s3, sp + 0x24);
    s3 = a0;
    sw(ra, sp + 0x2C);
    sw(s4, sp + 0x28);
    sw(s2, sp + 0x20);
    sw(s1, sp + 0x1C);
    sw(s0, sp + 0x18);
    a0 = lw(s3);
    a1 = sfx_dshtgn;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    a0 = lw(s3);
    a1 = 0xA0;
    v0 = P_SetMObjState(*vmAddrToPtr<mobj_t>(a0), (statenum_t) a1);
    v1 = lw(s3 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v0;
    v1 = lw(at);
    v1 <<= 2;
    v1 += s3;
    v0 = lw(v1 + 0x98);
    v0 -= 2;
    sw(v0, v1 + 0x98);
    v1 = lw(s3 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7108;                                       // Result = WeaponInfo_Fist[5] (80067108)
    at += v0;
    a0 = lw(at);
    s0 = s3 + 0x100;
    if (a0 != 0) goto loc_800211DC;
    sw(0, s3 + 0x100);
    goto loc_8002124C;
loc_800211DC:
    v0 = a0 << 3;
loc_800211E0:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s3;
    if (v0 == 0) goto loc_8002122C;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_8002124C;
loc_8002122C:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_8002124C;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_800211E0;
    sw(0, s0);
loc_8002124C:
    s1 = lw(s3);
    a2 = 0x4000000;                                     // Result = 04000000
    s0 = lw(s1 + 0x24);
    a0 = s1;
    a1 = s0;
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    sw(v0, gp + 0xA10);                                 // Store to: gSlope (80077FF0)
    s4 = 0;                                             // Result = 00000000
    if (v1 != 0) goto loc_800212B8;
    v0 = 0x4000000;                                     // Result = 04000000
    s0 += v0;
    a0 = s1;
    a1 = s0;
    a2 = 0x4000000;                                     // Result = 04000000
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    sw(v0, gp + 0xA10);                                 // Store to: gSlope (80077FF0)
    a0 = s1;
    if (v1 != 0) goto loc_800212B8;
    a1 = 0xF8000000;                                    // Result = F8000000
    a1 += s0;
    a2 = 0x4000000;                                     // Result = 04000000
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    sw(v0, gp + 0xA10);                                 // Store to: gSlope (80077FF0)
loc_800212B8:
    s4++;
    _thunk_P_Random();
    v1 = 0x55550000;                                    // Result = 55550000
    v1 |= 0x5556;                                       // Result = 55555556
    mult(v0, v1);
    v1 = lw(s3);
    s2 = lw(v1 + 0x24);
    v1 = u32(i32(v0) >> 31);
    a0 = hi;
    a0 -= v1;
    v1 = a0 << 1;
    v1 += a0;
    v0 -= v1;
    v0++;
    s1 = v0 << 2;
    s1 += v0;
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    s0 -= v0;
    s0 <<= 19;
    s2 += s0;
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    a1 = s2;
    a2 = 0x8000000;                                     // Result = 08000000
    s0 -= v0;
    a3 = lw(gp + 0xA10);                                // Load from: gSlope (80077FF0)
    s0 <<= 5;
    sw(s1, sp + 0x10);
    a0 = lw(s3);
    a3 += s0;
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
    v0 = (i32(s4) < 0x14);
    if (v0 != 0) goto loc_800212B8;
    ra = lw(sp + 0x2C);
    s4 = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x30;
    return;
}

void A_FireCGun() noexcept {
    sp -= 0x30;
    sw(s1, sp + 0x1C);
    s1 = a0;
    sw(s0, sp + 0x18);
    s0 = a1;
    sw(ra, sp + 0x28);
    sw(s3, sp + 0x24);
    sw(s2, sp + 0x20);
    a0 = lw(s1);
    a1 = sfx_pistol;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v0 = lw(s1 + 0x6C);
    v1 = v0 << 1;
    v1 += v0;
    v1 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F4;                                       // Result = WeaponInfo_Fist[0] (800670F4)
    at += v1;
    v0 = lw(at);
    v0 <<= 2;
    v1 = v0 + s1;
    v0 = lw(v1 + 0x98);
    {
        const bool bJump = (v0 == 0);
        v0--;
        if (bJump) goto loc_8002153C;
    }
    sw(v0, v1 + 0x98);
    v1 = lw(s1 + 0x6C);
    v0 = v1 << 1;
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x7108;                                       // Result = WeaponInfo_Fist[5] (80067108)
    at += v0;
    v0 = lw(at);
    v1 = v0 << 3;
    v1 -= v0;
    v1 <<= 2;
    v0 = lw(s0);
    v1 += v0;
    v0 = 0x80060000;                                    // Result = 80060000
    v0 -= 0x6CC4;                                       // Result = State_S_CHAIN1[0] (8005933C)
    v1 -= v0;
    v0 = v1 << 3;
    v0 += v1;
    a0 = v0 << 6;
    v0 += a0;
    v0 <<= 3;
    v0 += v1;
    a0 = v0 << 15;
    v0 += a0;
    v0 <<= 3;
    v0 += v1;
    v0 = -v0;
    a0 = u32(i32(v0) >> 2);
    s0 = s1 + 0x100;
    if (a0 != 0) goto loc_80021470;
    sw(0, s1 + 0x100);
    goto loc_800214E0;
loc_80021470:
    v0 = a0 << 3;
loc_80021474:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_800214C0;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_800214E0;
loc_800214C0:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_800214E0;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80021474;
    sw(0, s0);
loc_800214E0:
    s0 = lw(s1 + 0xC4);
    s3 = lw(s1);
    s0 = (s0 < 1);
    _thunk_P_Random();
    v0 &= 3;
    v0++;
    s1 = lw(s3 + 0x24);
    s2 = v0 << 2;
    if (s0 != 0) goto loc_80021520;
    _thunk_P_Random();
    s0 = v0;
    _thunk_P_Random();
    s0 -= v0;
    s0 <<= 18;
    s1 += s0;
loc_80021520:
    sw(s2, sp + 0x10);
    a0 = s3;
    a1 = s1;
    a3 = 0x7FFF0000;                                    // Result = 7FFF0000
    a3 |= 0xFFFF;                                       // Result = 7FFFFFFF
    a2 = 0x8000000;                                     // Result = 08000000
    P_LineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2, a3, lw(sp + 0x10));
loc_8002153C:
    ra = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x30;
    return;
}

void A_Light0() noexcept {
    sw(0, a0 + 0xE4);
}

void A_Light1() noexcept {
    v0 = 8;
    sw(v0, a0 + 0xE4);
}

void A_Light2() noexcept {
    v0 = 0x10;
    sw(v0, a0 + 0xE4);
}

void A_BFGSpray() noexcept {
    sp -= 0x28;
    sw(s3, sp + 0x1C);
    s3 = a0;
    sw(s4, sp + 0x20);
    s4 = 0;                                             // Result = 00000000
    sw(s2, sp + 0x18);
    s2 = 0;                                             // Result = 00000000
    sw(ra, sp + 0x24);
    sw(s1, sp + 0x14);
    sw(s0, sp + 0x10);
    a2 = 0x4000000;                                     // Result = 04000000
loc_800215A8:
    a1 = 0xE0000000;                                    // Result = E0000000
    a1 += s2;
    v0 = lw(s3 + 0x24);
    a0 = lw(s3 + 0x74);
    a1 += v0;
    v0 = P_AimLineAttack(*vmAddrToPtr<mobj_t>(a0), a1, a2);
    v0 = 0x80070000;                                    // Result = 80070000
    v0 = lw(v0 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    a3 = 0x20;                                          // Result = 00000020
    if (v0 == 0) goto loc_8002162C;
    s1 = 0;                                             // Result = 00000000
    s0 = 0;                                             // Result = 00000000
    a0 = lw(v0);
    a1 = lw(v0 + 0x4);
    a2 = lw(v0 + 0x44);
    v0 = lw(v0 + 0x8);
    a2 = u32(i32(a2) >> 2);
    a2 += v0;
    v0 = ptrToVmAddr(P_SpawnMobj(a0, a1, a2, (mobjtype_t) a3));
loc_800215F8:
    s0++;
    _thunk_P_Random();
    v1 = s1 + 1;
    v0 &= 7;
    s1 = v1 + v0;
    v0 = (i32(s0) < 0xF);
    a3 = s1;
    if (v0 != 0) goto loc_800215F8;
    a1 = lw(s3 + 0x74);
    a0 = 0x80070000;                                    // Result = 80070000
    a0 = lw(a0 + 0x7EE8);                               // Load from: gpLineTarget (80077EE8)
    a2 = a1;
    P_DamageMObj(*vmAddrToPtr<mobj_t>(a0), vmAddrToPtr<mobj_t>(a1), vmAddrToPtr<mobj_t>(a2), a3);
loc_8002162C:
    v0 = 0x1990000;                                     // Result = 01990000
    v0 |= 0x9999;                                       // Result = 01999999
    s2 += v0;
    s4++;
    v0 = (i32(s4) < 0x28);
    a2 = 0x4000000;                                     // Result = 04000000
    if (v0 != 0) goto loc_800215A8;
    ra = lw(sp + 0x24);
    s4 = lw(sp + 0x20);
    s3 = lw(sp + 0x1C);
    s2 = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x28;
    return;
}

void A_BFGsound() noexcept {
    a0 = lw(a0);
    a1 = sfx_bfg;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
}

void A_OpenShotgun2() noexcept {
    a0 = lw(a0);
    a1 = sfx_dbopn;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
}

void A_LoadShotgun2() noexcept {
    a0 = lw(a0);
    a1 = sfx_dbload;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
}

void A_CloseShotgun2() noexcept {
    sp -= 0x18;
    sw(s0, sp + 0x10);
    s0 = a0;
    sw(ra, sp + 0x14);
    a0 = lw(s0);
    a1 = sfx_dbcls;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v0 = *gPlayerNum;
    v0 <<= 2;
    at = ptrToVmAddr(&gpPlayerCtrlBindings[0]);
    at += v0;
    v1 = lw(at);
    at = 0x80070000;                                    // Result = 80070000
    at += 0x7F44;                                       // Result = gTicButtons[0] (80077F44)
    at += v0;
    v0 = lw(at);
    v1 = lw(v1);
    v0 &= v1;
    {
        const bool bJump = (v0 == 0);
        v0 = 0xA;                                       // Result = 0000000A
        if (bJump) goto loc_80021774;
    }
    v1 = lw(s0 + 0x70);
    if (v1 != v0) goto loc_80021774;
    v0 = lw(s0 + 0x24);
    a0 = s0;
    if (v0 == 0) goto loc_80021774;
    v0 = lw(s0 + 0xC4);
    v0++;
    sw(v0, a0 + 0xC4);
    P_FireWeapon(*vmAddrToPtr<player_t>(a0));
    goto loc_80021780;
loc_80021774:
    sw(0, s0 + 0xC4);
    a0 = s0;
    v0 = P_CheckAmmo(*vmAddrToPtr<player_t>(a0));
loc_80021780:
    ra = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x18;
    return;
}

void P_SetupPsprites() noexcept {
loc_80021794:
    sp -= 0x20;
    v1 = a0 << 2;
    a1 = 1;                                             // Result = 00000001
    sw(ra, sp + 0x18);
    sw(s1, sp + 0x14);
    sw(s0, sp + 0x10);
    at = 0x80080000;                                    // Result = 80080000
    at -= 0x7F70;                                       // Result = gTicRemainder[0] (80078090)
    at += v1;
    sw(0, at);
    v1 += a0;
    v0 = v1 << 4;
    v0 -= v1;
    v0 <<= 2;
    v1 = 0x800B0000;                                    // Result = 800B0000
    v1 -= 0x7814;                                       // Result = gPlayer1[0] (800A87EC)
    s1 = v0 + v1;
    v0 = s1 + 0x10;
loc_800217DC:
    sw(0, v0 + 0xF0);
    a1--;
    v0 -= 0x10;
    if (i32(a1) >= 0) goto loc_800217DC;
    v1 = lw(s1 + 0x6C);
    v0 = 0xA;                                           // Result = 0000000A
    sw(v1, s1 + 0x70);
    if (v1 != v0) goto loc_80021808;
    v0 = lw(s1 + 0x6C);
    sw(v0, s1 + 0x70);
loc_80021808:
    v1 = lw(s1 + 0x70);
    v0 = 8;                                             // Result = 00000008
    s0 = s1 + 0xF0;
    if (v1 != v0) goto loc_8002183C;
    v0 = *gbIsLevelDataCached;
    {
        const bool bJump = (v0 == 0);
        v0 = v1 << 1;
        if (bJump) goto loc_80021844;
    }
    a0 = lw(s1);
    a1 = sfx_sawup;
    S_StartSound(vmAddrToPtr<mobj_t>(a0), (sfxenum_t) a1);
    v1 = lw(s1 + 0x70);
loc_8002183C:
    v0 = v1 << 1;
loc_80021844:
    v0 += v1;
    v0 <<= 3;
    at = 0x80060000;                                    // Result = 80060000
    at += 0x70F8;                                       // Result = WeaponInfo_Fist[1] (800670F8)
    at += v0;
    v1 = lw(at);
    v0 = 0xA;                                           // Result = 0000000A
    sw(v0, s1 + 0x70);
    v0 = 0x10000;                                       // Result = 00010000
    sw(v0, s1 + 0xF8);
    v0 = 0x600000;                                      // Result = 00600000
    a0 = v1;
    sw(v0, s1 + 0xFC);
    if (a0 != 0) goto loc_80021884;
    sw(0, s1 + 0xF0);
    goto loc_800218F4;
loc_80021884:
    v0 = a0 << 3;
loc_80021888:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_800218D4;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_800218F4;
loc_800218D4:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_800218F4;
    v0 = a0 << 3;
    if (a0 != 0) goto loc_80021888;
    sw(0, s0);
loc_800218F4:
    ra = lw(sp + 0x18);
    s1 = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x20;
    return;
}

void P_MovePsprites() noexcept {
loc_8002190C:
    sp -= 0x38;
    sw(s1, sp + 0x1C);
    s1 = a0;
    v1 = *gPlayerNum;
    a1 = 0x80080000;                                    // Result = 80080000
    a1 -= 0x7F70;                                       // Result = gTicRemainder[0] (80078090)
    sw(ra, sp + 0x30);
    sw(s5, sp + 0x2C);
    sw(s4, sp + 0x28);
    sw(s3, sp + 0x24);
    sw(s2, sp + 0x20);
    sw(s0, sp + 0x18);
    v1 <<= 2;
    a0 = v1 + a1;
    v0 = lw(a0);
    at = 0x80070000;                                    // Result = 80070000
    at += 0x7FBC;                                       // Result = gPlayersElapsedVBlanks[0] (80077FBC)
    at += v1;
    v1 = lw(at);
    v0 += v1;
    sw(v0, a0);
    v0 = (i32(v0) < 4);
    if (v0 != 0) goto loc_80021A94;
loc_80021974:
    s3 = s1 + 0xF0;
    s5 = 0;                                             // Result = 00000000
    v1 = *gPlayerNum;
    s2 = s1 + 0xF4;
    v1 <<= 2;
    v1 += a1;
    v0 = lw(v1);
    s4 = 0xF0;                                          // Result = 000000F0
    v0 -= 4;
    sw(v0, v1);
loc_800219A0:
    v0 = lw(s3);
    {
        const bool bJump = (v0 == 0);
        v0 = -1;                                        // Result = FFFFFFFF
        if (bJump) goto loc_80021A48;
    }
    v1 = lw(s2);
    {
        const bool bJump = (v1 == v0);
        v0 = v1 - 1;
        if (bJump) goto loc_80021A48;
    }
    sw(v0, s2);
    if (v0 != 0) goto loc_80021A48;
    v0 = lw(s3);
    a0 = lw(v0 + 0x10);
    s0 = s1 + s4;
    goto loc_80021A3C;
loc_800219DC:
    v0 -= a0;
    v0 <<= 2;
    v1 = 0x80060000;                                    // Result = 80060000
    v1 -= 0x7274;                                       // Result = State_S_NULL[0] (80058D8C)
    v0 += v1;
    sw(v0, s0);
    v1 = lw(v0 + 0x8);
    sw(v1, s0 + 0x4);
    v0 = lw(v0 + 0xC);
    a0 = s1;
    if (v0 == 0) goto loc_80021A28;
    a1 = s0;
    ptr_call(v0);
    v0 = lw(s0);
    if (v0 == 0) goto loc_80021A48;
loc_80021A28:
    v0 = lw(s0);
    v1 = lw(s0 + 0x4);
    a0 = lw(v0 + 0x10);
    if (v1 != 0) goto loc_80021A48;
loc_80021A3C:
    v0 = a0 << 3;
    if (a0 != 0) goto loc_800219DC;
    sw(0, s0);
loc_80021A48:
    s4 += 0x10;
    s5++;
    s2 += 0x10;
    v0 = (i32(s5) < 2);
    s3 += 0x10;
    if (v0 != 0) goto loc_800219A0;
    v0 = *gPlayerNum;
    v0 <<= 2;
    at = 0x80080000;                                    // Result = 80080000
    at -= 0x7F70;                                       // Result = gTicRemainder[0] (80078090)
    at += v0;
    v0 = lw(at);
    a1 = 0x80080000;                                    // Result = 80080000
    a1 -= 0x7F70;                                       // Result = gTicRemainder[0] (80078090)
    v0 = (i32(v0) < 4);
    if (v0 == 0) goto loc_80021974;
loc_80021A94:
    v0 = lw(s1 + 0xF8);
    v1 = lw(s1 + 0xFC);
    sw(v0, s1 + 0x108);
    sw(v1, s1 + 0x10C);
    ra = lw(sp + 0x30);
    s5 = lw(sp + 0x2C);
    s4 = lw(sp + 0x28);
    s3 = lw(sp + 0x24);
    s2 = lw(sp + 0x20);
    s1 = lw(sp + 0x1C);
    s0 = lw(sp + 0x18);
    sp += 0x38;
    return;
}

// TODO: remove all these thunks
void _thunk_P_FireWeapon() noexcept { P_FireWeapon(*vmAddrToPtr<player_t>(*PsxVm::gpReg_a0)); }
void _thunk_A_ReFire() noexcept { A_ReFire(*vmAddrToPtr<player_t>(*PsxVm::gpReg_a0), *vmAddrToPtr<pspdef_t>(*PsxVm::gpReg_a1)); }
void _thunk_A_CheckReload() noexcept { A_CheckReload(*vmAddrToPtr<player_t>(*PsxVm::gpReg_a0), *vmAddrToPtr<pspdef_t>(*PsxVm::gpReg_a1)); }
