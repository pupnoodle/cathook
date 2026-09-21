#pragma once
#include "settings/Bool.hpp"
class INetMessage;

namespace hacks::tf2::warp
{
extern settings::Boolean enabled;
extern bool in_warp;
extern settings::Boolean dodge_projectile;
void SendNetMessage(INetMessage &msg);
void CL_SendMove_hook();
void PrepareShift();
} // namespace hacks::tf2::warp
