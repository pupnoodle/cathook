#include "core/ipc/ipc_shared.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace
{

std::atomic_bool running = true;

constexpr const char* classes[] = {
  "Unknown", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer"
};

constexpr const char* teams[] = {
  "UNK", "SPEC", "RED", "BLU"
};

void signal_handler(int)
{
  running.store(false);
}

[[nodiscard]] auto good_class(int class_id) -> bool
{
  return class_id > 0 && class_id < 10;
}

[[nodiscard]] auto good_team(int team_id) -> bool
{
  return team_id >= 0 && team_id < 4;
}

void print_status(pup_ipc::shared_state* state)
{
  pup_ipc::try_scoped_lock lock{state};
  if (!lock.locked())
  {
    return;
  }

  std::printf("\033[1;1H\033[2J");
  std::printf("\033[2;2H\033[1mpupbot IPC panel server\033[0m");
  std::printf("\033[3;4H\033[1mconnected: \033[0m%u / %u", state->peer_count, pup_ipc::max_peers);
  std::printf("\033[3;5H\033[1mcommand count: \033[0m%lu", state->command_count);
  std::printf("\033[2;8H%-2s %-5s %-12s %-21s %s\n", "ID", "PID", "SteamID", "Server IP", "Name");
  std::printf("    %-5s %-9s %-4s   %-5s   %-5s   %-9s %s",
    "State", "Class", "Team", "Score", "Total", "Health", "Heartbeat");

  auto row = 11;
  const auto now = pup_ipc::now_seconds();
  for (auto index = 0u; index < pup_ipc::max_peers; ++index)
  {
    if (!pup_ipc::peer_alive(state->peer_data[index], now))
    {
      continue;
    }

    const auto& data = state->peer_user_data[index];
    std::printf("\033[%d;1H%-2u %-5d %-12u %-21.*s %.*s\n",
      row, index, state->peer_data[index].pid, data.friendid,
      static_cast<int>(sizeof(data.ingame.server)), data.ingame.server,
      static_cast<int>(sizeof(data.name)), data.name);

    if (data.connected && data.ingame.good)
    {
      std::printf("    %-5s %-9s %-4s   %-5d   %-5d   %-4d/%-4d %ld\n",
        data.ingame.life_state ? "Dead" : "Alive",
        good_class(data.ingame.role) ? classes[data.ingame.role] : classes[0],
        good_team(data.ingame.team) ? teams[data.ingame.team] : teams[0],
        data.ingame.score,
        data.accumulated.score,
        data.ingame.health,
        data.ingame.health_max,
        now - data.heartbeat);
    }
    else
    {
      std::printf("    %-5s %-9s %-4s   %-5s   %-5d   %-9s %ld\n",
        "N/A", "N/A", "N/A", "N/A", data.accumulated.score, "N/A", now - data.heartbeat);
    }

    row += 2;
  }

  std::printf("\033[1;%dH", row + 1);
  std::fflush(stdout);
}

}

int main(int argc, char** argv)
{
  auto silent = false;
  auto reset_existing = false;
  for (auto index = 1; index < argc; ++index)
  {
    if (std::string_view{argv[index]} == "-s")
    {
      silent = true;
    }
    else if (std::string_view{argv[index]} == "--reset")
    {
      reset_existing = true;
    }
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  try
  {
    auto memory = pup_ipc::shared_memory::open_or_create_server(reset_existing);
    auto* state = memory.state();

    while (running.load())
    {
      pup_ipc::sweep_dead_peers(state);
      if (!silent)
      {
        print_status(state);
      }
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }
  catch (const std::exception& error)
  {
    std::cerr << "ipc server failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
