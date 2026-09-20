
#include "HookedMethods.hpp"
#include "WeaponData.hpp"

namespace hooked_methods
{

DEFINE_HOOKED_METHOD(RunCommand, void, CPrediction *prediction, IClientEntity *entity, CUserCmd *usercmd, IMoveHelper *move)
{
    if (CE_GOOD(LOCAL_E) && CE_GOOD(LOCAL_W) && entity && EntIndex(entity) == g_pLocalPlayer->entity_idx && usercmd && usercmd->command_number)
    {
        criticals::fixBucket(RAW_ENT(LOCAL_W), usercmd);
        original::RunCommand(prediction, entity, usercmd, move);
    }
    else
        return original::RunCommand(prediction, entity, usercmd, move);
}

DEFINE_HOOKED_METHOD(CalcIsAttackCriticalHelper_brokenweps, bool, IClientEntity *ent)
{
    if (original::CalcIsAttackCriticalHelper_brokenweps)
        return original::CalcIsAttackCriticalHelper_brokenweps(ent);
    return false;
}

} // namespace hooked_methods
