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
constexpr offset_t k_flCritTokenBucket              = 3716;
constexpr offset_t k_nCritChecks                    = 3720;
constexpr offset_t k_nCritSeedRequests              = 3724;
constexpr offset_t k_iWeaponMode                    = 3952;
constexpr offset_t k_pWeaponInfo                    = 3968;
constexpr offset_t k_bCurrentAttackIsCrit           = 3978;
constexpr offset_t k_bCurrentCritIsRandom           = 3979;
constexpr offset_t k_flCritTime                     = 4004;
constexpr offset_t k_flLastCritCheckTime            = 4008;
constexpr offset_t k_iLastCritCheckFrame            = 4012;
constexpr offset_t k_iCurrentSeed                   = 4016;
constexpr offset_t k_flLastRapidFireCritCheckTime   = 4020;
constexpr offset_t k_flObservedCritChance           = 4208;
constexpr offset_t k_pWeaponInfoMelee               = 4248;
constexpr std::size_t k_weapon_data_array           = 1828;

inline offset_t reload_mode()
{
    return netvar.iReloadMode;
}
inline offset_t weapon_mode()
{
    const int r = netvar.iReloadMode;
    return r ? r - 4 : k_iWeaponMode;
}
inline offset_t crit_bucket()
{
    const int r = netvar.iReloadMode;
    return r ? r - 240 : k_flCritTokenBucket;
}
inline offset_t crit_attempts()
{
    const int r = netvar.iReloadMode;
    return r ? r - 236 : k_nCritChecks;
}
inline offset_t crit_count()
{
    const int r = netvar.iReloadMode;
    return r ? r - 232 : k_nCritSeedRequests;
}
inline offset_t last_crit_check_time()
{
    const int t = netvar.flLastCritCheckTime;
    return t ? t : k_flLastCritCheckTime;
}
inline offset_t crit_time()
{
    const int t = netvar.flLastCritCheckTime;
    return t ? t - 4 : k_flCritTime;
}
inline offset_t last_crit_check_frame()
{
    const int t = netvar.flLastCritCheckTime;
    return t ? t + 4 : k_iLastCritCheckFrame;
}
inline offset_t weapon_seed()
{
    const int t = netvar.flLastCritCheckTime;
    return t ? t + 8 : k_iCurrentSeed;
}
inline offset_t last_rapid_fire_crit_check()
{
    const int t = netvar.flLastCritCheckTime;
    return t ? t + 12 : k_flLastRapidFireCritCheckTime;
}
inline offset_t current_attack_is_crit()
{
    return k_bCurrentAttackIsCrit;
}
inline offset_t current_crit_is_random()
{
    return k_bCurrentCritIsRandom;
}
inline offset_t weapon_info()
{
    const int r = netvar.iReloadMode;
    return r ? r + 12 : k_pWeaponInfo;
}
inline offset_t weapon_info_melee()
{
    return k_pWeaponInfoMelee;
}
inline offset_t observed_crit_chance()
{
    const int o = netvar.flObservedCritChance;
    return o ? o : k_flObservedCritChance;
}
} // namespace weapon_layout

class weapon_info
{
public:
    float crit_bucket{};
    unsigned int weapon_seed{};
    unsigned last_crit_check_frame{};
    unsigned unknown2{};
    bool unknown3{};
    float m_flCritTime{};
    int crit_attempts{};
    int crit_count{};
    float observed_crit_chance{};
    bool unknown7{};
    int weapon_mode{};
    uintptr_t weapon_data{};
    float last_rapid_fire_crit_check{};
    weapon_info() = default;
    void Load(IClientEntity *weapon)
    {
        if (!weapon)
            return;
        if (auto off = weapon_layout::crit_bucket())
            crit_bucket = *(float *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::weapon_seed())
            weapon_seed = *(unsigned int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::last_crit_check_frame())
            last_crit_check_frame = *(unsigned int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::current_attack_is_crit())
            unknown3 = *(bool *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::crit_time())
            m_flCritTime = *(float *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::crit_attempts())
            crit_attempts = *(int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::crit_count())
            crit_count = *(int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::observed_crit_chance())
            observed_crit_chance = *(float *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::current_crit_is_random())
            unknown7 = *(bool *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::weapon_mode())
            weapon_mode = *(int *) ((uintptr_t) weapon + off);
        if (auto off = weapon_layout::last_rapid_fire_crit_check())
            last_rapid_fire_crit_check = *(float *) ((uintptr_t) weapon + off);

        uintptr_t info_ptr = 0;
        const bool melee   = re::C_TFWeaponBase::IsMeleeWeapon(weapon);
        if (melee)
        {
            if (auto off = weapon_layout::weapon_info_melee())
                info_ptr = *reinterpret_cast<uintptr_t *>((uintptr_t) weapon + off);
        }
        if (!info_ptr)
        {
            if (auto off = weapon_layout::weapon_info())
                info_ptr = *reinterpret_cast<uintptr_t *>((uintptr_t) weapon + off);
        }
        weapon_data = info_ptr;
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
        if (auto off = weapon_layout::last_crit_check_frame())
            *(unsigned int *) ((uintptr_t) weapon + off) = last_crit_check_frame;
        if (auto off = weapon_layout::current_attack_is_crit())
            *(bool *) ((uintptr_t) weapon + off) = unknown3;
        if (auto off = weapon_layout::crit_time())
            *(float *) ((uintptr_t) weapon + off) = m_flCritTime;
        if (auto off = weapon_layout::crit_attempts())
            *(int *) ((uintptr_t) weapon + off) = crit_attempts;
        if (auto off = weapon_layout::crit_count())
            *(int *) ((uintptr_t) weapon + off) = crit_count;
        if (auto off = weapon_layout::observed_crit_chance())
            *(float *) ((uintptr_t) weapon + off) = observed_crit_chance;
        if (auto off = weapon_layout::current_crit_is_random())
            *(bool *) ((uintptr_t) weapon + off) = unknown7;
        if (auto off = weapon_layout::weapon_mode())
            *(int *) ((uintptr_t) weapon + off) = weapon_mode;
        if (auto off = weapon_layout::last_rapid_fire_crit_check())
            *(float *) ((uintptr_t) weapon + off) = last_rapid_fire_crit_check;
    }
    void restore_prediction_state(IClientEntity *weapon)
    {
        if (!weapon)
            return;
        if (auto off = weapon_layout::crit_bucket())
            *(float *) ((uintptr_t) weapon + off) = crit_bucket;
        if (auto off = weapon_layout::crit_attempts())
            *(int *) ((uintptr_t) weapon + off) = crit_attempts;
        if (auto off = weapon_layout::crit_count())
            *(int *) ((uintptr_t) weapon + off) = crit_count;
        if (auto off = weapon_layout::last_rapid_fire_crit_check())
            *(float *) ((uintptr_t) weapon + off) = last_rapid_fire_crit_check;
        if (auto off = weapon_layout::crit_time())
            *(float *) ((uintptr_t) weapon + off) = m_flCritTime;
        if (auto off = weapon_layout::weapon_seed())
            *(unsigned int *) ((uintptr_t) weapon + off) = weapon_seed;
    }
    bool operator==(const weapon_info &B) const
    {
        return crit_bucket == B.crit_bucket && weapon_seed == B.weapon_seed && last_crit_check_frame == B.last_crit_check_frame && unknown3 == B.unknown3 && m_flCritTime == B.m_flCritTime && crit_attempts == B.crit_attempts && crit_count == B.crit_count && observed_crit_chance == B.observed_crit_chance && unknown7 == B.unknown7 && last_rapid_fire_crit_check == B.last_rapid_fire_crit_check;
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
    static std::size_t weapon_data_array = weapon_layout::k_weapon_data_array;
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
    return reinterpret_cast<WeaponData_t *>(info.weapon_data + weapon_data_array);
}
