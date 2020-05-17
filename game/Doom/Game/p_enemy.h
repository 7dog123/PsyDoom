#pragma once

struct mobj_t;

bool P_CheckMeleeRange(mobj_t& attacker) noexcept;
bool P_CheckMissileRange(mobj_t& attacker) noexcept;
bool P_Move(mobj_t& actor) noexcept;
bool P_TryWalk(mobj_t& actor) noexcept;
void P_NewChaseDir(mobj_t& actor) noexcept;
bool P_LookForPlayers(mobj_t& actor, const bool bAllAround) noexcept;
void A_Look() noexcept;
void A_Chase() noexcept;
void A_FaceTarget() noexcept;
void A_PosAttack() noexcept;
void A_SPosAttack() noexcept;
void A_CPosAttack() noexcept;
void A_CPosRefire() noexcept;
void A_SpidAttack() noexcept;
void A_SpidRefire() noexcept;
void A_BspiAttack() noexcept;
void A_TroopAttack() noexcept;
void A_SargAttack() noexcept;
void A_HeadAttack() noexcept;
void A_CyberAttack() noexcept;
void A_BruisAttack() noexcept;
void A_SkelMissile() noexcept;
void A_Tracer() noexcept;
void A_SkelWhoosh() noexcept;
void A_SkelFist() noexcept;
void A_FatRaise() noexcept;
void A_FatAttack1() noexcept;
void A_FatAttack2() noexcept;
void A_FatAttack3() noexcept;
void A_SkullAttack() noexcept;
void A_PainShootSkull() noexcept;
void A_PainAttack() noexcept;
void A_PainDie() noexcept;
void A_Scream() noexcept;
void A_XScream() noexcept;
void A_Pain() noexcept;
void A_Fall() noexcept;
void A_Explode() noexcept;
void A_BossDeath() noexcept;
void A_Hoof() noexcept;
void A_Metal() noexcept;
void A_BabyMetal() noexcept;
void L_MissileHit() noexcept;
void L_SkullBash() noexcept;
