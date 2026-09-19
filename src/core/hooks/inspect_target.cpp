/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/inspect_target.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "inspect_target.hpp"
#include <cstring>
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"

namespace
{

auto allow_mvm_inspect() -> bool
{
  if (!config.misc.automation.allow_mvm_inspect || engine == nullptr || !engine->is_in_game())
  {
    return false;
  }

  const char* level_name = engine->get_level_name();
  return level_name != nullptr && std::strstr(level_name, "mvm_") != nullptr;
}

}

std::int64_t inspect_target_check_hook(void* me, void* target)
{
  PUPHOOK_HOOK_GUARD();
  if (allow_mvm_inspect())
  {
    return 1;
  }

  if (inspect_target_check_original == nullptr)
  {
    return 0;
  }

  return inspect_target_check_original(me, target);
}
