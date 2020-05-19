#pragma once

struct mobj_t;

bool P_CheckMeleeRange(mobj_t& attacker) noexcept;
bool P_CheckMissileRange(mobj_t& attacker) noexcept;
bool P_Move(mobj_t& actor) noexcept;
bool P_TryWalk(mobj_t& actor) noexcept;
void P_NewChaseDir(mobj_t& actor) noexcept;
bool P_LookForPlayers(mobj_t& actor, const bool bAllAround) noexcept;
void A_Look(mobj_t& actor) noexcept;
void A_Chase(mobj_t& actor) noexcept;
void A_FaceTarget(mobj_t& actor) noexcept;
void A_PosAttack(mobj_t& actor) noexcept;
void A_SPosAttack(mobj_t& actor) noexcept;
void A_CPosAttack(mobj_t& actor) noexcept;
void A_CPosRefire(mobj_t& actor) noexcept;
void A_SpidAttack(mobj_t& actor) noexcept;
void A_SpidRefire(mobj_t& actor) noexcept;
void A_BspiAttack(mobj_t& actor) noexcept;
void A_TroopAttack(mobj_t& actor) noexcept;
void A_SargAttack(mobj_t& actor) noexcept;
void A_HeadAttack(mobj_t& actor) noexcept;
void A_CyberAttack(mobj_t& actor) noexcept;
void A_BruisAttack(mobj_t& actor) noexcept;
void A_SkelMissile(mobj_t& actor) noexcept;
void A_Tracer(mobj_t& actor) noexcept;
void A_SkelWhoosh(mobj_t& actor) noexcept;
void A_SkelFist(mobj_t& actor) noexcept;
void A_FatRaise(mobj_t& actor) noexcept;
void A_FatAttack1(mobj_t& actor) noexcept;
void A_FatAttack2(mobj_t& actor) noexcept;
void A_FatAttack3(mobj_t& actor) noexcept;
void A_SkullAttack(mobj_t& actor) noexcept;
void A_PainShootSkull() noexcept;
void A_PainAttack() noexcept;
void A_PainDie() noexcept;
void A_Scream() noexcept;
void A_XScream() noexcept;
void A_Pain() noexcept;
void A_Fall() noexcept;
void A_Explode() noexcept;
void A_BossDeath() noexcept;
void A_Hoof(mobj_t& actor) noexcept;
void A_Metal(mobj_t& actor) noexcept;
void A_BabyMetal(mobj_t& actor) noexcept;
void L_MissileHit(mobj_t& missile) noexcept;
void L_SkullBash(mobj_t& actor) noexcept;

// TODO: remove all these thunks
void _thunk_L_MissileHit() noexcept;
void _thunk_L_SkullBash() noexcept;
