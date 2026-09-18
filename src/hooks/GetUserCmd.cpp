/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include "HookedMethods.hpp"

namespace hooked_methods
{

DEFINE_HOOKED_METHOD(GetUserCmd, CUserCmd *, IInput *this_, int sequence_number)
{
    auto *cmds = GetCmds(this_);
    if (!cmds)
        return original::GetUserCmd ? original::GetUserCmd(this_, sequence_number) : nullptr;
    CUserCmd *cmd = &cmds[sequence_number % VERIFIED_CMD_SIZE];
    if (cmd->command_number != sequence_number)
        return nullptr;
    return cmd;
}
} // namespace hooked_methods
