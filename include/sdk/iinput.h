#pragma once

#include "core/vfunc.hpp"
#include "core/vtables.hpp"
#include "mathlib/vector.h"

class CUserCmd;

struct CameraThirdData_t
{
    float m_flPitch;
    float m_flYaw;
    float m_flDist;
    float m_flLag;
    Vector m_vecHullMin;
    Vector m_vecHullMax;
};

class IInput
{
public:
    CUserCmd *GetUserCmd(int sequence_number)
    {
        return vfunc<CUserCmd *(*)(IInput *, int)>(this, vtables::input::get_user_cmd)(this, sequence_number);
    }
    void ActivateMouse()
    {
        vfunc<void (*)(IInput *)>(this, vtables::input::activate_mouse)(this);
    }
    void DeactivateMouse()
    {
        vfunc<void (*)(IInput *)>(this, vtables::input::deactivate_mouse)(this);
    }
    int CAM_IsThirdPerson()
    {
        return vfunc<int (*)(IInput *)>(this, vtables::input::cam_is_third_person)(this);
    }
};
