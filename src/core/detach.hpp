/*
/^-----^\   data: 2026-04-02
V  o o  V  file: src/core/detach.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef DETACH_HPP
#define DETACH_HPP
#include "print.hpp"
#include <atomic>
#include <cstdint>

namespace cathook::core
{

inline std::atomic_bool detach_requested = false;
inline std::atomic_bool detach_started = false;
inline std::atomic_bool detach_complete = false;
inline std::atomic_bool game_hooks_installed = false;
inline std::atomic_uint active_hook_calls = 0;
inline thread_local unsigned hook_call_depth = 0;

bool unload_module_runtime();
bool is_runtime_detached();

struct hook_call_guard
{
  hook_call_guard()
  {
    ++hook_call_depth;
    active_hook_calls.fetch_add(1, std::memory_order_acq_rel);
  }

  ~hook_call_guard()
  {
    active_hook_calls.fetch_sub(1, std::memory_order_acq_rel);
    --hook_call_depth;
  }
};

#define CATHOOK_HOOK_GUARD() ::cathook::core::hook_call_guard cathook_hook_guard{}

inline void request_detach()
{
  detach_requested.store(true, std::memory_order_release);
}

inline bool is_detach_pending()
{
  return detach_requested.load(std::memory_order_acquire) || detach_started.load(std::memory_order_acquire);
}

inline void service_detach_request()
{
  if (!detach_requested.load(std::memory_order_acquire)) {
    return;
  }

  if (detach_started.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

  detach_requested.store(false, std::memory_order_release);

  if (!unload_module_runtime()) {
    print("Detach cleanup failed; retry scheduled\n");
    detach_complete.store(false, std::memory_order_release);
    detach_started.store(false, std::memory_order_release);
    detach_requested.store(true, std::memory_order_release);
  }
}

}
#endif
