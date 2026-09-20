/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include "HookedMethods.hpp"

namespace hooked_methods
{

DEFINE_HOOKED_METHOD(GetUserCmd, CUserCmd *, IInput *this_, int sequence_number)
{
    if (sequence_number < 0)
        return nullptr;
    auto *cmds = GetCmds(this_);
    if (!cmds)
        return nullptr;
    return &cmds[sequence_number % VERIFIED_CMD_SIZE];
}
} // namespace hooked_methods
