#include "core/ipc/ipc_shared.hpp"

#include <cassert>
#include <cstring>
#include <thread>
#include <chrono>

int main()
{
  pup_ipc::shared_state state{};
  pup_ipc::command_s command{};
  pthread_mutex_init(&state.mutex, nullptr);

  command.payload_offset = pup_ipc::command_pool_size - 1;
  command.payload_size = 2;
  assert(!pup_ipc::command_payload(&state, command).has_value());

  command.payload_offset = 0;
  command.payload_size = 4;
  std::memcpy(state.pool, "abc", 4);
  assert(pup_ipc::command_payload(&state, command)->as_string() == "abc");

  std::memset(state.pool, 'x', 4);
  assert(!pup_ipc::command_payload(&state, command).has_value());
  assert(!pup_ipc::queue_command(&state, 0, pup_ipc::commands::execute_client_cmd,
    std::string(pup_ipc::command_payload_size, 'x')));
  assert(pup_ipc::allowed_console_command("pup_queue"));
  assert(!pup_ipc::allowed_console_command("pup_queue;quit"));
  pthread_mutexattr_t attributes{};
  assert(pthread_mutexattr_init(&attributes) == 0);
  assert(pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED) == 0);
  assert(pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST) == 0);
  pthread_mutex_t robust_mutex{};
  assert(pthread_mutex_init(&robust_mutex, &attributes) == 0);
  assert(pthread_mutex_lock(&robust_mutex) == 0);
  assert(pthread_mutex_unlock(&robust_mutex) == 0);
  assert(pthread_mutex_destroy(&robust_mutex) == 0);
  assert(pthread_mutexattr_destroy(&attributes) == 0);
  pthread_mutex_destroy(&state.mutex);
  return 0;
}
