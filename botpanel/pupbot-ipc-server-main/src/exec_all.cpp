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
  if (argc < 2)
  {
    std::cerr << "usage: exec_all <command...>\n";
    return EXIT_FAILURE;
  }

  std::string command{};
  for (auto index = 1; index < argc; ++index)
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
    if (!pup_ipc::allowed_console_command(command))
    {
      std::cerr << "command is not allowlisted\n";
      return EXIT_FAILURE;
    }
    if (!pup_ipc::queue_command(
      memory.state(),
      -1,
      command.size() >= pup_ipc::command_data_size - 1 ? pup_ipc::commands::execute_client_cmd_long : pup_ipc::commands::execute_client_cmd,
      command))
    {
      std::cerr << "command is empty or exceeds the IPC payload limit\n";
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
