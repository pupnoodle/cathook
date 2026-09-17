/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/hooks.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <dlfcn.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <vector>

#include "core/memory/maps.hpp"
#include "core/memory/resolve.hpp"
#include "core/print.hpp"

struct memory_page_permissions
{
  void* page = nullptr;
  int protection = PROT_NONE;
};

long cached_page_size()
{
  static const long page_size = sysconf(_SC_PAGESIZE);
  return page_size;
}

bool query_page_permissions(void* address, memory_page_permissions& page_permissions)
{
  const long page_size_value = cached_page_size();
  if (page_size_value <= 0) {
    print("failed to query page size\n");
    return false;
  }

  const auto page_size = static_cast<std::uintptr_t>(page_size_value);
  const auto address_value = reinterpret_cast<std::uintptr_t>(address);
  page_permissions.page = reinterpret_cast<void*>(address_value & ~(page_size - 1));

  const int protection = cathook::core::memory::protection_at(address);
  if (protection < 0) {
    print("failed to find memory mapping for %p\n", address);
    return false;
  }

  page_permissions.protection = protection;
  return true;
}

bool set_memory_page_protection(const memory_page_permissions& page_permissions, int protection)
{
  const long page_size_value = cached_page_size();
  if (page_size_value <= 0) {
    print("failed to query page size\n");
    return false;
  }

  if (mprotect(page_permissions.page, static_cast<std::size_t>(page_size_value), protection) != 0) {
    print("mprotect failed for page %p\n", page_permissions.page);
    return false;
  }

  return true;
}

bool make_memory_page_writable(const memory_page_permissions& page_permissions)
{
  return set_memory_page_protection(page_permissions, page_permissions.protection | PROT_WRITE);
}

bool restore_memory_page_protection(const memory_page_permissions& page_permissions)
{
  return set_memory_page_protection(page_permissions, page_permissions.protection);
}

bool is_executable_memory_address(const void* address)
{
  if (address == nullptr) {
    return false;
  }

  memory_page_permissions page_permissions{};
  return query_page_permissions(const_cast<void*>(address), page_permissions) &&
    (page_permissions.protection & (PROT_READ | PROT_EXEC)) == (PROT_READ | PROT_EXEC);
}

bool is_valid_code_address(const void* address)
{
  if (!is_executable_memory_address(address)) {
    return false;
  }

  Dl_info info{};
  return ::dladdr(address, &info) != 0 && info.dli_fname != nullptr;
}

namespace
{

std::string find_loaded_library_path(const char* lib_path)
{
  if (lib_path == nullptr || lib_path[0] == '\0') {
    return {};
  }

  return cathook::core::memory::module_path(cathook::core::memory::file_name(lib_path));
}

}

void* open_loaded_library(const char* lib_path)
{
  void* lib_handle = dlopen(lib_path, RTLD_NOLOAD | RTLD_NOW);
  if (lib_handle != nullptr) {
    return lib_handle;
  }

  const auto loaded_path = find_loaded_library_path(lib_path);
  if (loaded_path.empty()) {
    return nullptr;
  }

  return dlopen(loaded_path.c_str(), RTLD_NOLOAD | RTLD_NOW);
}

void* get_interface(const char* lib_path, const char* version) {
  static std::vector<void*> retained_handles{};
  void* lib_handle = open_loaded_library(lib_path);
  if (!lib_handle) {
    print("Failed to load %s\n", lib_path);
    return NULL;
  }

  print("%s loaded at %p\n", lib_path, lib_handle);

  typedef void* (*CreateInterface)(const char*, int*);

  CreateInterface create_interface = (CreateInterface)dlsym(lib_handle, "CreateInterface");
  if (create_interface != nullptr) {
    if (std::find(retained_handles.begin(), retained_handles.end(), lib_handle) == retained_handles.end()) {
      retained_handles.push_back(lib_handle);
    } else {
      dlclose(lib_handle);
    }
  } else {
    dlclose(lib_handle);
  }

  if (!create_interface) {
    print("Failed to get CreateInterface\n");
    return NULL;
  }

  print("%s factory found at %p\n", lib_path, create_interface);

  void* interface =  create_interface(version, NULL);

  if (!interface) {
    print("Failed to get %s interface\n", version);
    return NULL;
  }

  print("%s interface found at %p\n", version, interface);

  return interface;
}

void* read_vtable_entry(void** vtable, int index, const char* hook_name)
{
  if (vtable == nullptr || index < 0) {
    print("%s has an invalid vtable or slot\n", hook_name);
    return nullptr;
  }

  memory_page_permissions page_permissions{};
  if (!query_page_permissions(&vtable[index], page_permissions)) {
    print("%s vtable slot is unmapped\n", hook_name);
    return nullptr;
  }

  if ((page_permissions.protection & PROT_READ) == 0) {
    print("%s vtable slot is not readable\n", hook_name);
    return nullptr;
  }

  void* const entry = vtable[index];
  if (!is_valid_code_address(entry)) {
    print("%s vtable slot %d has non-executable entry %p\n", hook_name, index, entry);
    return nullptr;
  }

  return entry;
}

namespace {

struct cached_vtable_write
{
  void** vtable = nullptr;
  int index = -1;
  void* func = nullptr;
};

constexpr std::size_t max_cached_vtable_writes = 64;
cached_vtable_write cached_vtable_writes[max_cached_vtable_writes]{};
std::size_t cached_vtable_write_count = 0;

bool is_cached_vtable_write(void** vtable, const int index, void* func)
{
  for (std::size_t i = 0; i < cached_vtable_write_count; ++i) {
    const auto& cached = cached_vtable_writes[i];
    if (cached.vtable == vtable && cached.index == index && cached.func == func) {
      return vtable[index] == func;
    }
  }

  return false;
}

void cache_vtable_write(void** vtable, const int index, void* func)
{
  for (std::size_t i = 0; i < cached_vtable_write_count; ++i) {
    auto& cached = cached_vtable_writes[i];
    if (cached.vtable == vtable && cached.index == index) {
      cached.func = func;
      return;
    }
  }

  if (cached_vtable_write_count >= max_cached_vtable_writes) {
    return;
  }

  cached_vtable_writes[cached_vtable_write_count++] = { vtable, index, func };
}

bool write_to_table_impl(void** vtable, int index, void* func, const bool verbose) {
  if (vtable == nullptr || index < 0 || func == nullptr) {
    print("refusing to install an invalid vtable target\n");
    return false;
  }

  if (is_cached_vtable_write(vtable, index, func)) {
    return true;
  }

  if (!is_valid_code_address(func)) {
    print("refusing to install non-executable vtable target %p\n", func);
    return false;
  }

  if (read_vtable_entry(vtable, index, "vtable write") == nullptr) {
    return false;
  }

  memory_page_permissions page_permissions{};

  if (!query_page_permissions(&vtable[index], page_permissions)) {
    return false;
  }

  if (verbose) print("vfunc table page found at %p\n", page_permissions.page);

  if (!make_memory_page_writable(page_permissions)) {
    print("mprotect failed to change page protection\n");
    return false;
  }

  vtable[index] = func;

  if (!restore_memory_page_protection(page_permissions)) {
    print("mprotect failed to reset page protection\n");
    return false;
  }

  cache_vtable_write(vtable, index, func);
  return true;
}

}

bool write_to_table(void** vtable, int index, void* func) {
  return write_to_table_impl(vtable, index, func, true);
}

bool write_pointer_slot(void** slot, void* value)
{
  if (slot == nullptr || !is_valid_code_address(value)) {
    return false;
  }

  memory_page_permissions page_permissions{};
  if (!query_page_permissions(slot, page_permissions)) {
    return false;
  }

  if ((page_permissions.protection & PROT_READ) == 0) {
    return false;
  }

  if (!make_memory_page_writable(page_permissions)) {
    print("mprotect failed for pointer slot %p\n", slot);
    return false;
  }

  *slot = value;

  if (!restore_memory_page_protection(page_permissions)) {
    print("mprotect failed to restore pointer slot page %p\n", slot);
    return false;
  }

  return true;
}

bool get_sdl_wrapper_target(void* func, const char* func_name, void*** ptr_to_func)
{
  if (!is_valid_code_address(func) || ptr_to_func == nullptr) {
    return false;
  }

  auto* slot = static_cast<void**>(cathook::core::memory::resolve_jmp_slot(func));
  if (slot == nullptr) {
    const auto* bytes = static_cast<const std::uint8_t*>(func);
    print("%s wrapper has unexpected prologue %02x %02x\n", func_name, bytes[0], bytes[1]);
    return false;
  }

  *ptr_to_func = slot;
  return true;
}

bool sdl_hook(void* lib_handle, const char* func_name, void* hook, void** original, void*** target = nullptr) {
  void* func = dlsym(lib_handle, func_name);

  if (!func) {
    print("Failed to get %s\n", func_name);
    return false;
  }

  print("%s wrapper found at %p\n", func_name, func);

  void** ptr_to_func = nullptr;
  if (!get_sdl_wrapper_target(func, func_name, &ptr_to_func) || ptr_to_func == nullptr || *ptr_to_func == nullptr ||
      !is_valid_code_address(*ptr_to_func)) {
    print("Failed to resolve %s wrapper target\n", func_name);
    return false;
  }

  void* original_func = *ptr_to_func;

  print("Original %s at %p\n", func_name, original_func);

  if (!write_pointer_slot(ptr_to_func, hook)) {
    return false;
  }

  *original = original_func;
  if (target != nullptr) {
    *target = ptr_to_func;
  }

  return true;
}

bool restore_sdl_hook_target(void** target, void* original)
{
  if (target == nullptr || original == nullptr) {
    return false;
  }

  return write_pointer_slot(target, original);
}
