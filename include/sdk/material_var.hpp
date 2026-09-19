#pragma once

#include "core/vfunc.hpp"
#include "core/vtables.hpp"
#include "materialsystem/imaterialvar.h"

// Live linux64 CMaterialVar. No extra dtor slots. Slot 0 is GetTextureValue;
// SetIntValue is 4; SetVecValue(x,y,z) is 11 (forwards to SetVecValue(float*, 3)).
class CMaterialVar
{
public:
    void SetIntValue(int value)
    {
        vfunc<void (*)(CMaterialVar *, int)>(this, vtables::material_var::set_int_value)(this, value);
    }
    void SetVecValue(float x, float y, float z)
    {
        vfunc<void (*)(CMaterialVar *, float, float, float)>(this, vtables::material_var::set_vec_value_xyz)(this, x, y, z);
    }
};

inline CMaterialVar *LiveMaterialVar(IMaterialVar *var)
{
    return reinterpret_cast<CMaterialVar *>(var);
}
