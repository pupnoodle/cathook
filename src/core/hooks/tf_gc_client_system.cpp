/*
/^-----^\   data: 2026-05-08
V  o o  V  file: src/core/hooks/tf_gc_client_system.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "tf_gc_client_system.hpp"

#include <cstddef>
#include <cstdint>

#include "features/menu/config.hpp"

namespace
{

constexpr int shared_object_created_event = 0;
constexpr unsigned int tf_game_server_lobby_type = 2004;
constexpr unsigned int tf_lobby_invite_type = 2008;
constexpr std::ptrdiff_t lobby_invite_id_offset = 0x20;
constexpr int shared_object_type_vfunc_index = 2;

#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
constexpr bool textmode_auto_casual_join = true;
#else
constexpr bool textmode_auto_casual_join = false;
#endif

using shared_object_type_fn = unsigned int (*)(void* self);

bool auto_casual_join_enabled()
{
  return textmode_auto_casual_join || config.misc.automation.auto_casual_join;
}

unsigned int get_shared_object_type(void* shared_object)
{
  if (shared_object == nullptr)
  {
    return 0;
  }

  auto** vtable = *reinterpret_cast<void***>(shared_object);
  if (vtable == nullptr || vtable[shared_object_type_vfunc_index] == nullptr)
  {
    return 0;
  }

  auto get_type = reinterpret_cast<shared_object_type_fn>(vtable[shared_object_type_vfunc_index]);
  return get_type(shared_object);
}

std::uint64_t get_lobby_invite_id(void* shared_object)
{
  if (shared_object == nullptr)
  {
    return 0;
  }

  auto* bytes = static_cast<std::uint8_t*>(shared_object);
  return *reinterpret_cast<std::uint64_t*>(bytes + lobby_invite_id_offset);
}

void accept_lobby_invite(void* self, void* shared_object)
{
  if (tf_gc_client_system_request_accept_match_invite == nullptr)
  {
    return;
  }

  const std::uint64_t lobby_id = get_lobby_invite_id(shared_object);
  if (lobby_id == 0)
  {
    return;
  }

  tf_gc_client_system_request_accept_match_invite(self, lobby_id);
}

void join_matchmade_lobby(void* self)
{
  if (tf_gc_client_system_join_mm_match == nullptr)
  {
    return;
  }

  tf_gc_client_system_join_mm_match(self);
}

} // namespace

void tf_gc_client_system_so_event_hook(void* self, void* shared_object, const int event_type)
{
  if (!auto_casual_join_enabled() || event_type != shared_object_created_event)
  {
    if (tf_gc_client_system_so_event_original != nullptr)
    {
      tf_gc_client_system_so_event_original(self, shared_object, event_type);
    }
    return;
  }

  const unsigned int object_type = get_shared_object_type(shared_object);
  if (object_type == tf_lobby_invite_type)
  {
    accept_lobby_invite(self, shared_object);
  }
  else if (object_type == tf_game_server_lobby_type)
  {
    if (tf_gc_client_system_so_event_original != nullptr)
    {
      tf_gc_client_system_so_event_original(self, shared_object, event_type);
    }
    join_matchmade_lobby(self);
    return;
  }

  if (tf_gc_client_system_so_event_original != nullptr)
  {
    tf_gc_client_system_so_event_original(self, shared_object, event_type);
  }
}

