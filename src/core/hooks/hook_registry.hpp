#ifndef HOOK_REGISTRY_HPP
#define HOOK_REGISTRY_HPP

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "core/print.hpp"
#include "libsigscan/libsigscan.h"
#include "funchook/funchook.h"

namespace hooks
{

enum class kind : std::uint8_t
{
  signature,
  vtable,
  sdl_slot,
};

enum class group : std::uint8_t
{
  core,
  deferred,
  sdl,
};

struct entry
{
  const char*  name;
  kind         hook_kind;
  group        hook_group;
  void*        detour;
  void**       original;
  const char*  module;
  const char*  pattern;
  void***      vtable;
  int          index;
  const char*  symbol;
  void***      slot;
  bool         required;
  bool         arm_required;
  bool         armed;
  void*        target;
};

inline entry sig(const char* name, const char* module, const char* pattern,
                 void** original, void* detour, bool required = false, bool arm_required = true)
{
  return { name, kind::signature, group::core, detour, original,
           module, pattern, nullptr, -1, nullptr, nullptr, required, arm_required, false, nullptr };
}

inline entry resolved(const char* name, const char* module, const char* pattern,
                      void** original, bool required = false)
{
  return { name, kind::signature, group::core, nullptr, original,
           module, pattern, nullptr, -1, nullptr, nullptr, required, false, false, nullptr };
}

inline entry pre_resolved(const char* name, void** original, void* detour,
                          bool required = false, bool arm_required = true)
{
  return { name, kind::signature, group::core, detour, original,
           nullptr, nullptr, nullptr, -1, nullptr, nullptr, required, arm_required, false, nullptr };
}

inline entry vmt(const char* name, void*** vtable, int index,
                 void** original, void* detour = nullptr,
                 group hook_group = group::core)
{
  return { name, kind::vtable, hook_group, detour, original,
           nullptr, nullptr, vtable, index, nullptr, nullptr, false, false, false, nullptr };
}

inline entry sdl(const char* symbol, void* detour, void** original, void*** slot)
{
  return { symbol, kind::sdl_slot, group::sdl, detour, original,
           nullptr, nullptr, nullptr, -1, symbol, slot, false, false, false, nullptr };
}

inline std::vector<entry>& all()
{
  static std::vector<entry> entries;
  return entries;
}

inline std::mutex& registry_mutex()
{
  static std::mutex mutex;
  return mutex;
}

inline void add(const entry& e)
{
  std::lock_guard lock{registry_mutex()};
  for (auto& existing : all()) {
    if (std::strcmp(existing.name, e.name) == 0) {
      return;
    }
  }
  all().push_back(e);
}

namespace detail
{

inline funchook_t*& funchook_instance()
{
  static funchook_t* instance = nullptr;
  return instance;
}

inline funchook_t* ensure_funchook()
{
  if (funchook_instance() == nullptr) {
    funchook_instance() = funchook_create();
  }
  return funchook_instance();
}

}

bool resolve_all()
{
  std::lock_guard lock{registry_mutex()};
  bool ok = true;
  for (auto& e : all()) {
    if (e.hook_kind != kind::signature || e.module == nullptr) {
      continue;
    }
    if (e.target == nullptr) {
      e.target = sigscan_module(e.module, e.pattern);
    }
    if (e.target == nullptr) {
      print("Failed to find %s\n", e.name);
      if (e.required) {
        ok = false;
      }
      continue;
    }
    if (e.original != nullptr) {
      *e.original = e.target;
    }
  }
  return ok;
}

namespace detail
{

inline bool arm_vtable(entry& e)
{
  void** vtable = e.vtable != nullptr ? *e.vtable : nullptr;
  if (vtable == nullptr) {
    print("%s vtable unavailable; hook skipped\n", e.name);
    return false;
  }

  void* const original = read_vtable_entry(vtable, e.index, e.name);
  if (original == nullptr) {
    return false;
  }

  *e.original = original;
  if (e.detour != nullptr && !write_to_table(vtable, e.index, e.detour)) {
    return false;
  }

  e.armed = true;
  return true;
}

inline bool arm_signature(entry& e)
{
  void* target = e.target != nullptr ? e.target
    : e.original != nullptr ? *e.original
    : nullptr;
  if (target == nullptr) {
    return true;
  }
  if (e.detour == nullptr) {
    e.armed = true;
    return true;
  }
  if (ensure_funchook() == nullptr) {
    return false;
  }

  void* trampoline = target;
  if (funchook_prepare(funchook_instance(), &trampoline, e.detour) != 0) {
    print("Failed to prepare %s hook: %s\n", e.name, funchook_error_message(funchook_instance()));
    if (e.original != nullptr) {
      *e.original = nullptr;
    }
    return false;
  }

  if (e.original != nullptr) {
    *e.original = trampoline;
  }
  e.armed = true;
  return true;
}

inline bool arm(entry& e)
{
  switch (e.hook_kind) {
  case kind::vtable:
    return arm_vtable(e);
  case kind::signature:
    return arm_signature(e);
  case kind::sdl_slot:
    return true;
  }
  return false;
}

}

bool install_group_unlocked(group hook_group)
{
  bool ok = true;
  for (auto& e : all()) {
    if (e.hook_group != hook_group || e.hook_kind == kind::sdl_slot || e.armed) {
      continue;
    }
    if (!detail::arm(e)) {
      if (e.arm_required) {
        print("Required hook %s failed; aborting puphook startup\n", e.name);
        ok = false;
      } else {
        print("%s hook failed\n", e.name);
      }
    } else if (e.hook_kind == kind::vtable && e.detour != nullptr) {
      print("%s hooked\n", e.name);
    }
  }
  return ok;
}

bool install_group(group hook_group)
{
  std::lock_guard lock{registry_mutex()};
  return install_group_unlocked(hook_group);
}

bool install_all()
{
  std::lock_guard lock{registry_mutex()};
  if (!install_group_unlocked(group::core)) {
    return false;
  }
  if (detail::funchook_instance() != nullptr && funchook_install(detail::funchook_instance(), 0) != 0) {
    print("Inline hook install failed: %s\n", funchook_error_message(detail::funchook_instance()));
    return false;
  }
  return true;
}

bool restore_vtable_slot(entry& e)
{
  void** vtable = e.vtable != nullptr ? *e.vtable : nullptr;
  void* original = e.original != nullptr ? *e.original : nullptr;
  if (vtable == nullptr || original == nullptr) {
    return false;
  }
  if (vtable[e.index] != e.detour) {
    print("%s vtable slot %d was re-hooked after us (%p != %p); leaving it\n",
      e.name, e.index, vtable[e.index], e.detour);
    return true;
  }
  return write_to_table(vtable, e.index, original);
}

bool disable(const char* name)
{
  std::lock_guard lock{registry_mutex()};
  for (auto& e : all()) {
    if (std::strcmp(e.name, name) != 0) {
      continue;
    }
    if (e.armed && e.hook_kind == kind::vtable) {
      restore_vtable_slot(e);
      e.armed = false;
    }
    e.detour = nullptr;
    e.target = nullptr;
    if (e.original != nullptr) {
      *e.original = nullptr;
    }
    return true;
  }
  return false;
}

bool group_armed(group hook_group)
{
  std::lock_guard lock{registry_mutex()};
  for (auto& e : all()) {
    if (e.hook_group == hook_group && e.detour != nullptr && !e.armed) {
      return false;
    }
  }
  return true;
}

bool install_sdl(void* lib_handle)
{
  std::lock_guard lock{registry_mutex()};
  bool ok = true;
  for (auto& e : all()) {
    if (e.hook_kind != kind::sdl_slot || e.armed) {
      continue;
    }
    if (sdl_hook(lib_handle, e.symbol, e.detour, e.original, e.slot)) {
      e.armed = true;
    } else {
      print("Failed to hook %s\n", e.symbol);
      ok = false;
    }
  }
  return ok;
}

bool restore_sdl()
{
  std::lock_guard lock{registry_mutex()};
  bool ok = true;
  for (auto& e : all()) {
    if (e.hook_kind != kind::sdl_slot || !e.armed) {
      continue;
    }
    void** slot = e.slot != nullptr ? *e.slot : nullptr;
    void* original = e.original != nullptr ? *e.original : nullptr;
    if (!restore_sdl_hook_target(slot, original)) {
      print("Failed to restore %s\n", e.symbol);
      ok = false;
      continue;
    }
    if (e.slot != nullptr) {
      *e.slot = nullptr;
    }
    if (e.original != nullptr) {
      *e.original = nullptr;
    }
    e.armed = false;
  }
  return ok;
}

bool restore_all()
{
  std::lock_guard lock{registry_mutex()};
  bool hooks_restored = true;
  for (auto& e : all()) {
    if (e.hook_kind != kind::vtable || !e.armed) {
      continue;
    }
    if (e.detour == nullptr) {
      e.armed = false;
      continue;
    }
    if (!restore_vtable_slot(e)) {
      print("%s failed to restore hook\n", e.name);
      hooks_restored = false;
      continue;
    }
    e.armed = false;
  }

  if (detail::funchook_instance() != nullptr) {
    const int result = funchook_uninstall(detail::funchook_instance(), 0);
    if (result != 0 && result != FUNCHOOK_ERROR_NOT_INSTALLED) {
      print("Failed to uninstall inline hooks: %d\n", result);
      hooks_restored = false;
    } else {
      funchook_destroy(detail::funchook_instance());
      detail::funchook_instance() = nullptr;
      for (auto& e : all()) {
        if (e.hook_kind == kind::signature) {
          e.armed = false;
        }
      }
    }
  }

  return hooks_restored;
}

void clear()
{
  std::lock_guard lock{registry_mutex()};
  for (auto& e : all()) {
    if (e.original != nullptr) {
      *e.original = nullptr;
    }
    if (e.slot != nullptr) {
      *e.slot = nullptr;
    }
    e.target = nullptr;
    e.armed = false;
  }
}

}
#endif
