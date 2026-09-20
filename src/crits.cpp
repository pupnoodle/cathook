#include "common.hpp"
#include "crits.hpp"
#include "WeaponData.hpp"
#include "AntiCheatBypass.hpp"
#include "DetourHook.hpp"

#include <vstdlib/random.h>

boost::unordered_flat_map<int, int> command_number_mod{};

namespace
{
constexpr int k_ntab = 32;
constexpr int k_ia   = 16807;
constexpr int k_im   = 2147483647;
constexpr int k_iq   = 127773;
constexpr int k_ir   = 2836;
constexpr int k_ndiv = 1 + (k_im - 1) / k_ntab;

struct ValveRandom
{
    int idum{};
    int iy{};
    int iv[k_ntab]{};

    void SetSeed(int seed)
    {
        idum = seed < 0 ? seed : -seed;
        iy   = 0;
    }
    int Generate()
    {
        int j, k;
        if (idum <= 0 || !iy)
        {
            if (-idum < 1)
                idum = 1;
            else
                idum = -idum;
            for (j = k_ntab + 7; j >= 0; j--)
            {
                k    = idum / k_iq;
                idum = k_ia * (idum - k * k_iq) - k_ir * k;
                if (idum < 0)
                    idum += k_im;
                if (j < k_ntab)
                    iv[j] = idum;
            }
            iy = iv[0];
        }
        k    = idum / k_iq;
        idum = k_ia * (idum - k * k_iq) - k_ir * k;
        if (idum < 0)
            idum += k_im;
        j = iy / k_ndiv;
        if (j >= k_ntab || j < 0)
            j = (j % k_ntab) & 0x7fffffff;
        iy    = iv[j];
        iv[j] = idum;
        return iy;
    }
    int RandomInt(int low, int high)
    {
        unsigned x = unsigned(high - low + 1);
        if (x <= 1 || high < low)
            return low;
        unsigned max_ok = 0x7fffffffu - ((0x7fffffffu + 1u) % x);
        unsigned n;
        do
            n = unsigned(Generate());
        while (n > max_ok);
        return low + int(n % x);
    }
};
}

namespace criticals
{
settings::Boolean enabled{ "crit.enabled", "false" };
settings::Boolean melee{ "crit.melee", "false" };
static settings::Button crit_key{ "crit.key", "<null>" };
static settings::Boolean force_no_crit{ "crit.anti-crit", "true" };

static settings::Boolean draw{ "crit.info", "false" };
static settings::Boolean draw_meter{ "crit.draw-meter", "false" };
static settings::Int draw_string_x{ "crit.draw-info.x", "8" };
static settings::Int draw_string_y{ "crit.draw-info.y", "800" };
static settings::Int size{ "crit.bar-size", "100" };
static settings::Int bar_x{ "crit.bar-x", "50" };
static settings::Int bar_y{ "crit.bar-y", "500" };
static settings::Boolean debug_desync{ "crit.desync-debug", "false" };

static constexpr int WEAPON_RANDOM_RANGE           = 10000;
static constexpr float TF_DAMAGE_CRIT_MULTIPLIER   = 3.0f;
static constexpr float TF_DAMAGE_CRIT_CHANCE       = 0.02f;
static constexpr float TF_DAMAGE_CRIT_CHANCE_RAPID = 0.02f;
static constexpr float TF_DAMAGE_CRIT_CHANCE_MELEE = 0.15f;
static constexpr float TF_DAMAGE_CRIT_DURATION_RAPID = 2.0f;
static constexpr int SEED_ATTEMPTS                 = 4096;
static constexpr int BUCKET_ATTEMPTS               = 1000;

enum
{
    kTF_WEAPON_KNIFE                 = 7,
    kTF_WEAPON_SNIPERRIFLE           = 17,
    kTF_WEAPON_PDA                   = 45,
    kTF_WEAPON_PDA_ENGINEER_BUILD    = 46,
    kTF_WEAPON_PDA_ENGINEER_DESTROY  = 47,
    kTF_WEAPON_PDA_SPY               = 48,
    kTF_WEAPON_BUILDER               = 49,
    kTF_WEAPON_MEDIGUN               = 50,
    kTF_WEAPON_INVIS                 = 57,
    kTF_WEAPON_LUNCHBOX              = 59,
    kTF_WEAPON_JAR                   = 60,
    kTF_WEAPON_COMPOUND_BOW          = 61,
    kTF_WEAPON_BUFF_ITEM             = 62,
    kTF_WEAPON_LASER_POINTER         = 67,
    kTF_WEAPON_JAR_MILK              = 70,
    kTF_WEAPON_SNIPERRIFLE_DECAP     = 77,
    kTF_WEAPON_PDA_SPY_BUILD         = 94,
    kTF_WEAPON_SNIPERRIFLE_CLASSIC   = 99,
    kTF_WEAPON_PASSTIME_GUN          = 102,
    kTF_WEAPON_ROCKETPACK            = 105,
    kTF_WEAPON_JAR_GAS               = 107,
    kTF_WEAPON_FLAME_BALL            = 109
};

static float added_per_shot     = 0.0f;
static float taken_per_crit     = 0.0f;
static float cached_cost        = 0.0f;
static int cached_damage        = 0;
static int crit_damage          = 0;
static int melee_damage         = 0;
static int ranged_damage        = 0;
static int round_damage         = 0;
static bool is_out_of_sync      = false;
static int shots_to_fill_bucket = 0;
static int available_crits      = 0;
static int potential_crits      = 0;
static int next_crit_shots      = 0;
static bool crit_banned         = false;
static float damage_til_flip    = 0.0f;
static float cached_crit_chance = 0.0f;
static float cached_mult_chance = 1.0f;
static bool cached_melee        = false;

bool calling_crithelper = false;
bool force_crit_this_tick = false;
size_t current_index      = 0;
boost::unordered_flat_map<int, std::vector<int>> crit_cmds;

static float getBucketCap()
{
    static ConVar *tf_weapon_criticals_bucket_cap = g_ICvar ? g_ICvar->FindVar("tf_weapon_criticals_bucket_cap") : nullptr;
    return tf_weapon_criticals_bucket_cap ? tf_weapon_criticals_bucket_cap->GetFloat() : 1000.0f;
}

static bool randomCritEnabled()
{
    if (!g_ICvar)
        return false;
    static ConVar *tf_weapon_criticals = g_ICvar->FindVar("tf_weapon_criticals");
    return tf_weapon_criticals && tf_weapon_criticals->GetBool();
}

static float getWithdrawMult(IClientEntity *wep, bool melee_wep)
{
    weapon_info info(wep);
    int call_count  = info.crit_count + 1;
    int crit_checks = info.crit_attempts + 1;
    if (melee_wep)
        return 0.5f;
    return RemapValClamped((float) call_count / (float) crit_checks, 0.1f, 1.f, 1.f, 3.f);
}

float getWithdrawAmount(IClientEntity *wep, bool melee_wep)
{
    float amount = added_per_shot * getWithdrawMult(wep, melee_wep);
    if (isRapidFire(wep))
    {
        amount = taken_per_crit * getWithdrawMult(wep, melee_wep);
        reinterpret_cast<int &>(amount) &= ~1;
    }
    return amount;
}

static bool isAllowedToWithdrawFromBucket(IClientEntity *wep, bool melee_wep, bool add_damage = true)
{
    weapon_info info(wep);
    if (add_damage && info.crit_bucket < getBucketCap())
    {
        info.crit_bucket += added_per_shot;
        info.crit_bucket = std::min(info.crit_bucket, getBucketCap());
    }
    return getWithdrawAmount(wep, melee_wep) <= info.crit_bucket;
}

static int shotsUntilCrit(IClientEntity *wep, bool melee_wep)
{
    weapon_info sim(wep);
    const float cap = getBucketCap();
    int shots       = 0;
    for (; shots < shots_to_fill_bucket + 1 && shots < BUCKET_ATTEMPTS; shots++)
    {
        float bucket = sim.crit_bucket;
        if (bucket < cap)
        {
            bucket += added_per_shot;
            bucket = std::min(bucket, cap);
        }
        if (getWithdrawAmount(wep, melee_wep) <= bucket)
            break;
        sim.crit_bucket = bucket;
        sim.crit_attempts++;
    }
    return shots;
}

static float getCritCap(IClientEntity *wep, bool melee_wep)
{
    float crit_mult = re::CTFPlayerShared::GetCritMult(re::CTFPlayerShared::GetPlayerShared(RAW_ENT(LOCAL_E)));
    float chance    = melee_wep ? TF_DAMAGE_CRIT_CHANCE_MELEE : TF_DAMAGE_CRIT_CHANCE;
    float flMultCritChance = ATTRIB_HOOK_FLOAT(crit_mult * chance, "mult_crit_chance", wep, 0, 1);
    if (isRapidFire(wep))
    {
        float flTotalCritChance  = clamp(TF_DAMAGE_CRIT_CHANCE_RAPID * crit_mult, 0.01f, 0.99f);
        float flNonCritDuration  = (TF_DAMAGE_CRIT_DURATION_RAPID / flTotalCritChance) - TF_DAMAGE_CRIT_DURATION_RAPID;
        float flStartCritChance  = 1.0f / flNonCritDuration;
        flMultCritChance         = ATTRIB_HOOK_FLOAT(flStartCritChance, "mult_crit_chance", wep, 0, 1);
    }
    return flMultCritChance;
}

static float getObservedCritChance()
{
    if (ranged_damage <= 0)
        return 0.0f;
    float normalized_damage = (float) crit_damage / TF_DAMAGE_CRIT_MULTIPLIER;
    float denom             = normalized_damage + (float) (ranged_damage - crit_damage);
    if (denom <= 0.0f)
        return 0.0f;
    return normalized_damage / denom;
}

float GetObservedCritChance()
{
    return getObservedCritChance();
}

void SyncObservedChance(IClientEntity *weapon)
{
    if (!weapon)
        return;
    if (auto off = weapon_layout::observed_crit_chance())
        *(float *) ((uintptr_t) weapon + off) = getObservedCritChance();
}

static std::pair<float, float> critMultInfo(IClientEntity *wep, bool melee_wep)
{
    float cur_crit        = getCritCap(wep, melee_wep);
    float observed_chance = 0.0f;
    if (auto off = weapon_layout::observed_crit_chance())
        observed_chance = *(float *) ((uintptr_t) wep + off);
    return { observed_chance, cur_crit + 0.1f };
}

static int damageUntilToCrit(IClientEntity *wep, bool melee_wep)
{
    auto crit_info = critMultInfo(wep, melee_wep);
    if (crit_info.first <= crit_info.second || melee_wep)
        return 0;
    float target_chance = crit_info.second;
    int damage          = std::ceil(crit_damage * (2.0f * target_chance + 1.0f) / (3.0f * target_chance));
    return damage - ranged_damage;
}

static int CommandToSeed(int command_number, IClientEntity *weapon, bool melee_wep)
{
    int iSeed = MD5_PseudoRandom(command_number) & 0x7FFFFFFF;
    int iMask = melee_wep ? (EntIndex(weapon) << 16 | LOCAL_E->m_IDX << 8) : (EntIndex(weapon) << 8 | LOCAL_E->m_IDX);
    return iSeed ^ iMask;
}

static bool IsCritSeed(int seed, IClientEntity *weapon, bool want_crit, bool safe, bool melee_wep)
{
    weapon_info info(weapon);
    if ((unsigned) seed == info.weapon_seed)
        return false;

    ValveRandom rng;
    rng.SetSeed(seed);
    int iRandom = rng.RandomInt(0, WEAPON_RANDOM_RANGE - 1);

    if (safe)
    {
        int iLower = melee_wep ? 1500 : 100;
        int iUpper = melee_wep ? 6000 : 800;
        iLower     = (int) (iLower * cached_mult_chance);
        iUpper     = (int) (iUpper * cached_mult_chance);
        if (want_crit)
            return iLower >= 0 && iRandom < iLower;
        return iUpper < WEAPON_RANDOM_RANGE && !(iRandom < iUpper);
    }

    int iRange = (int) (cached_crit_chance * WEAPON_RANDOM_RANGE);
    return want_crit ? iRandom < iRange : !(iRandom < iRange);
}

static bool IsCritCommand(int command_number, IClientEntity *weapon, bool want_crit, bool safe, bool melee_wep)
{
    return IsCritSeed(CommandToSeed(command_number, weapon, melee_wep), weapon, want_crit, safe, melee_wep);
}

static int GetCritCommand(IClientEntity *weapon, int command_number, bool want_crit, bool safe, bool melee_wep, int attempts)
{
    for (int i = 0; i < attempts; i++)
    {
        int cmd = command_number + i;
        if (IsCritCommand(cmd, weapon, want_crit, safe, melee_wep))
            return cmd;
    }
    return 0;
}

static void UpdateWeaponInfo(IClientEntity *weapon, bool melee_wep)
{
    cached_melee        = melee_wep;
    cached_mult_chance  = ATTRIB_HOOK_FLOAT(1.0f, "mult_crit_chance", weapon, 0, 1);
    cached_crit_chance  = getCritCap(weapon, melee_wep);

    WeaponData_t *data = GetWeaponData(weapon);
    int nProjectilesPerShot = data->m_nBulletsPerShot;
    if (!melee_wep && nProjectilesPerShot > 0)
        nProjectilesPerShot = (int) ATTRIB_HOOK_FLOAT((float) nProjectilesPerShot, "mult_bullets_per_shot", weapon, 0, true);
    else
        nProjectilesPerShot = 1;

    added_per_shot = (float) data->m_nDamage;
    added_per_shot = ATTRIB_HOOK_FLOAT(added_per_shot, "mult_dmg", weapon, 0, true);
    added_per_shot *= (float) nProjectilesPerShot;
    if (added_per_shot <= 0.0f)
        added_per_shot = 0.0f;

    const float cap = getBucketCap();
    shots_to_fill_bucket = added_per_shot > 0.0f ? (int) (cap / added_per_shot) : 0;

    float flDamage = added_per_shot;
    taken_per_crit = added_per_shot;
    if (isRapidFire(weapon))
    {
        float delay = data->m_flTimeFireDelay > 0.0f ? data->m_flTimeFireDelay : 0.1f;
        taken_per_crit = added_per_shot * (TF_DAMAGE_CRIT_DURATION_RAPID / delay);
        if (taken_per_crit * TF_DAMAGE_CRIT_MULTIPLIER > cap)
            taken_per_crit = cap / TF_DAMAGE_CRIT_MULTIPLIER;
        flDamage = taken_per_crit;
    }

    weapon_info info(weapon);
    float flMult = getWithdrawMult(weapon, melee_wep);
    float flCost = flDamage * TF_DAMAGE_CRIT_MULTIPLIER;
    cached_cost  = flCost * flMult;

    float flBaseDamage = added_per_shot;
    float denom        = TF_DAMAGE_CRIT_MULTIPLIER * flDamage / (melee_wep ? 2.0f : 1.0f) - flBaseDamage;
    potential_crits    = denom > 0.0f ? (int) ((std::max(cap, info.crit_bucket) - flBaseDamage) / denom) : 0;

    available_crits = 0;
    {
        int iTestShots   = info.crit_attempts;
        int iTestCrits   = info.crit_count;
        float flTestBucket = info.crit_bucket;
        for (int i = 0; i < BUCKET_ATTEMPTS; i++)
        {
            iTestShots++;
            iTestCrits++;
            float flTestMult = melee_wep ? 0.5f : RemapValClamped((float) iTestCrits / (float) iTestShots, 0.1f, 1.f, 1.f, 3.f);
            if (flTestBucket < cap)
                flTestBucket = std::min(flTestBucket + flBaseDamage, cap);
            flTestBucket -= flCost * flTestMult;
            if (flTestBucket < 0.0f)
                break;
            available_crits++;
        }
    }

    next_crit_shots = 0;
    if (available_crits != potential_crits)
    {
        int iTestShots     = info.crit_attempts;
        int iTestCrits     = info.crit_count;
        float flTestBucket = info.crit_bucket;
        float flTickBase   = g_GlobalVars->curtime;
        float flLastRapid  = info.last_rapid_fire_crit_check;
        float flFireRate   = data->m_flTimeFireDelay > 0.0f ? data->m_flTimeFireDelay : 0.1f;
        bool rapid         = isRapidFire(weapon);
        for (int i = 0; i < BUCKET_ATTEMPTS; i++)
        {
            int iCrits = 0;
            {
                int iTestShots2     = iTestShots;
                int iTestCrits2     = iTestCrits;
                float flTestBucket2 = flTestBucket;
                for (int j = 0; j < BUCKET_ATTEMPTS; j++)
                {
                    iTestShots2++;
                    iTestCrits2++;
                    float flTestMult = melee_wep ? 0.5f : RemapValClamped((float) iTestCrits2 / (float) iTestShots2, 0.1f, 1.f, 1.f, 3.f);
                    if (flTestBucket2 < cap)
                        flTestBucket2 = std::min(flTestBucket2 + flBaseDamage, cap);
                    flTestBucket2 -= flCost * flTestMult;
                    if (flTestBucket2 < 0.0f)
                        break;
                    iCrits++;
                }
            }
            if (available_crits < iCrits)
                break;

            if (!rapid)
                iTestShots++;
            else
            {
                flTickBase += std::ceil(flFireRate / g_GlobalVars->interval_per_tick) * g_GlobalVars->interval_per_tick;
                if (flTickBase >= flLastRapid + 1.0f || (!i && flTestBucket == cap))
                {
                    iTestShots++;
                    flLastRapid = flTickBase;
                }
            }
            if (flTestBucket < cap)
                flTestBucket = std::min(flTestBucket + flBaseDamage, cap);
            next_crit_shots++;
        }
    }
}

static void UpdateInfo(IClientEntity *weapon, bool melee_wep)
{
    UpdateWeaponInfo(weapon, melee_wep);
    crit_banned      = false;
    damage_til_flip  = 0.0f;
    if (melee_wep)
        return;

    float flNormalizedDamage = crit_damage / TF_DAMAGE_CRIT_MULTIPLIER;
    float flCritChance       = cached_crit_chance + 0.1f;
    if (ranged_damage > 0 && crit_damage > 0)
    {
        float observed = flNormalizedDamage / (flNormalizedDamage + ranged_damage - crit_damage);
        crit_banned    = observed > flCritChance;
    }
    if (crit_banned)
        damage_til_flip = flNormalizedDamage / flCritChance + flNormalizedDamage * 2.0f - ranged_damage;
    else if (flCritChance != 1.0f)
        damage_til_flip = TF_DAMAGE_CRIT_MULTIPLIER * (flNormalizedDamage - flCritChance * (flNormalizedDamage + ranged_damage - crit_damage)) / (flCritChance - 1.0f);
}

static bool WeaponCanCrit(IClientEntity *weapon, bool weapon_only = false)
{
    if (!weapon)
        return false;
    if (!re::C_TFWeaponBase::IsBaseCombatWeapon(weapon))
        return false;
    if (!weapon_only && !re::C_TFWeaponBase::AreRandomCritsEnabled(weapon))
        return false;
    if (ATTRIB_HOOK_FLOAT(1.0f, "mult_crit_chance", weapon, 0, 1) <= 0.0f)
        return false;

    switch (re::C_TFWeaponBase::GetWeaponID(weapon))
    {
    case kTF_WEAPON_PDA:
    case kTF_WEAPON_PDA_ENGINEER_BUILD:
    case kTF_WEAPON_PDA_ENGINEER_DESTROY:
    case kTF_WEAPON_PDA_SPY:
    case kTF_WEAPON_PDA_SPY_BUILD:
    case kTF_WEAPON_BUILDER:
    case kTF_WEAPON_INVIS:
    case kTF_WEAPON_JAR_MILK:
    case kTF_WEAPON_LUNCHBOX:
    case kTF_WEAPON_BUFF_ITEM:
    case kTF_WEAPON_FLAME_BALL:
    case kTF_WEAPON_ROCKETPACK:
    case kTF_WEAPON_JAR_GAS:
    case kTF_WEAPON_LASER_POINTER:
    case kTF_WEAPON_MEDIGUN:
    case kTF_WEAPON_SNIPERRIFLE:
    case kTF_WEAPON_SNIPERRIFLE_DECAP:
    case kTF_WEAPON_SNIPERRIFLE_CLASSIC:
    case kTF_WEAPON_COMPOUND_BOW:
    case kTF_WEAPON_JAR:
    case kTF_WEAPON_KNIFE:
    case kTF_WEAPON_PASSTIME_GUN:
        return false;
    }
    return true;
}

bool isEnabled()
{
    if (!randomCritEnabled())
        return false;
    return melee || enabled;
}

static bool isMeleeWep(IClientEntity *weapon = nullptr)
{
    if (!weapon)
    {
        if (CE_BAD(LOCAL_W))
            return false;
        weapon = RAW_ENT(LOCAL_W);
    }
    return re::C_TFWeaponBase::IsMeleeWeapon(weapon);
}

bool shouldMeleeCrit()
{
    return melee && isMeleeWep();
}

bool shouldCrit()
{
    if (shouldMeleeCrit())
        return true;
    static bool pressed_key_last_tick = false;
    static int loose_cannon_countdown = 0;
    if (enabled && ((!isMeleeWep() && !crit_key) || crit_key.isKeyDown()))
    {
        pressed_key_last_tick = true;
        return true;
    }
    if (!crit_key.isKeyDown() && (pressed_key_last_tick || loose_cannon_countdown))
    {
        if (pressed_key_last_tick)
            loose_cannon_countdown = 7;
        pressed_key_last_tick          = false;
        static unsigned last_tickcount = 0;
        if (tickcount != last_tickcount)
        {
            last_tickcount = tickcount;
            loose_cannon_countdown--;
        }
        if (CE_GOOD(LOCAL_W) && LOCAL_W->m_iClassID() == CL_CLASS(CTFCannon))
            return true;
    }
    if (force_crit_this_tick)
        return true;
    return false;
}

static bool can_beggars_crit   = false;
static bool attacked_last_tick = false;

bool canWeaponCrit(bool for_draw = false)
{
    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    if (!WeaponCanCrit(weapon, for_draw))
        return false;
    if (!added_per_shot)
        return false;
    bool melee_wep = isMeleeWep(weapon);
    if (!getCritCap(weapon, melee_wep))
        return false;
    if (!isAllowedToWithdrawFromBucket(weapon, melee_wep))
        return false;
    if (!for_draw && !CanShoot() && !isRapidFire(weapon) && LOCAL_W->m_iClassID() != CL_CLASS(CTFCannon))
        return false;
    if (!for_draw && CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 730 && !can_beggars_crit)
        return false;
    auto crit_mult_info = critMultInfo(weapon, melee_wep);
    if (crit_mult_info.first > crit_mult_info.second && !melee_wep)
        return false;
    return true;
}

static int last_sent_command = 0;

static void apply_command(int cmd)
{
    current_late_user_cmd->command_number = cmd;
    current_late_user_cmd->random_seed    = MD5_PseudoRandom(cmd) & 0x7FFFFFFF;
    SetPredictionRandomSeed(current_late_user_cmd->random_seed);
}

static void sync_command_number()
{
    if (!current_late_user_cmd || current_late_user_cmd->command_number <= 0)
        return;
    if (last_sent_command >= current_late_user_cmd->command_number)
        apply_command(last_sent_command + 1);
    last_sent_command = current_late_user_cmd->command_number;
}

bool prevent_crit()
{
    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    bool melee_wep        = isMeleeWep(weapon);
    int cmd = GetCritCommand(weapon, current_late_user_cmd->command_number, false, true, melee_wep, SEED_ATTEMPTS);
    if (!cmd)
        return false;
    apply_command(cmd);
    return true;
}

void force_crit()
{
    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    bool melee_wep        = isMeleeWep(weapon);
    bool want_crit        = true;

    if (hacks::tf2::antianticheat::enabled)
    {
        if (CE_GOOD(LOCAL_W) && (LOCAL_W->m_iClassID() == CL_CLASS(CTFCannon) || LOCAL_W->m_iClassID() == CL_CLASS(CTFPipebombLauncher) || CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 730))
            return;
        if (!IsCritCommand(current_late_user_cmd->command_number, weapon, want_crit, false, melee_wep))
            current_late_user_cmd->buttons &= ~IN_ATTACK;
        force_crit_this_tick = false;
        return;
    }

    int attempts = (melee_wep || LOCAL_W->m_iClassID() == CL_CLASS(CTFPipebombLauncher)) ? SEED_ATTEMPTS : SEED_ATTEMPTS;
    int cmd      = GetCritCommand(weapon, current_late_user_cmd->command_number, want_crit, true, melee_wep, attempts);
    if (!cmd)
        cmd = GetCritCommand(weapon, current_late_user_cmd->command_number, want_crit, false, melee_wep, attempts);
    if (cmd)
        apply_command(cmd);
    force_crit_this_tick = false;
}

void fixBucket(IClientEntity *weapon, CUserCmd *cmd)
{
    (void) cmd;
    if (!weapon)
        return;
    if (auto off = weapon_layout::last_crit_check_frame())
        *(int *) ((uintptr_t) weapon + off) = -1;
}

bool isExploitingDoubleAttack()
{
    if (!current_user_cmd || !(current_user_cmd->buttons & IN_ATTACK2) || !isMeleeWep() || g_pLocalPlayer->clazz != tf_engineer)
        return false;
    int eindex;
    WhatIAmLookingAt(&eindex, nullptr);
    if (eindex == -1)
        return false;
    auto entity = ENTITY(eindex);
    if (CE_GOOD(entity) && entity->m_Type() == ENTITY_BUILDING && HandleToIDX(CE_INT(entity, netvar.m_hBuilder)) == LOCAL_E->m_IDX)
        return true;
    return false;
}

struct player_status
{
    int health{};
    int clazz{};
    bool just_updated{};
};
static std::array<player_status, PLAYER_ARRAY_SIZE> player_status_list{};

static player_status *status_for(int idx)
{
    if (idx < 1 || idx >= PLAYER_ARRAY_SIZE)
        return nullptr;
    return &player_status_list[idx];
}

static void CreateMove()
{
    if (g_pPlayerResource->GetDamage(g_pLocalPlayer->entity_idx) < round_damage)
        round_damage = g_pPlayerResource->GetDamage(g_pLocalPlayer->entity_idx);
    cached_damage = g_pPlayerResource->GetDamage(g_pLocalPlayer->entity_idx) - melee_damage;

    for (auto const &ent : entity_cache::player_cache)
    {
        if (!g_pPlayerResource->GetHealth(ent))
            continue;
        auto *status = status_for(ent->m_IDX);
        if (!status)
            continue;
        if (!status->just_updated && (status->clazz != g_pPlayerResource->GetClass(ent) || status->health < g_pPlayerResource->GetHealth(ent)))
        {
            status->clazz  = g_pPlayerResource->GetClass(ent);
            status->health = g_pPlayerResource->GetHealth(ent);
        }
        status->just_updated = false;
    }

    if (current_late_user_cmd && current_late_user_cmd->command_number > 0)
        sync_command_number();

    if (!isEnabled())
        return;
    if (!current_late_user_cmd || !current_late_user_cmd->command_number)
        return;
    if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W))
        return;

    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    bool melee_wep        = isMeleeWep(weapon);
    UpdateInfo(weapon, melee_wep);

    if (CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 730)
    {
        if (!can_beggars_crit)
            can_beggars_crit = !(current_late_user_cmd->buttons & IN_ATTACK) && attacked_last_tick;
        attacked_last_tick = current_late_user_cmd->buttons & IN_ATTACK;
        if (!CE_INT(LOCAL_W, netvar.m_iClip1) && CE_INT(LOCAL_W, netvar.iReloadMode) == 0)
            can_beggars_crit = false;
    }
    else
        can_beggars_crit = false;

    if (!WeaponCanCrit(weapon))
        return;

    weapon_info info(weapon);
    if (re::CTFPlayerShared::IsCritBoosted(re::CTFPlayerShared::GetPlayerShared(RAW_ENT(LOCAL_E))) || info.m_flCritTime > g_GlobalVars->curtime)
        return;

    if (LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun) && current_late_user_cmd->buttons & IN_ATTACK)
        current_late_user_cmd->buttons &= ~IN_ATTACK2;

    if (!(current_late_user_cmd->buttons & IN_ATTACK))
    {
        if (LOCAL_W->m_iClassID() == CL_CLASS(CTFPipebombLauncher))
        {
            float chargebegin = netvar.flChargeBeginTime ? CE_FLOAT(LOCAL_W, netvar.flChargeBeginTime) : 0.0f;
            float chargetime  = g_GlobalVars->curtime - chargebegin;
            static bool currently_charging_pipe = false;
            if (chargetime < 6.0f && chargetime)
                currently_charging_pipe = true;
            if (!(current_user_cmd->buttons & IN_ATTACK) && currently_charging_pipe)
                currently_charging_pipe = false;
            else
                return;
        }
        else if (LOCAL_W->m_iClassID() == CL_CLASS(CTFCannon))
        {
        }
        else if (!can_beggars_crit)
            return;
    }

    if (isRapidFire(weapon) && g_GlobalVars->curtime < info.last_rapid_fire_crit_check + 1.0f)
        return;

    if (isExploitingDoubleAttack())
        force_crit_this_tick = true;

    if (!canWeaponCrit())
        return;

    if (shouldCrit())
        force_crit();
    else if (force_no_crit)
        prevent_crit();
    force_crit_this_tick = false;
    if (current_late_user_cmd && current_late_user_cmd->command_number > 0)
        last_sent_command = current_late_user_cmd->command_number;
}

static int last_crit_tick   = -1;
static int last_bucket      = 0;
static int shots_until_crit = 0;
static int last_wep         = 0;

#if ENABLE_VISUALS
static std::array<std::string, 32> crit_strings;
static size_t crit_strings_count{ 0 };
static std::array<rgba_t, 32> crit_strings_colors{ colors::empty };
static std::string bar_string = "";

void AddCritString(const std::string &string, const rgba_t &color)
{
    crit_strings[crit_strings_count]        = string;
    crit_strings_colors[crit_strings_count] = color;
    ++crit_strings_count;
}

void DrawCritStrings()
{
    float x = *bar_x + *size;
    float y = *bar_y + *size / 5.0f;
    if (bar_string != "")
    {
        float sx, sy;
        fonts::center_screen->stringSize(bar_string, &sx, &sy);
        draw::String(x - sx / 2, (y + sy), colors::red_s, bar_string.c_str(), *fonts::center_screen);
        y += fonts::center_screen->size + 1;
    }
    x = *draw_string_x;
    y = *draw_string_y;
    for (size_t i = 0; i < crit_strings_count; ++i)
    {
        float sx, sy;
        fonts::menu->stringSize(crit_strings[i], &sx, &sy);
        draw::String(x, y, crit_strings_colors[i], crit_strings[i].c_str(), *fonts::center_screen);
        y += fonts::center_screen->size + 1;
    }
    crit_strings_count = 0;
    bar_string         = "";
}

static Timer update_shots{};

void Draw()
{
    if (!isEnabled())
        return;
    if (!draw && !draw_meter)
        return;
    if (!g_IEngine->GetNetChannelInfo())
        last_crit_tick = -1;
    if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W))
        return;

    auto wep       = RAW_ENT(LOCAL_W);
    bool melee_wep = isMeleeWep(wep);
    weapon_info info(wep);
    float bucket = info.crit_bucket;
    bool can_crit = canWeaponCrit(true);

    if (bucket != last_bucket || EntIndex(wep) != last_wep || update_shots.test_and_set(500))
    {
        if (!can_crit)
            shots_until_crit = shotsUntilCrit(wep, melee_wep);
    }

    auto crit_mult_info = critMultInfo(wep, melee_wep);

    if (draw)
    {
        if (shouldCrit())
        {
            if (can_crit)
                AddCritString("Forcing Crits!", colors::red_s);
            else
                AddCritString("Weapon can currently not crit!", colors::red_s);
        }

        if (!WeaponCanCrit(wep, true))
        {
            AddCritString("Weapon cannot randomly crit.", colors::red_s);
            DrawCritStrings();
            last_bucket = bucket;
            last_wep    = EntIndex(wep);
            return;
        }
        if (!re::C_TFWeaponBase::AreRandomCritsEnabled(wep))
        {
            AddCritString("Random crits disabled.", colors::red_s);
            DrawCritStrings();
            last_bucket = bucket;
            last_wep    = EntIndex(wep);
            return;
        }

        if (is_out_of_sync)
            AddCritString("Out of sync.", colors::red_s);
        else if (crit_banned && !melee_wep)
            AddCritString("Damage Until crit: " + std::to_string((int) std::ceil(std::max(0.0f, damage_til_flip))), colors::orange);
        else if (crit_mult_info.first > crit_mult_info.second && !melee_wep)
            AddCritString("Damage Until crit: " + std::to_string(damageUntilToCrit(wep, melee_wep)), colors::orange);
        else if (!can_crit)
        {
            if (isRapidFire(wep))
            {
                std::string crit_string = "Shots until crit: ";
                crit_string += std::to_string(std::max(0, next_crit_shots));
                if (info.last_rapid_fire_crit_check + 1.0f >= g_GlobalVars->curtime)
                {
                    crit_string += ", ";
                    crit_string += std::to_string(info.last_rapid_fire_crit_check + 1.0f - g_GlobalVars->curtime) + "s";
                    AddCritString(crit_string, colors::red);
                }
                else
                    AddCritString(crit_string, colors::orange);
            }
            else
                AddCritString("Shots until crit: " + std::to_string(shots_until_crit), colors::orange);
        }

        auto color = colors::red_s;
        if (can_crit && (crit_mult_info.first <= crit_mult_info.second || melee_wep))
            color = colors::green;
        AddCritString("Crits: " + std::to_string(std::max(0, available_crits)) + " / " + std::to_string(std::max(0, potential_crits)), color);
        AddCritString("Crit Bucket: " + std::to_string(bucket), color);
    }

    if (draw_meter)
    {
        if (WeaponCanCrit(wep, true) && added_per_shot)
        {
            rgba_t bucket_color = colors::FromRGBA8(0x53, 0xbc, 0x31, 255);
            if (shouldCrit())
                bucket_color = colors::FromRGBA8(0x34, 0xeb, 0xae, 255);
            if (!can_crit)
                bucket_color = colors::red_s;

            float bucket_percentage = bucket / getBucketCap();
            float bucket_percentage_post_crit = bucket;
            bucket_percentage_post_crit += added_per_shot;
            if (bucket_percentage_post_crit > getBucketCap())
                bucket_percentage_post_crit = getBucketCap();
            bucket_percentage_post_crit -= getWithdrawAmount(wep, melee_wep);
            if (bucket_percentage_post_crit < 0.0f)
                bucket_percentage_post_crit = 0.0f;
            bucket_percentage_post_crit /= getBucketCap();

            rgba_t reduction_color = colors::Fade(bucket_color, colors::white, g_GlobalVars->curtime, 2.0f);
            static rgba_t background_color = colors::FromRGBA8(96, 96, 96, 150);
            float bar_bg_x_size            = *size * 2.0f;
            float bar_bg_y_size            = *size / 5.0f;
            draw::Rectangle(*bar_x - 5.0f, *bar_y - 5.0f, bar_bg_x_size + 10.0f, bar_bg_y_size + 10.0f, background_color);

            if (is_out_of_sync || (crit_mult_info.first > crit_mult_info.second && !melee_wep) || !can_crit)
            {
                draw::Rectangle(*bar_x, *bar_y, bar_bg_x_size * bucket_percentage, bar_bg_y_size, bucket_color);
                if (is_out_of_sync)
                    bar_string = "Out of sync.";
                else if (crit_banned && !melee_wep)
                    bar_string = std::to_string((int) std::ceil(std::max(0.0f, damage_til_flip))) + " Damage until Crit!";
                else if (crit_mult_info.first > crit_mult_info.second && !melee_wep)
                    bar_string = std::to_string(damageUntilToCrit(wep, melee_wep)) + " Damage until Crit!";
                else if (isRapidFire(wep))
                {
                    std::string crit_string = std::to_string(std::max(0, next_crit_shots)) + " Shots until Crit! ";
                    if (info.last_rapid_fire_crit_check + 1.0f >= g_GlobalVars->curtime)
                        crit_string += std::to_string(info.last_rapid_fire_crit_check + 1.0f - g_GlobalVars->curtime) + "s";
                    bar_string = crit_string;
                }
                else
                    bar_string = std::to_string(shots_until_crit) + " Shots until Crit!";
            }
            if (!((crit_mult_info.first > crit_mult_info.second && !melee_wep) || !can_crit))
            {
                float bucket_draw_percentage = bucket_percentage_post_crit;
                float x_offset_bucket        = bucket_draw_percentage * bar_bg_x_size;
                if (x_offset_bucket > 0.0f)
                    draw::Rectangle(*bar_x, *bar_y, x_offset_bucket, bar_bg_y_size, bucket_color);
                else
                    x_offset_bucket = 0.0f;
                float reduction_draw_percentage = bucket_percentage - bucket_percentage_post_crit;
                if (bucket_draw_percentage < 0.0f)
                    reduction_draw_percentage += bucket_draw_percentage;
                draw::Rectangle(*bar_x + x_offset_bucket, *bar_y, bar_bg_x_size * reduction_draw_percentage, bar_bg_y_size, reduction_color);
            }
        }
    }

    last_bucket = bucket;
    last_wep    = EntIndex(wep);
    DrawCritStrings();
}
#endif

class CritEventListener : public IGameEventListener
{
public:
    void FireGameEvent(KeyValues *event) override
    {
        const char *name = event->GetName();
        if (!strcmp(name, "teamplay_round_start") || !strcmp(name, "scorestats_accumulated_update") || !strcmp(name, "mvm_reset_stats"))
        {
            crit_damage   = 0;
            melee_damage  = 0;
            ranged_damage = 0;
            round_damage  = g_pPlayerResource ? g_pPlayerResource->GetDamage(g_pLocalPlayer->entity_idx) : 0;
            cached_damage = round_damage - melee_damage;
        }
        else if (!strcmp(name, "player_hurt"))
        {
            int victim = GetPlayerForUserID(event->GetInt("userid"));
            int health = event->GetInt("health");
            auto *status = status_for(victim);
            int health_difference = 0;
            if (status)
            {
                health_difference   = status->health - health;
                status->health      = health;
                status->just_updated = true;
            }
            if (GetPlayerForUserID(event->GetInt("attacker")) != g_pLocalPlayer->entity_idx)
                return;
            if (victim == g_pLocalPlayer->entity_idx)
                return;

            int weaponid   = event->GetInt("weaponid");
            int weapon_idx = getWeaponByID(LOCAL_E, weaponid);
            bool isMelee   = false;
            if (IDX_GOOD(weapon_idx))
            {
                IClientEntity *hurt_wep = g_IEntityList->GetClientEntity(weapon_idx);
                isMelee                 = re::C_TFWeaponBase::IsMeleeWeapon(hurt_wep);
            }
            int damage = event->GetInt("damageamount");
            if (damage > health_difference && !health && health_difference > 0)
                damage = health_difference;
            if (!isMelee)
            {
                ranged_damage += damage;
                if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W) || !re::CTFPlayerShared::IsCritBoosted(re::CTFPlayerShared::GetPlayerShared(RAW_ENT(LOCAL_E))))
                {
                    if (event->GetBool("crit") || event->GetBool("minicrit"))
                        crit_damage += damage;
                }
                cached_damage = g_pPlayerResource->GetDamage(g_pLocalPlayer->entity_idx) - melee_damage;
            }
            else
                melee_damage += damage;
        }
    }
};

static CritEventListener listener{};

void observedcritchance_nethook(const CRecvProxyData *data, void *pWeapon, void *out)
{
    auto fl_observed_crit_chance = reinterpret_cast<float *>(out);
    *fl_observed_crit_chance     = data->m_Value.m_Float;
    if (!debug_desync || CE_BAD(LOCAL_W) || !enabled)
        return;
    if (pWeapon != LOCAL_W->InternalEntity())
        return;
    float sent_chance = data->m_Value.m_Float;
    if (sent_chance)
    {
        float ours = getObservedCritChance();
        if (fabsf(sent_chance - ours) > 0.01f)
            logging::Info("Observed crit chance server=%f client=%f", sent_chance, ours);
    }
}

static ProxyFnHook observed_crit_chance_hook{};

void LevelShutdown()
{
    last_crit_tick = -1;
    cached_damage  = 0;
    crit_damage    = 0;
    melee_damage   = 0;
    ranged_damage  = 0;
    round_damage   = 0;
    last_sent_command = 0;
    is_out_of_sync = false;
    crit_cmds.clear();
    current_index    = 0;
    available_crits  = 0;
    potential_crits  = 0;
    added_per_shot   = 0.0f;
}

static CatCommand debug_print_crit_info("debug_print_crit_info", "Print a bunch of useful crit info",
                                        []()
                                        {
                                            if (CE_BAD(LOCAL_E))
                                                return;
                                            logging::Info("Player specific information:");
                                            logging::Info("Ranged Damage this round: %d", ranged_damage);
                                            logging::Info("Melee Damage this round: %d", melee_damage);
                                            logging::Info("Crit Damage this round: %d", crit_damage);
                                            logging::Info("Observed crit chance: %f", getObservedCritChance());
                                            if (CE_GOOD(LOCAL_W))
                                            {
                                                IClientEntity *wep = RAW_ENT(LOCAL_W);
                                                bool melee_wep     = isMeleeWep(wep);
                                                weapon_info info(wep);
                                                logging::Info("Weapon specific information:");
                                                logging::Info("Crit bucket: %f", info.crit_bucket);
                                                logging::Info("Needed Crit chance: %f", critMultInfo(wep, melee_wep).second);
                                                logging::Info("Added per shot: %f", added_per_shot);
                                                logging::Info("Subtracted per crit: %f", getWithdrawAmount(wep, melee_wep));
                                                logging::Info("Damage Until crit: %d", damageUntilToCrit(wep, melee_wep));
                                                logging::Info("Shots until crit: %d", shotsUntilCrit(wep, melee_wep));
                                                logging::Info("Available / potential: %d / %d", available_crits, potential_crits);
                                                logging::Info("Weapon info ptr: %p mode=%d", (void *) info.weapon_data, info.weapon_mode);
                                            }
                                        });

static CatCommand debug_data("debug_data", "debug",
                             []()
                             {
                                 if (CE_BAD(LOCAL_W))
                                     return;
                                 IClientEntity *wep = RAW_ENT(LOCAL_W);
                                 weapon_info info(wep);
                                 logging::Info("bucket=%f seed=%u frame=%u critt=%f checks=%d seeds=%d obs=%f mode=%d info=%p rapidt=%f", info.crit_bucket, info.weapon_seed, info.last_crit_check_frame, info.m_flCritTime, info.crit_attempts, info.crit_count, info.observed_crit_chance, info.weapon_mode, (void *) info.weapon_data, info.last_rapid_fire_crit_check);
                             });

static DetourHook outer_crit_detour{};
static DetourHook validate_cmd_detour{};
static DetourHook random_crit_detour{};
static int saved_attack_seed = -1;

using OuterCalcFn                 = intptr_t (*)(IClientEntity *);
using CanFireRandomCriticalShotFn = bool (*)(IClientEntity *, float);

static intptr_t OuterCalcHook(IClientEntity *weapon)
{
    auto orig = (OuterCalcFn) outer_crit_detour.GetOriginalFunc();
    if (!weapon || !orig)
        return orig ? orig(weapon) : 0;
    IClientEntity *owner = re::C_TFWeaponBase::GetOwnerViaInterface(weapon);
    if (CE_BAD(LOCAL_E) || !owner || owner != LOCAL_E->InternalEntity())
        return orig(weapon);

    const int mode_off = weapon_layout::weapon_mode();
    int old_mode       = mode_off ? *(int *) ((uintptr_t) weapon + mode_off) : 0;
    if (mode_off)
        *(int *) ((uintptr_t) weapon + mode_off) = 0;

    bool first = g_IPrediction && g_IPrediction->IsFirstTimePredicted();
    weapon_info saved(weapon);
    intptr_t ret = orig(weapon);
    if (first)
    {
        if (auto off = weapon_layout::weapon_seed())
            saved_attack_seed = *(int *) ((uintptr_t) weapon + off);
    }
    else
    {
        saved.restore_prediction_state(weapon);
        if (saved_attack_seed >= 0)
        {
            if (auto off = weapon_layout::weapon_seed())
                *(int *) ((uintptr_t) weapon + off) = saved_attack_seed;
        }
    }
    if (mode_off)
        *(int *) ((uintptr_t) weapon + mode_off) = old_mode;
    return ret;
}

static void ValidateUserCmd_hook(void *this_, CUserCmd *cmd, int sequence_number)
{
    (void) this_;
    (void) cmd;
    (void) sequence_number;
}

static bool CanFireRandomCriticalShot_hook(IClientEntity *weapon, float flCritChance)
{
    auto orig = (CanFireRandomCriticalShotFn) random_crit_detour.GetOriginalFunc();
    if (!weapon || CE_BAD(LOCAL_W) || weapon != RAW_ENT(LOCAL_W))
        return orig ? orig(weapon, flCritChance) : true;
    if (!ranged_damage)
        return true;
    SyncObservedChance(weapon);
    return orig ? orig(weapon, flCritChance) : true;
}

static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMoveLate, CreateMove, "crit_cm", EC::late);
#if ENABLE_VISUALS
        EC::Register(EC::Draw, Draw, "crit_draw");
#endif
        EC::Register(EC::LevelShutdown, LevelShutdown, "crit_lvlshutdown");
        g_IGameEventManager->AddListener(&listener, false);
        HookNetvar({ "DT_TFWeaponBase", "LocalActiveTFWeaponData", "m_flObservedCritChance" }, observed_crit_chance_hook, observedcritchance_nethook);

        if (auto addr = gSignatures.GetClientSignature(sigs::ctf_weapon_base_calc_is_attack_critical_outer))
        {
            outer_crit_detour.Init((uintptr_t) addr, (void *) OuterCalcHook);
            logging::Info("CritHack: hooked CalcIsAttackCritical at %p", addr);
        }
        else
            logging::Info("CritHack: CalcIsAttackCritical signature missed");
        if (auto addr = gSignatures.GetClientSignature(sigs::cinput_validate_usercmd))
        {
            validate_cmd_detour.Init((uintptr_t) addr, (void *) ValidateUserCmd_hook);
            logging::Info("CritHack: hooked ValidateUserCmd at %p", addr);
        }
        else
            logging::Info("CritHack: ValidateUserCmd signature missed");
        if (auto addr = gSignatures.GetClientSignature(sigs::ctf_weapon_base_can_fire_random_critical_shot))
        {
            random_crit_detour.Init((uintptr_t) addr, (void *) CanFireRandomCriticalShot_hook);
            logging::Info("CritHack: hooked CanFireRandomCriticalShot at %p", addr);
        }
        else
            logging::Info("CritHack: CanFireRandomCriticalShot signature missed");

        EC::Register(
            EC::Shutdown,
            []()
            {
                g_IGameEventManager->RemoveListener(&listener);
                observed_crit_chance_hook.restore();
                outer_crit_detour.Shutdown();
                validate_cmd_detour.Shutdown();
                random_crit_detour.Shutdown();
            },
            "crit_shutdown");
        if (g_IEngine->IsInGame())
            is_out_of_sync = true;
    });
} // namespace criticals
