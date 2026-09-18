#pragma once

#include "config.h"
#include "core/macros.hpp"

extern int g_AppID;

constexpr bool IsTF2()
{
    return true;
}
constexpr bool IsTF2C()
{
    return false;
}
constexpr bool IsHL2DM()
{
    return false;
}
constexpr bool IsCSS()
{
    return false;
}
constexpr bool IsDynamic()
{
    return false;
}
constexpr bool IsTF()
{
    return true;
}

#define IF_GAME(x) if constexpr (x)
