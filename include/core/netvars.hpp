#pragma once

#include <atomic>
#include <cstdint>

class IClientEntity;

#define NET_VAR(entity, offset, type) (*(reinterpret_cast<type *>(reinterpret_cast<uint64_t>(entity) + (offset))))
#define NET_INT(entity, offset) NET_VAR(entity, offset, int)
#define NET_FLOAT(entity, offset) NET_VAR(entity, offset, float)
#define NET_BYTE(entity, offset) NET_VAR(entity, offset, unsigned char)
#define NET_VECTOR(entity, offset) NET_VAR(entity, offset, Vector)

typedef unsigned int offset_t;

void InitNetVars();

struct lazy_netvar
{
    const char *n0{};
    const char *n1{};
    const char *n2{};
    const char *n3{};
    const char *n4{};
    uint8_t argc{0};
    mutable std::atomic<int> cached{-1};

    lazy_netvar(const char *a, const char *b) : n0(a), n1(b), argc(2)
    {
    }
    lazy_netvar(const char *a, const char *b, const char *c) : n0(a), n1(b), n2(c), argc(3)
    {
    }
    lazy_netvar(const char *a, const char *b, const char *c, const char *d) : n0(a), n1(b), n2(c), n3(d), argc(4)
    {
    }
    lazy_netvar(const char *a, const char *b, const char *c, const char *d, const char *e) : n0(a), n1(b), n2(c), n3(d), n4(e), argc(5)
    {
    }

    lazy_netvar(const lazy_netvar &)            = delete;
    lazy_netvar &operator=(const lazy_netvar &) = delete;

    int get() const;
    operator int() const
    {
        return get();
    }
    explicit operator bool() const
    {
        return get() > 0;
    }
};

struct lazy_datamap
{
    const char *name{};
    mutable std::atomic<int> cached{-1};
    mutable std::atomic<long long> next_retry{ 0 };

    explicit lazy_datamap(const char *field) : name(field)
    {
    }

    lazy_datamap(const lazy_datamap &)            = delete;
    lazy_datamap &operator=(const lazy_datamap &) = delete;

    int get() const;
    operator int() const
    {
        return get();
    }
    explicit operator bool() const
    {
        return get() > 0;
    }
};

struct lazy_proxy
{
    const char *table{};
    const char *prop{};
    mutable std::atomic<int> cached{-1};

    lazy_proxy(const char *t, const char *p) : table(t), prop(p)
    {
    }

    lazy_proxy(const lazy_proxy &)            = delete;
    lazy_proxy &operator=(const lazy_proxy &) = delete;

    int get() const;
    operator int() const
    {
        return get();
    }
    explicit operator bool() const
    {
        return get() > 0;
    }
};

class NetVars
{
public:
    void Init();

    lazy_netvar iTeamNum{ "DT_BaseEntity", "m_iTeamNum" };
    lazy_netvar iFlags{ "DT_BasePlayer", "m_fFlags" };
    lazy_netvar iHealth{ "DT_BasePlayer", "m_iHealth" };
    lazy_netvar m_vecOrigin{ "DT_BaseEntity", "m_vecOrigin" };

    lazy_netvar m_iAmmoShells{ "DT_ObjectSentrygun", "m_iAmmoShells" };
    lazy_netvar m_iAmmoRockets{ "DT_ObjectSentrygun", "m_iAmmoRockets" };
    lazy_netvar m_iSentryState{ "DT_ObjectSentrygun", "m_iState" };
    lazy_netvar m_bPlayerControlled{ "DT_ObjectSentrygun", "m_bPlayerControlled" };
    lazy_netvar m_bDisabled{ "DT_BaseObject", "m_bDisabled" };
    lazy_netvar m_iAmmoMetal{ "DT_ObjectDispenser", "m_iAmmoMetal" };
    lazy_netvar m_nSetupTimeLength{ "DT_TeamRoundTimer", "m_nSetupTimeLength" };
    lazy_netvar m_nState{ "DT_TeamRoundTimer", "m_nState" };

    lazy_netvar iLifeState{ "DT_BasePlayer", "m_lifeState" };
    lazy_netvar iCond{ "DT_TFPlayer", "m_Shared", "m_nPlayerCond" };
    lazy_netvar iCond1{ "DT_TFPlayer", "m_Shared", "m_nPlayerCondEx" };
    lazy_netvar iCond2{ "DT_TFPlayer", "m_Shared", "m_nPlayerCondEx2" };
    lazy_netvar iCond3{ "DT_TFPlayer", "m_Shared", "m_nPlayerCondEx3" };
    lazy_netvar iClass{ "DT_TFPlayer", "m_PlayerClass", "m_iClass" };
    lazy_netvar vViewOffset{ "DT_BasePlayer", "localdata", "m_vecViewOffset[0]" };
    lazy_netvar hActiveWeapon{ "DT_BaseCombatCharacter", "m_hActiveWeapon" };
    lazy_netvar flChargedDamage{ "DT_TFSniperRifle", "SniperRifleLocalData", "m_flChargedDamage" };

    lazy_netvar m_iUpgradeMetal{ "DT_BaseObject", "m_iUpgradeMetal" };
    lazy_netvar m_flPercentageConstructed{ "DT_BaseObject", "m_flPercentageConstructed" };
    lazy_netvar iUpgradeLevel{ "DT_BaseObject", "m_iUpgradeLevel" };
    lazy_netvar m_iUpgradeMetalRequired{ "DT_BaseObject", "m_iUpgradeMetalRequired" };
    lazy_netvar m_hBuilder{ "DT_BaseObject", "m_hBuilder" };
    lazy_netvar m_bCanPlace{ "DT_BaseObject", "m_bServerOverridePlacement" };
    lazy_netvar m_iObjectType{ "DT_BaseObject", "m_iObjectType" };
    lazy_netvar m_bMiniBuilding{ "DT_BaseObject", "m_bMiniBuilding" };
    lazy_netvar m_bHasSapper{ "DT_BaseObject", "m_bHasSapper" };
    lazy_netvar m_bPlacing{ "DT_BaseObject", "m_bPlacing" };
    lazy_netvar m_bBuilding{ "DT_BaseObject", "m_bBuilding" };
    lazy_netvar m_bPlasmaDisable{ "DT_BaseObject", "m_bPlasmaDisable" };
    lazy_netvar m_bCarryDeploy{ "DT_BaseObject", "m_bCarryDeploy" };

    lazy_netvar m_iTeleState{ "DT_ObjectTeleporter", "m_iState" };
    lazy_netvar m_flTeleRechargeTime{ "DT_ObjectTeleporter", "m_flRechargeTime" };
    lazy_netvar m_flTeleCurrentRechargeDuration{ "DT_ObjectTeleporter", "m_flCurrentRechargeDuration" };
    lazy_netvar m_iTeleTimesUsed{ "DT_ObjectTeleporter", "m_iTimesUsed" };
    lazy_netvar m_flTeleYawToExit{ "DT_ObjectTeleporter", "m_flYawToExit" };
    lazy_netvar m_bMatchBuilding{ "DT_ObjectTeleporter", "m_bMatchBuilding" };

    lazy_netvar iPipeType{ "DT_TFProjectile_Pipebomb", "m_iType" };
    lazy_netvar iBuildingHealth{ "DT_BaseObject", "m_iHealth" };
    lazy_netvar iBuildingMaxHealth{ "DT_BaseObject", "m_iMaxHealth" };
    lazy_netvar m_iAmmo{ "DT_BasePlayer", "localdata", "m_iAmmo" };
    lazy_netvar m_iPrimaryAmmoType{ "DT_BaseCombatWeapon", "LocalWeaponData", "m_iPrimaryAmmoType" };
    lazy_netvar m_iSecondaryAmmoType{ "DT_BaseCombatWeapon", "LocalWeaponData", "m_iSecondaryAmmoType" };
    lazy_netvar iHitboxSet{ "DT_BaseAnimating", "m_nHitboxSet" };
    lazy_netvar vVelocity{ "DT_BasePlayer", "localdata", "m_vecVelocity[0]" };
    lazy_netvar m_vecBaseVelocity{ "DT_BasePlayer", "localdata", "m_vecBaseVelocity" };
    lazy_netvar bGlowEnabled{ "DT_TFPlayer", "m_bGlowEnabled" };
    lazy_netvar iReloadMode{ "DT_TFWeaponBase", "m_iReloadMode" };
    lazy_netvar flLastCritCheckTime{ "DT_TFWeaponBase", "m_flLastCritCheckTime" };
    lazy_netvar bPlayerDominated{ "DT_TFPlayer", "m_Shared", "m_bPlayerDominated" };
    lazy_netvar res_iMaxHealth{ "DT_TFPlayerResource", "m_iMaxHealth" };
    lazy_netvar flNextAttack{ "DT_BaseCombatCharacter", "bcc_localdata", "m_flNextAttack" };
    lazy_netvar flNextPrimaryAttack{ "DT_BaseCombatWeapon", "LocalActiveWeaponData", "m_flNextPrimaryAttack" };
    lazy_netvar flNextSecondaryAttack{ "DT_BaseCombatWeapon", "LocalActiveWeaponData", "m_flNextSecondaryAttack" };
    lazy_netvar iNextThinkTick{ "DT_BaseCombatWeapon", "LocalActiveWeaponData", "m_nNextThinkTick" };
    lazy_netvar m_iClip1{ "DT_BaseCombatWeapon", "LocalWeaponData", "m_iClip1" };
    lazy_netvar m_iClip2{ "DT_BaseCombatWeapon", "LocalWeaponData", "m_iClip2" };
    lazy_netvar nTickBase{ "DT_BasePlayer", "localdata", "m_nTickBase" };
    lazy_netvar res_iMaxBuffedHealth{ "DT_TFPlayerResource", "m_iMaxBuffedHealth" };
    lazy_netvar iItemDefinitionIndex{ "DT_EconEntity", "m_AttributeManager", "m_Item", "m_iItemDefinitionIndex" };
    lazy_netvar AttributeList{ "DT_EconEntity", "m_AttributeManager", "m_Item", "m_AttributeList" };

    lazy_netvar vecPunchAngle{ "DT_BasePlayer", "localdata", "m_Local", "m_vecPunchAngle" };
    lazy_netvar vecPunchAngleVel{ "DT_BasePlayer", "localdata", "m_Local", "m_vecPunchAngleVel" };
    lazy_netvar iObserverMode{ "DT_BasePlayer", "m_iObserverMode" };
    lazy_netvar hObserverTarget{ "DT_BasePlayer", "m_hObserverTarget" };
    lazy_netvar flChargeBeginTime{ "DT_WeaponPipebombLauncher", "PipebombLauncherLocalData", "m_flChargeBeginTime" };
    lazy_netvar flDetonateTime{ "DT_WeaponGrenadeLauncher", "m_flDetonateTime" };
    lazy_netvar flLastFireTime{ "DT_TFWeaponBase", "LocalActiveTFWeaponData", "m_flLastFireTime" };
    lazy_netvar flObservedCritChance{ "DT_TFWeaponBase", "LocalActiveTFWeaponData", "m_flObservedCritChance" };
    lazy_netvar hThrower{ "DT_BaseGrenade", "m_hThrower" };
    lazy_netvar hMyWeapons{ "DT_BaseCombatCharacter", "m_hMyWeapons" };

    lazy_netvar Rocket_iDeflected{ "DT_TFBaseRocket", "m_iDeflected" };
    lazy_netvar Grenade_iDeflected{ "DT_TFWeaponBaseGrenadeProj", "m_iDeflected" };
    lazy_netvar Rocket_bCritical{ "DT_TFProjectile_Rocket", "m_bCritical" };
    lazy_netvar Grenade_bCritical{ "DT_TFWeaponBaseGrenadeProj", "m_bCritical" };
    lazy_netvar m_DmgRadius{ "DT_BaseGrenade", "m_DmgRadius" };
    lazy_netvar bDistributed{ "DT_CurrencyPack", "m_bDistributed" };
    lazy_netvar angEyeAngles{ "DT_TFPlayer", "tfnonlocaldata", "m_angEyeAngles[0]" };
    lazy_netvar deadflag{ "DT_BasePlayer", "pl", "deadflag" };
    lazy_netvar nForceTauntCam{ "DT_TFPlayer", "m_nForceTauntCam" };
    lazy_netvar iDefaultFOV{ "DT_BasePlayer", "m_iDefaultFOV" };
    lazy_netvar iFOV{ "DT_BasePlayer", "m_iFOV" };
    lazy_netvar _condition_bits{ "DT_TFPlayer", "m_Shared", "m_ConditionList", "_condition_bits" };
    lazy_netvar res_iPlayerClass{ "DT_TFPlayerResource", "m_iPlayerClass" };
    lazy_netvar hOwner{ "DT_BaseCombatWeapon", "m_hOwner" };
    lazy_netvar iWeaponState{ "DT_WeaponMinigun", "m_iWeaponState" };
    lazy_netvar iCritMult{ "DT_TFPlayer", "m_Shared", "m_iCritMult" };
    lazy_netvar flChargeLevel{ "DT_WeaponMedigun", "NonLocalTFWeaponMedigunData", "m_flChargeLevel" };
    lazy_netvar bChargeRelease{ "DT_WeaponMedigun", "m_bChargeRelease" };
    lazy_netvar m_flStealthNoAttackExpire{ "DT_TFPlayer", "m_Shared", "tfsharedlocaldata", "m_flStealthNoAttackExpire" };
    lazy_netvar m_iCrits{ "DT_TFPlayer", "m_Shared", "tfsharedlocaldata", "m_RoundScoreData", "m_iCrits" };
    lazy_netvar m_flDuckTimer{ "DT_TFPlayer", "m_Shared", "m_flDuckTimer" };
    lazy_netvar m_bDucked{ "DT_TFPlayer", "localdata", "m_Local", "m_bDucked" };
    lazy_netvar m_angEyeAngles{ "DT_TFPlayer", "tfnonlocaldata", "m_angEyeAngles[0]" };
    lazy_netvar m_bReadyToBackstab{ "DT_TFWeaponKnife", "m_bReadyToBackstab" };
    lazy_netvar m_Collision{ "DT_BaseEntity", "m_Collision" };
    lazy_netvar moveparent{ "DT_BaseEntity", "moveparent" };
    lazy_netvar res_iTeam{ "DT_TFPlayerResource", "baseclass", "m_iTeam" };
    lazy_netvar res_iScore{ "DT_TFPlayerResource", "baseclass", "m_iScore" };
    lazy_netvar res_bAlive{ "DT_TFPlayerResource", "baseclass", "m_bAlive" };
    lazy_netvar res_bValid{ "DT_TFPlayerResource", "baseclass", "m_bValid" };
    lazy_netvar m_nChargeResistType{ "DT_WeaponMedigun", "m_nChargeResistType" };
    lazy_netvar m_hHealingTarget{ "DT_WeaponMedigun", "m_hHealingTarget" };
    lazy_netvar m_flChargeLevel{ "DT_WeaponMedigun", "NonLocalTFWeaponMedigunData", "m_flChargeLevel" };
    lazy_netvar m_flChargeMeter{ "DT_TFPlayer", "m_Shared", "m_flChargeMeter" };

    lazy_netvar m_nCurrency{ "DT_TFPlayer", "m_nCurrency" };
    lazy_netvar m_bUsingActionSlot{ "DT_TFPlayer", "m_bUsingActionSlot" };
    lazy_netvar m_hConstraintEntity{ "DT_BasePlayer", "m_hConstraintEntity" };
    lazy_netvar m_bFeignDeathReady{ "DT_TFPlayer", "m_Shared", "m_bFeignDeathReady" };
    lazy_netvar m_bCarryingObject{ "DT_TFPlayer", "m_Shared", "m_bCarryingObject" };
    lazy_netvar m_hCarriedObject{ "DT_TFPlayer", "m_Shared", "m_hCarriedObject" };
    lazy_netvar m_iTauntConcept{ "DT_TFPlayer", "m_Shared", "m_iTauntConcept" };
    lazy_netvar m_iTauntIndex{ "DT_TFPlayer", "m_Shared", "m_iTauntIndex" };
    lazy_netvar m_bViewingCYOAPDA{ "DT_TFPlayer", "m_bViewingCYOAPDA" };
    lazy_netvar m_angEyeAnglesLocal{ "DT_TFPlayer", "tflocaldata", "m_angEyeAngles[0]" };
    lazy_netvar m_nSequence{ "DT_BaseAnimating", "m_nSequence" };
    lazy_netvar m_flPoseParameter{ "DT_BaseAnimating", "m_flPoseParameter" };
    lazy_netvar m_flModelScale{ "DT_BaseAnimating", "m_flModelScale" };
    lazy_netvar m_flEncodedController{ "DT_BaseAnimating", "m_flEncodedController" };
    lazy_netvar m_flSimulationTime{ "DT_BaseEntity", "m_flSimulationTime" };
    lazy_netvar m_flAnimTime{ "DT_BaseEntity", "AnimTimeMustBeFirst", "m_flAnimTime" };
    lazy_netvar m_flCycle{ "DT_BaseAnimating", "serveranimdata", "m_flCycle" };
    lazy_netvar m_angRotation{ "DT_BaseEntity", "m_angRotation" };
    lazy_netvar m_hOwnerEntity{ "DT_BaseEntity", "m_hOwnerEntity" };
    lazy_netvar m_hOriginalLauncher{ "DT_BaseProjectile", "m_hOriginalLauncher" };

    lazy_netvar m_nStreaks_Player{ "DT_TFPlayer", "m_Shared", "m_nStreaks" };
    lazy_netvar m_nStreaks_Resource{ "DT_TFPlayerResource", "m_iStreaks" };
    lazy_netvar m_iPing_Resource{ "DT_TFPlayerResource", "baseclass", "m_iPing" };
    lazy_netvar m_iKills_Resource{ "DT_TFPlayerResource", "baseclass", "m_iScore" };
    lazy_netvar m_iDeaths_Resource{ "DT_TFPlayerResource", "baseclass", "m_iDeaths" };
    lazy_netvar m_iHealth_Resource{ "DT_TFPlayerResource", "baseclass", "m_iHealth" };
    lazy_netvar m_iTotalScore_Resource{ "DT_TFPlayerResource", "m_iTotalScore" };
    lazy_netvar m_iMaxHealth_Resource{ "DT_TFPlayerResource", "m_iMaxHealth" };
    lazy_netvar m_iMaxBuffedHealth_Resource{ "DT_TFPlayerResource", "m_iMaxBuffedHealth" };
    lazy_netvar m_iPlayerClass_Resource{ "DT_TFPlayerResource", "m_iPlayerClass" };
    lazy_netvar m_iActiveDominations_Resource{ "DT_TFPlayerResource", "m_iActiveDominations" };
    lazy_netvar m_flNextRespawnTime_Resource{ "DT_TFPlayerResource", "m_flNextRespawnTime" };
    lazy_netvar m_iDamage_Resource{ "DT_TFPlayerResource", "m_iDamage" };
    lazy_netvar m_iAccountID_Resource{ "DT_TFPlayerResource", "baseclass", "m_iAccountID" };
    lazy_netvar m_iDamageAssist_Resource{ "DT_TFPlayerResource", "m_iDamageAssist" };
    lazy_netvar m_iHealing_Resource{ "DT_TFPlayerResource", "m_iHealing" };
    lazy_netvar m_iHealingAssist_Resource{ "DT_TFPlayerResource", "m_iHealingAssist" };
    lazy_netvar m_iPlayerLevel_Resource{ "DT_TFPlayerResource", "m_iPlayerLevel" };

    lazy_netvar m_nFlagType{ "DT_CaptureFlag", "m_nType" };
    lazy_netvar m_nFlagStatus{ "DT_CaptureFlag", "m_nFlagStatus" };
    lazy_netvar m_bTeamCanCap{ "DT_BaseTeamObjectiveResource", "m_bTeamCanCap" };
    lazy_netvar m_iNumControlPoints{ "DT_BaseTeamObjectiveResource", "m_iNumControlPoints" };
    lazy_netvar m_vCPPositions{ "DT_BaseTeamObjectiveResource", "m_vCPPositions[0]" };
    lazy_netvar m_iOwningTeam{ "DT_BaseTeamObjectiveResource", "m_iOwner" };
    lazy_netvar m_bCPLocked{ "DT_BaseTeamObjectiveResource", "m_bCPLocked" };
    lazy_netvar m_bPlayingMiniRounds{ "DT_BaseTeamObjectiveResource", "m_bPlayingMiniRounds" };
    lazy_netvar m_bInMiniRound{ "DT_BaseTeamObjectiveResource", "m_bInMiniRound" };
    lazy_netvar m_iPreviousPoints{ "DT_BaseTeamObjectiveResource", "m_iPreviousPoints" };
    lazy_netvar m_iBaseControlPoints{ "DT_BaseTeamObjectiveResource", "m_iBaseControlPoints" };
    lazy_netvar m_iPlayerIndex{ "DT_TFRagdoll", "m_iPlayerIndex" };
    lazy_netvar m_hTargetPlayer{ "DT_CHalloweenGiftPickup", "m_hTargetPlayer" };
    lazy_netvar m_flResetTime{ "DT_CaptureFlag", "m_flResetTime" };
    lazy_netvar m_flMaxspeed{ "DT_BasePlayer", "m_flMaxspeed" };
    lazy_netvar m_Shared{ "DT_TFPlayer", "m_Shared" };
    lazy_netvar m_iCritMult{ "DT_TFPlayer", "m_Shared", "m_iCritMult" };
    lazy_netvar m_iRoundState{ "DT_TFGameRulesProxy", "m_iRoundState" };
    lazy_netvar m_bInSetup{ "DT_TFGameRulesProxy", "m_bInSetup" };
    lazy_netvar m_bInWaitingForPlayers{ "DT_TFGameRulesProxy", "m_bInWaitingForPlayers" };
    lazy_netvar m_bPlayingSpecialDeliveryMode{ "DT_TFGameRulesProxy", "m_bPlayingSpecialDeliveryMode" };
    lazy_netvar m_iWinningTeam{ "DT_TFGameRulesProxy", "m_iWinningTeam" };
    lazy_netvar m_bPlayingMannVsMachine{ "DT_TFGameRulesProxy", "m_bPlayingMannVsMachine" };
    lazy_netvar m_halloweenScenario{ "DT_TFGameRulesProxy", "m_halloweenScenario" };
    lazy_netvar m_bIsUsingSpells{ "DT_TFGameRulesProxy", "m_bIsUsingSpells" };

    lazy_proxy movetype{ "DT_BaseEntity", "movetype" };

    lazy_datamap m_pCurrentCommand{ "m_pCurrentCommand" };
    lazy_datamap m_rgflCoordinateFrame{ "m_rgflCoordinateFrame" };
    lazy_datamap m_pStudioHdr{ "m_pStudioHdr" };
    lazy_datamap m_pIk{ "m_pIk" };
    lazy_datamap m_AnimOverlay{ "m_AnimOverlay" };
    lazy_datamap m_iEFlags{ "m_iEFlags" };
    lazy_datamap m_iWeaponMode{ "m_iWeaponMode" };
    lazy_datamap m_pWeaponInfo{ "m_pWeaponInfo" };
    lazy_datamap m_bCurrentAttackIsCrit{ "m_bCurrentAttackIsCrit" };
    lazy_datamap m_bCurrentCritIsRandom{ "m_bCurrentCritIsRandom" };
    lazy_datamap m_flCritTime{ "m_flCritTime" };
    lazy_datamap m_iLastCritCheckFrame{ "m_iLastCritCheckFrame" };
    lazy_datamap m_iCurrentSeed{ "m_iCurrentSeed" };
    lazy_datamap m_flCritTokenBucket{ "m_flCritTokenBucket" };
    lazy_datamap m_nCritChecks{ "m_nCritChecks" };
    lazy_datamap m_nCritSeedRequests{ "m_nCritSeedRequests" };
    lazy_datamap m_surfaceFriction{ "m_surfaceFriction" };
};

extern NetVars netvar;
