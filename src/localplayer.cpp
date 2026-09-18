/*
 * localplayer.cpp
 *
 *  Created on: Oct 15, 2016
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "AntiAim.hpp"

CatCommand printfov("fov_print", "Dump achievements to file (development)",
                    []()
                    {
                        if (CE_GOOD(LOCAL_E))
                            logging::Info("%d", CE_INT(LOCAL_E, netvar.iFOV));
                    });
weaponmode GetWeaponModeloc()
{
    int weapon_handle, weapon_idx, slot;
    CachedEntity *weapon;

    if (CE_BAD(LOCAL_E) | CE_BAD(LOCAL_W))
        return weapon_invalid;
    weapon_handle = CE_INT(LOCAL_E, netvar.hActiveWeapon);
    weapon_idx    = HandleToIDX(weapon_handle);
    if (IDX_BAD(weapon_idx))
    {
        // logging::Info("IDX_BAD: %i", weapon_idx);
        return weaponmode::weapon_invalid;
    }
    weapon = (ENTITY(weapon_idx));
    if (CE_BAD(weapon))
        return weaponmode::weapon_invalid;
    int classid = weapon->m_iClassID();
    slot        = re::C_BaseCombatWeapon::GetSlot(RAW_ENT(weapon));
    if (slot == 2)
        return weaponmode::weapon_melee;
    if (slot > 2)
        return weaponmode::weapon_pda;
    if (class_is(classid, CL_CLASS(CTFLunchBox), CL_CLASS(CTFLunchBox_Drink), CL_CLASS(CTFBuffItem)))
        return weaponmode::weapon_consumable;
    if (class_is(classid, CL_CLASS(CTFRocketLauncher_DirectHit), CL_CLASS(CTFRocketLauncher), CL_CLASS(CTFParticleCannon), CL_CLASS(CTFGrenadeLauncher), CL_CLASS(CTFPipebombLauncher), CL_CLASS(CTFCompoundBow), CL_CLASS(CTFBat_Wood), CL_CLASS(CTFBat_Giftwrap), CL_CLASS(CTFFlareGun), CL_CLASS(CTFFlareGun_Revenge), CL_CLASS(CTFSyringeGun), CL_CLASS(CTFCrossbow), CL_CLASS(CTFShotgunBuildingRescue), CL_CLASS(CTFDRGPomson), CL_CLASS(CTFWeaponFlameBall), CL_CLASS(CTFRaygun), CL_CLASS(CTFGrapplingHook), CL_CLASS(CTFRocketLauncher_AirStrike), CL_CLASS(CTFCannon), CL_CLASS(CTFMechanicalArm)))
        return weaponmode::weapon_projectile;
    if (class_is(classid, CL_CLASS(CTFJar), CL_CLASS(CTFJarMilk), CL_CLASS(CTFJarGas), CL_CLASS(CTFCleaver)))
        return weaponmode::weapon_throwable;
    if (classid == CL_CLASS(CWeaponMedigun))
        return weaponmode::weapon_medigun;
    if (class_is(classid, CL_CLASS(CTFWeaponSapper), CL_CLASS(CTFWeaponPDA), CL_CLASS(CTFWeaponPDA_Spy), CL_CLASS(CTFWeaponPDA_Engineer_Build), CL_CLASS(CTFWeaponPDA_Engineer_Destroy), CL_CLASS(CTFWeaponBuilder), CL_CLASS(CTFLaserPointer)))
        return weaponmode::weapon_pda;
    return weaponmode::weapon_hitscan;
}
void LocalPlayer::Update()
{
    CachedEntity *wep;

    entity_idx = g_IEngine->GetLocalPlayer();
    entity     = ENTITY(entity_idx);
    if (CE_BAD(entity))
    {
        team = 0;
        return;
    }
    holding_sniper_rifle     = false;
    holding_sapper           = false;
    weapon_melee_damage_tick = false;
    bRevving                 = false;
    bRevved                  = false;
    wep                      = weapon();
    if (CE_GOOD(wep))
    {
        weapon_mode = GetWeaponModeloc();
        if (wep->m_iClassID() == CL_CLASS(CTFSniperRifle) || wep->m_iClassID() == CL_CLASS(CTFSniperRifleDecap))
            holding_sniper_rifle = true;
        else if (wep->m_iClassID() == CL_CLASS(CTFWeaponBuilder) || wep->m_iClassID() == CL_CLASS(CTFWeaponSapper))
            holding_sapper = true;
        else if (wep->m_iClassID() == CL_CLASS(CTFMinigun))
        {
            if (CE_INT(LOCAL_W, netvar.iWeaponState) == 2 || CE_INT(LOCAL_W, netvar.iWeaponState) == 1)
                bRevving = true;
            else if (CE_INT(LOCAL_W, netvar.iWeaponState) == 3)
                bRevved = true;
        }
        // Detect when a melee hit will result in damage, useful for aimbot and antiaim
        if (CE_FLOAT(wep, netvar.flNextPrimaryAttack) > g_GlobalVars->curtime && weapon_mode == weapon_melee)
        {
            if (melee_damagetick == tickcount)
            {
                weapon_melee_damage_tick = true;
                melee_damagetick         = ULONG_MAX;
            }
            else if (bAttackLastTick && (long long) melee_damagetick - (long long) tickcount < 0)
                // Thirteen ticks after attack detection = damage tick
                melee_damagetick = tickcount + TIME_TO_TICKS(0.195f);
        }
        else
            melee_damagetick = 0;
    }
    team                   = CE_INT(entity, netvar.iTeamNum);
    life_state             = CE_BYTE(entity, netvar.iLifeState);
    v_ViewOffset           = CE_VECTOR(entity, netvar.vViewOffset);
    v_Origin               = entity->m_vecOrigin();
    v_OrigViewangles       = current_user_cmd->viewangles;
    v_Eye                  = v_Origin + v_ViewOffset;
    clazz                  = CE_INT(entity, netvar.iClass);
    health                 = CE_INT(entity, netvar.iHealth);
    this->bUseSilentAngles = false;
    bZoomed                = HasCondition<TFCond_Zoomed>(entity);
    if (bZoomed)
    {
        if (flZoomBegin == 0.0f)
            flZoomBegin = g_GlobalVars->curtime;
    }
    else
    {
        flZoomBegin = 0.0f;
    }
    // Alive
    if (!life_state)
    {
        spectator_state = NONE;
        for (auto const &ent: entity_cache::player_cache)
        {
            // Assign the for loops tick number to an ent
            
            if (!CE_BAD(ent) && (HandleToIDX(CE_INT(ent, netvar.hObserverTarget))) == LOCAL_E->m_IDX)
            {
                auto mode = CE_INT(ent, netvar.iObserverMode);
                if (mode == 4)
                {
                    spectator_state = FIRSTPERSON;
                    // FIRSTPERSON is the "worst" state, exit
                    return;
                }
                if (mode == 5)
                {
                    spectator_state = THIRDPERSON;
                }
            }
        }
    }
}

void LocalPlayer::UpdateEye()
{
    v_Eye = entity->m_vecOrigin() + CE_VECTOR(entity, netvar.vViewOffset);
}

void LocalPlayer::UpdateEnd()
{
    if (!isFakeAngleCM)
        realAngles = current_user_cmd->viewangles;
    bAttackLastTick = (current_user_cmd->buttons & IN_ATTACK);
}

CachedEntity *LocalPlayer::weapon()
{
    int handle, eid;

    if (CE_BAD(entity))
        return 0;
    handle = CE_INT(entity, netvar.hActiveWeapon);
    eid    = HandleToIDX(handle);
    if (IDX_BAD(eid))
        return 0;
    return ENTITY(eid);
}

LocalPlayer *g_pLocalPlayer = 0;
