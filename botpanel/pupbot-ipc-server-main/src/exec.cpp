#include "core/ipc/ipc_shared.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{

}

int main(int argc, const char** argv)
{
  if (argc < 3)
  {
    std::cerr << "usage: exec <peer_id> <command...>\n";
    return EXIT_FAILURE;
  }

  int target_id = -1;
  try
  {
    target_id = std::stoi(argv[1]);
  }
  catch (const std::exception& error)
  {
    std::cerr << "invalid peer id: " << error.what() << '\n';
    return EXIT_FAILURE;
  }

  if (target_id < 0 || target_id >= static_cast<int>(pup_ipc::max_peers))
  {
    std::cerr << "invalid peer id: " << target_id << '\n';
    return EXIT_FAILURE;
  }

  std::string command{};
  for (auto index = 2; index < argc; ++index)
  {
    if (!command.empty())
    {
      command.push_back(' ');
    }
    command += argv[index];
  }

  try
  {
    auto memory = pup_ipc::shared_memory::open_client();
    auto* state = memory.state();

    if (!pup_ipc::allowed_console_command(command))
    {
      std::cerr << "command is not allowlisted\n";
      return EXIT_FAILURE;
    }
    if (!pup_ipc::queue_command(
      state,
      target_id,
      command.size() >= pup_ipc::command_data_size - 1 ? pup_ipc::commands::execute_client_cmd_long : pup_ipc::commands::execute_client_cmd,
      command))
    {
      std::cerr << "peer is unavailable or command exceeds the IPC payload limit\n";
      return EXIT_FAILURE;
    }
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
