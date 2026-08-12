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

int protection_from_maps_permissions(const char* permissions)
{
  int protection = PROT_NONE;

  if (permissions[0] == 'r') {
    protection |= PROT_READ;
  }
  if (permissions[1] == 'w') {
    protection |= PROT_WRITE;
  }
  if (permissions[2] == 'x') {
    protection |= PROT_EXEC;
  }

  return protection;
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

  std::ifstream maps{ "/proc/self/maps" };
  std::string line{};
  while (std::getline(maps, line)) {
    unsigned long long region_start = 0;
    unsigned long long region_end = 0;
    char permissions[5] = {};

    if (std::sscanf(line.c_str(), "%llx-%llx %4s", &region_start, &region_end, permissions) != 3) {
      continue;
    }

    if (address_value >= region_start && address_value < region_end) {
      page_permissions.protection = protection_from_maps_permissions(permissions);
      return true;
    }
  }

  print("failed to find memory mapping for %p\n", address);
  return false;
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
    (page_permissions.protection & PROT_EXEC) != 0;
}

namespace
{

std::string_view file_name_from_path(std::string_view path)
{
  const auto offset = path.rfind('/');
  return offset == std::string_view::npos ? path : path.substr(offset + 1);
}

std::string find_loaded_library_path(const char* lib_path)
{
  if (lib_path == nullptr || lib_path[0] == '\0') {
    return {};
  }

  const auto target_name = file_name_from_path(lib_path);
  if (target_name.empty()) {
    return {};
  }

  std::ifstream maps{ "/proc/self/maps" };
  std::string line{};

  while (std::getline(maps, line)) {
    const auto path_offset = line.find('/');
    if (path_offset == std::string::npos) {
      continue;
    }

    const auto path = std::string_view{ line }.substr(path_offset);
    if (file_name_from_path(path) == target_name) {
      return std::string{ path };
    }
  }

  return {};
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
  void* lib_handle = open_loaded_library(lib_path);
  if (!lib_handle) {
    print("Failed to load %s\n", lib_path);
    return NULL;
  }

  print("%s loaded at %p\n", lib_path, lib_handle);

  typedef void* (*CreateInterface)(const char*, int*);

  CreateInterface create_interface = (CreateInterface)dlsym(lib_handle, "CreateInterface");
  dlclose(lib_handle);

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
  if (!is_executable_memory_address(entry)) {
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

  if (func == nullptr || !is_executable_memory_address(func)) {
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

bool write_to_table_quiet(void** vtable, int index, void* func) {
  return write_to_table_impl(vtable, index, func, false);
}

bool write_pointer_slot(void** slot, void* value)
{
  memory_page_permissions page_permissions{};
  if (!query_page_permissions(slot, page_permissions)) {
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
  auto* bytes = reinterpret_cast<std::uint8_t*>(func);
  if (bytes[0] != 0xff || bytes[1] != 0x25) {
    print("%s wrapper has unexpected prologue %02x %02x\n", func_name, bytes[0], bytes[1]);
    return false;
  }

  std::int32_t offset = 0;
  std::memcpy(&offset, bytes + 2, sizeof(offset));
  *ptr_to_func = reinterpret_cast<void**>(bytes + 6 + offset);
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
  if (!get_sdl_wrapper_target(func, func_name, &ptr_to_func) || ptr_to_func == nullptr || *ptr_to_func == nullptr) {
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

bool restore_sdl_hook(void* lib_handle, const char* func_name, void* original) {
  void *func = dlsym(lib_handle, func_name);

  if (!func) {
    print("Failed to get %s\n", func_name);
    return false;
  }

  void** ptr_to_func = nullptr;
  if (!get_sdl_wrapper_target(func, func_name, &ptr_to_func) || ptr_to_func == nullptr) {
    print("Failed to resolve %s wrapper target\n", func_name);
    return false;
  }

  return write_pointer_slot(ptr_to_func, original);
}

bool restore_sdl_hook_target(void** target, void* original)
{
  if (target == nullptr || original == nullptr) {
    return false;
  }

  return write_pointer_slot(target, original);
}
