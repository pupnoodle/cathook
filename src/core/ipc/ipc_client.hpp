/*
/^-----^\   data: 2026-05-01
V  o o  V  file: src/core/ipc/ipc_client.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef CAT_IPC_CLIENT_HPP
#define CAT_IPC_CLIENT_HPP

#include <cstdint>

class GameEvent;

namespace cat_ipc::client
{

void set_enabled(bool enabled);
void set_auto_ignore_enabled(bool enabled);
void start();
void tick();
void on_game_event(GameEvent* event);
void shutdown();
[[nodiscard]] bool connected();
[[nodiscard]] int peer_id();
[[nodiscard]] bool is_local_ipc_friend(std::uint32_t friend_id);
[[nodiscard]] bool is_excess_ipc_bot_on_current_server(int max_bots);

} // namespace cat_ipc::client

#endif
