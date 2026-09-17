/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/core/ipc/ipc_shared.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef CAT_IPC_SHARED_HPP
#define CAT_IPC_SHARED_HPP

#include "core/ipc/ipc_protocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>

namespace cat_ipc
{

inline auto read_host_pid() -> pid_t;
inline auto read_process_start_time(pid_t pid) -> unsigned long;

class shared_memory
{
public:
  friend class scoped_lock;
  shared_memory() = default;

  shared_memory(const shared_memory&) = delete;
  auto operator=(const shared_memory&) -> shared_memory& = delete;

  shared_memory(shared_memory&& other) noexcept
  {
    move_from(other);
  }

  auto operator=(shared_memory&& other) noexcept -> shared_memory&
  {
    if (this != &other)
    {
      close();
      move_from(other);
    }

    return *this;
  }

  ~shared_memory()
  {
    close();
  }

  [[nodiscard]] static auto create_server(bool reset_existing) -> shared_memory
  {
    ensure_ipc_directory();
    if (reset_existing)
    {
      const std::optional<int> unlink_err = force_unlink_shared_object();
      if (unlink_err.has_value() && (unlink_err.value() == EACCES || unlink_err.value() == EPERM))
      {
        throw std::runtime_error("stale IPC file could not be removed due to insufficient permissions. "
                                 "The file " + std::string{ipc_socket_path} + " is likely owned by another user (e.g. root). "
                                 "Please remove it manually by running: sudo rm -f " + std::string{ipc_socket_path});
      }
    }

    auto memory = shared_memory{};
    for (auto attempt = 0; attempt < 5; ++attempt)
    {
      if (reset_existing)
      {
        const std::optional<int> unlink_err = force_unlink_shared_object();
        if (unlink_err.has_value() && (unlink_err.value() == EACCES || unlink_err.value() == EPERM))
        {
          throw std::runtime_error("stale IPC file could not be removed due to insufficient permissions. "
                                   "The file " + std::string{ipc_socket_path} + " is likely owned by another user (e.g. root). "
                                   "Please remove it manually by running: sudo rm -f " + std::string{ipc_socket_path});
        }
      }

      memory.fd_ = ::open(ipc_socket_path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0660);
      if (memory.fd_ >= 0)
      {
        break;
      }

      if (errno != EEXIST)
      {
        throw std::runtime_error(std::string{"open create failed: "} + std::strerror(errno));
      }

      const std::optional<int> unlink_err = force_unlink_shared_object();
      if (unlink_err.has_value() && (unlink_err.value() == EACCES || unlink_err.value() == EPERM))
      {
        throw std::runtime_error("stale IPC file could not be removed due to insufficient permissions. "
                                 "The file " + std::string{ipc_socket_path} + " is likely owned by another user (e.g. root). "
                                 "Please remove it manually by running: sudo rm -f " + std::string{ipc_socket_path});
      }
      memory.fd_ = -1;
    }

    if (memory.fd_ < 0)
    {
      throw std::runtime_error("open create failed: stale IPC file could not be removed. "
                               "The file " + std::string{ipc_socket_path} + " is likely owned by another user (e.g. root). "
                               "Please remove it manually by running: sudo rm -f " + std::string{ipc_socket_path});
    }

    if (ftruncate(memory.fd_, static_cast<off_t>(sizeof(shared_state))) != 0)
    {
      throw std::runtime_error(std::string{"ftruncate failed: "} + std::strerror(errno));
    }

    memory.map();
    memory.owner_ = true;
    memory.initialize_state();
    return memory;
  }

  [[nodiscard]] static auto open_or_create_server(bool reset_existing) -> shared_memory
  {
    if (reset_existing)
    {
      return create_server(true);
    }

    try
    {
      return open_client();
    }
    catch (const std::exception&)
    {
      if (::access(ipc_socket_path, F_OK) != 0 && errno == ENOENT)
      {
        return create_server(false);
      }
      throw;
    }
  }

  [[nodiscard]] static auto open_client() -> shared_memory
  {
    auto memory = shared_memory{};
    memory.fd_ = ::open(ipc_socket_path, O_RDWR | O_CLOEXEC);
    if (memory.fd_ < 0)
    {
      throw std::runtime_error(std::string{"open failed: "} + std::strerror(errno));
    }

    struct stat object_stat {};
    if (fstat(memory.fd_, &object_stat) != 0 || object_stat.st_size < static_cast<off_t>(sizeof(shared_state)))
    {
      throw std::runtime_error("IPC object is smaller than the expected ABI");
    }

    memory.map();
    if (!valid_state(memory.state_))
    {
      throw std::runtime_error("catbot ipc protocol mismatch");
    }

    return memory;
  }

  void close()
  {
    if (state_ != nullptr)
    {
      munmap(state_, sizeof(shared_state));
      state_ = nullptr;
    }

    if (fd_ >= 0)
    {
      ::close(fd_);
      fd_ = -1;
    }
  }

  void unlink_if_owner()
  {
    if (owner_)
    {
      ::unlink(ipc_socket_path);
      owner_ = false;
    }
  }

  [[nodiscard]] auto state() const -> shared_state*
  {
    return state_;
  }

  [[nodiscard]] bool owns_valid_state() const
  {
    return valid_state(state_);
  }

  [[nodiscard]] bool maps_current_object() const
  {
    if (fd_ < 0 || state_ == nullptr)
    {
      return false;
    }

    struct stat mapped_stat {};
    if (fstat(fd_, &mapped_stat) != 0)
    {
      return false;
    }

    struct stat named_stat {};
    if (stat(ipc_socket_path, &named_stat) != 0)
    {
      return false;
    }

    return mapped_stat.st_dev == named_stat.st_dev && mapped_stat.st_ino == named_stat.st_ino;
  }

  [[nodiscard]] static bool valid_state_for_lock_recovery(const shared_state* state)
  {
    return valid_state(state);
  }

  static void reset_state_for_lock_recovery(shared_state* state)
  {
    reset_recoverable_state(state);
  }

private:
  static void ensure_ipc_directory()
  {
    constexpr std::string_view directory = "/opt/cathook/ipc";
    constexpr std::string_view root = "/opt/cathook";
    if (::mkdir(root.data(), 0750) != 0 && errno != EEXIST)
    {
      throw std::runtime_error(std::string{"mkdir IPC root failed: "} + std::strerror(errno));
    }
    if (::mkdir(directory.data(), 0770) != 0 && errno != EEXIST)
    {
      throw std::runtime_error(std::string{"mkdir IPC directory failed: "} + std::strerror(errno));
    }
  }

  static bool valid_state(const shared_state* state)
  {
    return state != nullptr &&
      state->global_data.magic_number == cathook_magic_number &&
      state->global_data.protocol_version == ipc_protocol_version &&
      state->global_data.abi_version == ipc_abi_version &&
      state->global_data.state_version != 0 &&
      state->command_count < (std::numeric_limits<unsigned long>::max() - command_ring_size) &&
      state->peer_count <= max_peers;
  }

  static void reset_recoverable_state(shared_state* state)
  {
    const auto state_version = state->global_data.state_version == 0 ? 1u : state->global_data.state_version + 1u;
    state->peer_count = 0;
    state->command_count = 0;
    std::memset(state->peer_data, 0, sizeof(state->peer_data));
    std::memset(state->commands, 0, sizeof(state->commands));
    std::memset(state->pool, 0, sizeof(state->pool));
    std::memset(state->peer_user_data, 0, sizeof(state->peer_user_data));
    for (auto& peer : state->peer_data)
    {
      peer.free = true;
    }
    state->global_data.magic_number = cathook_magic_number;
    state->global_data.protocol_version = ipc_protocol_version;
    state->global_data.abi_version = ipc_abi_version;
    state->global_data.state_version = state_version;
    state->global_data.pid = read_host_pid();
    state->global_data.starttime = read_process_start_time(getpid());
  }

  [[nodiscard]] static auto force_unlink_shared_object() -> std::optional<int>
  {
    auto last_err = std::optional<int>{};
    if (::unlink(ipc_socket_path) != 0)
    {
      if (errno != ENOENT)
      {
        last_err = errno;
      }
    }
    return last_err;
  }

  void map()
  {
    auto* ptr = mmap(nullptr, sizeof(shared_state), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr == MAP_FAILED)
    {
      throw std::runtime_error(std::string{"mmap failed: "} + std::strerror(errno));
    }

    state_ = static_cast<shared_state*>(ptr);
  }

  void initialize_state()
  {
    std::memset(state_, 0, sizeof(shared_state));

    auto attributes = pthread_mutexattr_t{};
    if (pthread_mutexattr_init(&attributes) != 0)
    {
      throw std::runtime_error("pthread_mutexattr_init failed");
    }

    if (pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED) != 0)
    {
      pthread_mutexattr_destroy(&attributes);
      throw std::runtime_error("pthread_mutexattr_setpshared failed");
    }

    if (pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST) != 0)
    {
      pthread_mutexattr_destroy(&attributes);
      throw std::runtime_error("pthread_mutexattr_setrobust failed");
    }

    if (pthread_mutex_init(&state_->mutex, &attributes) != 0)
    {
      pthread_mutexattr_destroy(&attributes);
      throw std::runtime_error("pthread_mutex_init failed");
    }

    pthread_mutexattr_destroy(&attributes);

    state_->global_data.magic_number = cathook_magic_number;
    state_->global_data.protocol_version = ipc_protocol_version;
    state_->global_data.abi_version = ipc_abi_version;
    state_->global_data.state_version = 1;
    state_->global_data.pid = read_host_pid();
    state_->global_data.starttime = read_process_start_time(getpid());
    for (auto& peer : state_->peer_data)
    {
      peer.free = true;
    }
  }

  void move_from(shared_memory& other) noexcept
  {
    fd_ = other.fd_;
    state_ = other.state_;
    owner_ = other.owner_;
    other.fd_ = -1;
    other.state_ = nullptr;
    other.owner_ = false;
  }

  int fd_ = -1;
  shared_state* state_ = nullptr;
  bool owner_ = false;
};

class scoped_lock
{
public:
  explicit scoped_lock(shared_state* state) : state_{state}
  {
    if (state_ != nullptr)
    {
      const int result = pthread_mutex_lock(&state_->mutex);
      if (result == EOWNERDEAD)
      {
        shared_memory::reset_state_for_lock_recovery(state_);
        owns_ = pthread_mutex_consistent(&state_->mutex) == 0;
        if (!owns_)
        {
          pthread_mutex_unlock(&state_->mutex);
        }
      }
      else
      {
        owns_ = result == 0;
      }
    }
  }

  scoped_lock(const scoped_lock&) = delete;
  auto operator=(const scoped_lock&) -> scoped_lock& = delete;

  ~scoped_lock()
  {
    if (state_ != nullptr && owns_)
    {
      pthread_mutex_unlock(&state_->mutex);
    }
  }

  [[nodiscard]] bool locked() const
  {
    return owns_;
  }

private:
  shared_state* state_ = nullptr;
  bool owns_ = false;
};

class try_scoped_lock
{
public:
  explicit try_scoped_lock(shared_state* state) : state_{state}
  {
    if (state_ == nullptr)
    {
      return;
    }

    const int result = pthread_mutex_trylock(&state_->mutex);
    if (result == EOWNERDEAD)
    {
      shared_memory::reset_state_for_lock_recovery(state_);
      locked_ = pthread_mutex_consistent(&state_->mutex) == 0;
      if (!locked_)
      {
        pthread_mutex_unlock(&state_->mutex);
      }
    }
    else
    {
      locked_ = result == 0;
    }
  }

  try_scoped_lock(const try_scoped_lock&) = delete;
  auto operator=(const try_scoped_lock&) -> try_scoped_lock& = delete;

  ~try_scoped_lock()
  {
    if (locked_)
    {
      pthread_mutex_unlock(&state_->mutex);
    }
  }

  [[nodiscard]] bool locked() const
  {
    return locked_;
  }

private:
  shared_state* state_ = nullptr;
  bool locked_ = false;
};

inline auto read_host_pid() -> pid_t
{
  std::ifstream status_file{"/proc/self/status"};
  std::string line{};
  while (std::getline(status_file, line))
  {
    if (line.compare(0, 6, "NSpid:") != 0)
    {
      continue;
    }

    std::istringstream stream{line.substr(6)};
    pid_t host_pid = 0;
    if (stream >> host_pid && host_pid > 0)
    {
      return host_pid;
    }

    break;
  }

  return getpid();
}

inline auto now_seconds() -> std::time_t
{
  return std::time(nullptr);
}

inline auto read_process_start_time(pid_t pid) -> unsigned long
{
  std::ifstream stat_file{"/proc/" + std::to_string(pid) + "/stat"};
  std::string stat{};
  std::getline(stat_file, stat);
  const auto close_paren = stat.rfind(')');
  if (close_paren == std::string::npos || close_paren + 2 >= stat.size())
  {
    return 0;
  }

  std::istringstream stream{stat.substr(close_paren + 2)};
  std::string token{};
  for (int index = 0; stream >> token; ++index)
  {
    if (index == 19)
    {
      return static_cast<unsigned long>(std::strtoul(token.c_str(), nullptr, 10));
    }
  }

  return 0;
}

inline void copy_cstr(char* destination, std::size_t destination_size, std::string_view source)
{
  if (destination == nullptr || destination_size == 0)
  {
    return;
  }

  const auto copy_size = std::min(source.size(), destination_size - 1);
  std::memcpy(destination, source.data(), copy_size);
  destination[copy_size] = '\0';
}

inline auto peer_alive(const peer_data_s& peer, std::time_t now = now_seconds()) -> bool
{
  return !peer.free && peer.heartbeat != 0 && now - peer.heartbeat < peer_dead_seconds;
}

inline auto peer_identity_valid(const peer_data_s& peer) -> bool
{
  return peer_alive(peer) && peer.pid > 0 && peer.starttime != 0;
}

inline auto allowed_console_command(std::string_view command) -> bool
{
  if (command.find_first_of(";\r\n") != std::string_view::npos || command.find("&&") != std::string_view::npos)
  {
    return false;
  }
  const auto end = command.find_first_of(" \t\r\n;");
  const auto name = command.substr(0, end);
  constexpr std::array allowed{
    std::string_view{"cat_detach"}, std::string_view{"cat_exec"}, std::string_view{"cat_exec_textmode"},
    std::string_view{"cat_load"}, std::string_view{"cat_save"}, std::string_view{"cat_unlock_achievements"},
    std::string_view{"cat_lock_achievements"}, std::string_view{"cat_unlock_achievement"}, std::string_view{"cat_lock_achievement"},
    std::string_view{"cat_dump_achievements"}, std::string_view{"cat_medal_flip"}, std::string_view{"cat_medal_changer"},
    std::string_view{"cat_autoitem_rent"}, std::string_view{"cat_autoitem_craft"},
    std::string_view{"cat_queue"}, std::string_view{"cat_cancelqueue"}, std::string_view{"cat_abandon"},
    std::string_view{"cat_mvm_fix"}, std::string_view{"cat_mvm_quit"}, std::string_view{"cat_mvm_tele"},
    std::string_view{"cat_mvm_rent"}, std::string_view{"cat_path_to"}, std::string_view{"cat_cancel_path"},
    std::string_view{"cat_kill"}, std::string_view{"cat_menu"}, std::string_view{"cat_party_givelead"},
    std::string_view{"cat_setcvar"}, std::string_view{"cat_getcvar"},
    std::string_view{"cat_criteria"}, std::string_view{"cat_commands"}, std::string_view{"cat_playerlist_print"},
    std::string_view{"cat_playerlist_info"}, std::string_view{"cat_config_get"}, std::string_view{"cat_config_set"},
    std::string_view{"cat_config_toggle"}, std::string_view{"cat_config_reset"}, std::string_view{"cat_config_list"}
  };
  return !name.empty() && std::find(allowed.begin(), allowed.end(), name) != allowed.end();
}

struct payload_view
{
  const char* data = nullptr;
  std::size_t size = 0;

  [[nodiscard]] explicit operator bool() const
  {
    return data != nullptr;
  }

  [[nodiscard]] auto as_string() const -> std::string
  {
    return data == nullptr ? std::string{} : std::string{data, size};
  }
};

inline auto command_payload(shared_state* state, const command_s& command) -> std::optional<payload_view>
{
  if (state == nullptr || command.payload_size == 0 || command.payload_size > command_payload_size)
  {
    return std::nullopt;
  }

  if (command.payload_offset >= command_pool_size ||
      command.payload_size > command_pool_size - command.payload_offset)
  {
    return std::nullopt;
  }

  const auto* payload = reinterpret_cast<const char*>(state->pool + command.payload_offset);
  const auto* terminator = static_cast<const char*>(std::memchr(payload, '\0', command.payload_size));
  if (terminator == nullptr)
  {
    return std::nullopt;
  }

  return payload_view{payload, static_cast<std::size_t>(terminator - payload)};
}

inline auto queue_command(shared_state* state, int target_peer, unsigned int command_type, std::string_view data, int sender = -1) -> bool
{
  if (state == nullptr || data.empty() || data.size() >= command_payload_size ||
      target_peer < -1 || target_peer >= static_cast<int>(max_peers) ||
      sender < -1 || sender >= static_cast<int>(max_peers))
  {
    return false;
  }

  scoped_lock lock{state};
  if (!lock.locked())
  {
    return false;
  }
  if (target_peer >= 0 && !peer_identity_valid(state->peer_data[target_peer]))
  {
    return false;
  }
  if (sender >= 0 && !peer_identity_valid(state->peer_data[sender]))
  {
    return false;
  }
  if (state->command_count >= std::numeric_limits<std::uint32_t>::max() - command_ring_size)
  {
    return false;
  }
  auto& command = state->commands[++state->command_count % command_ring_size];
  std::memset(&command, 0, sizeof(command));

  command.command_number = static_cast<unsigned int>(state->command_count);
  command.target_peer = target_peer;
  command.sender = sender;
  if (sender < 0)
  {
    command.sender_pid = state->global_data.pid;
    command.sender_starttime = state->global_data.starttime;
  }
  else
  {
    command.sender_pid = state->peer_data[sender].pid;
    command.sender_starttime = state->peer_data[sender].starttime;
  }
  command.cmd_type = command_type;

  if (data.size() + 1 <= command_data_size)
  {
    std::memcpy(command.cmd_data, data.data(), data.size());
    command.cmd_data[data.size()] = '\0';
    return true;
  }

  const auto slot = command.command_number % command_ring_size;
  const auto payload_offset = slot * command_payload_size;
  const auto payload_size = data.size() + 1;
  std::memcpy(state->pool + payload_offset, data.data(), payload_size - 1);
  state->pool[payload_offset + payload_size - 1] = '\0';
  command.payload_offset = payload_offset;
  command.payload_size = static_cast<unsigned int>(payload_size);
  return true;
}

inline void sweep_dead_peers(shared_state* state)
{
  if (state == nullptr)
  {
    return;
  }

  scoped_lock lock{state};
  if (!lock.locked())
  {
    return;
  }
  auto count = 0u;
  const auto now = now_seconds();
  for (auto& peer : state->peer_data)
  {
    if (!peer.free && !peer_alive(peer, now))
    {
      peer.free = true;
    }

    if (!peer.free)
    {
      ++count;
    }
  }

  state->peer_count = count;
}

}

#endif
