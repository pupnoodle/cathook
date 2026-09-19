#pragma once

#include "common.hpp"

struct WeaponData_t
{
    int m_nDamage;
    int m_nBulletsPerShot;
    float m_flRange;
    float m_flSpread;
    float m_flPunchAngle;
    float m_flTimeFireDelay;
    float m_flTimeIdle;
    float m_flTimeIdleEmpty;
    float m_flTimeReloadStart;
    float m_flTimeReload;
    bool m_bDrawCrosshair;
    int m_iProjectile;
    int m_iAmmoPerShot;
    float m_flProjectileSpeed;
    float m_flSmackDelay;
    bool m_bUseRapidFireCrits;
};
static_assert(sizeof(WeaponData_t) == 64, "WeaponData_t must match TF2 FileWeaponInfo stride");

namespace weapon_layout
{
inline offset_t reload_mode()
{
    return netvar.iReloadMode;
}
inline offset_t weapon_mode()
{
    return netvar.m_iWeaponMode;
}
inline offset_t crit_bucket()
{
    return netvar.m_flCritTokenBucket;
}
inline offset_t crit_attempts()
{
    return netvar.m_nCritChecks;
}
inline offset_t crit_count()
{
    return netvar.m_nCritSeedRequests;
}
inline offset_t last_crit_check_time()
{
    return netvar.flLastCritCheckTime;
}
inline offset_t crit_time()
{
    return netvar.m_flCritTime;
}
inline offset_t last_crit_check_frame()
{
    return netvar.m_iLastCritCheckFrame;
}
inline offset_t weapon_seed()
{
    return netvar.m_iCurrentSeed;
}
inline offset_t current_attack_is_crit()
{
    return netvar.m_bCurrentAttackIsCrit;
}
inline offset_t current_crit_is_random()
{
    return netvar.m_bCurrentCritIsRandom;
}
} // namespace weapon_layout

class weapon_info
{
public:
    float crit_bucket{};
    unsigned int weapon_seed{};
    unsigned unknown1{};
    unsigned unknown2{};
    bool unknown3{};
    float m_flCritTime{};
    int crit_attempts{};
    int crit_count{};
    float observed_crit_chance{};
    bool unknown7{};
    int weapon_mode{};
    uintptr_t weapon_data{};
    weapon_info() = default;
    void Load(IClientEntity *weapon)
    {
        if (!weapon)
            return;
        if (auto off = weapon_layout::crit_bucket())
            crit_bucket = *(float *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::weapon_seed())
            weapon_seed = *(unsigned int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::current_attack_is_crit())
            unknown3 = *(bool *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::crit_time())
            m_flCritTime = *(float *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::crit_attempts())
            crit_attempts = *(int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::crit_count())
            crit_count = *(int *) ((uintptr_t) weapon + off);
        if (netvar.flObservedCritChance)
            observed_crit_chance = *(float *) ((uintptr_t) weapon + netvar.flObservedCritChance);
        if (auto off = weapon_layout::current_crit_is_random())
            unknown7 = *(bool *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::weapon_mode())
            weapon_mode = *(int *) ((uintptr_t) weapon + off);
        if (netvar.m_pWeaponInfo)
            weapon_data = *reinterpret_cast<uintptr_t *>((uintptr_t) weapon + netvar.m_pWeaponInfo);
    }
    weapon_info(IClientEntity *weapon)
    {
        Load(weapon);
    }
    void restore_data(IClientEntity *weapon)
    {
        if (!weapon)
            return;
        if (auto off = weapon_layout::crit_bucket())
            *(float *) ((uintptr_t) weapon + off) = crit_bucket;
        if (auto off = weapon_layout::weapon_seed())
            *(unsigned int *) ((uintptr_t) weapon + off) = weapon_seed;
        if (auto off = weapon_layout::current_attack_is_crit())
            *(bool *) ((uintptr_t) weapon + off) = unknown3;
        if (auto off = weapon_layout::crit_time())
            *(float *) ((uintptr_t) weapon + off) = m_flCritTime;
        if (auto off = weapon_layout::crit_attempts())
            *(int *) ((uintptr_t) weapon + off) = crit_attempts;
        if (auto off = weapon_layout::crit_count())
            *(int *) ((uintptr_t) weapon + off) = crit_count;
        if (netvar.flObservedCritChance)
            *(float *) ((uintptr_t) weapon + netvar.flObservedCritChance) = observed_crit_chance;
        if (auto off = weapon_layout::current_crit_is_random())
            *(bool *) ((uintptr_t) weapon + off) = unknown7;
    }
    bool operator==(const weapon_info &B) const
    {
        return crit_bucket == B.crit_bucket && weapon_seed == B.weapon_seed && unknown1 == B.unknown1 && unknown2 == B.unknown2 && unknown3 == B.unknown3 && m_flCritTime == B.m_flCritTime && crit_attempts == B.crit_attempts && crit_count == B.crit_count && observed_crit_chance == B.observed_crit_chance && unknown7 == B.unknown7;
    }
    bool operator!=(const weapon_info &B) const
    {
        return !(*this == B);
    }
};

inline WeaponData_t *GetWeaponData(IClientEntity *weapon)
{
    static WeaponData_t dummy{};
    if (!weapon)
        return &dummy;
    weapon_info info(weapon);
    if (!info.weapon_data)
        return &dummy;
    int mode = info.weapon_mode;
    if (mode < 0)
        mode = 0;
    if (mode > 1)
        mode = 1;
    static std::size_t weapon_data_array = 1828;
    static bool parsed                   = false;
    if (!parsed)
    {
        parsed = true;
        if (auto *p = reinterpret_cast<std::uint8_t *>(gSignatures.GetClientSignature(sigs::tf_weapon_info_primary_data)))
        {
            if (p[0] == 0x48 && p[1] == 0xC7 && p[2] == 0x83)
            {
                int disp = *reinterpret_cast<int *>(p + 3);
                if (disp > 0x100 && disp < 0x4000)
                    weapon_data_array = std::size_t(disp);
            }
        }
        logging::Info("WeaponData array off=%zu", weapon_data_array);
    }
    return reinterpret_cast<WeaponData_t *>(info.weapon_data + weapon_data_array + sizeof(WeaponData_t) * mode);
}
