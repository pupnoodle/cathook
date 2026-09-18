#include "CServerTools.hpp"

IServerEntity *CServerTools::GetIServerEntity(IClientEntity *pClientEntity)
{
    typedef IServerEntity *(*GetIServerEntity_t)(CServerTools *, IClientEntity *);
    static uintptr_t GetIServerEntity_sig         = uintptr_t(0);
    static GetIServerEntity_t GetIServerEntity_fn = (GetIServerEntity_t) GetIServerEntity_sig;
    if (!GetIServerEntity_fn)
        return nullptr;
    return GetIServerEntity_fn(this, pClientEntity);
}
