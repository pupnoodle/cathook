#pragma once

#include "config.h"
#include "dynamic.gen.hpp"

void InitClassTable();

#define CL_CLASS(x) (client_classes::dynamic_list.x)

template <class... Ids>
inline bool class_is(int id, Ids... ids)
{
    return ((id == int(ids)) || ...);
}

#define RCC_PLAYER CL_CLASS(CTFPlayer)
#define RCC_PLAYERRESOURCE CL_CLASS(CTFPlayerResource)
