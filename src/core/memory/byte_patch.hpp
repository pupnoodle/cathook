/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/memory/byte_patch.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef BYTE_PATCH_HPP
#define BYTE_PATCH_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <initializer_list>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

#include "core/print.hpp"

class byte_patch
{
public:
  byte_patch() = default;

  byte_patch(void* target, std::initializer_list<std::uint8_t> patch_bytes)
      : target_(reinterpret_cast<std::uint8_t*>(target)),
        patch_bytes_(patch_bytes)
  {
  }

  byte_patch(void* target, std::vector<std::uint8_t> patch_bytes)
      : target_(reinterpret_cast<std::uint8_t*>(target)),
        patch_bytes_(std::move(patch_bytes))
  {
  }

  [[nodiscard]] bool valid() const
  {
    return target_ != nullptr && !patch_bytes_.empty();
  }

  bool apply()
  {
    if (!valid())
    {
      return false;
    }

    if (applied_)
    {
      return restore_page_protections();
    }

    if (!capture_page_protections()) {
      return false;
    }

    original_bytes_.resize(patch_bytes_.size());
    std::memcpy(original_bytes_.data(), target_, original_bytes_.size());

    if (!make_pages_writable())
    {
      return false;
    }

    std::memcpy(target_, patch_bytes_.data(), patch_bytes_.size());
    __builtin___clear_cache(reinterpret_cast<char*>(target_), reinterpret_cast<char*>(target_ + patch_bytes_.size()));
    applied_ = true;
    return restore_page_protections();
  }

  bool restore()
  {
    if (!applied_ || target_ == nullptr || original_bytes_.empty())
    {
      return true;
    }

    if (!make_pages_writable())
    {
      return false;
    }

    std::memcpy(target_, original_bytes_.data(), original_bytes_.size());
    __builtin___clear_cache(reinterpret_cast<char*>(target_), reinterpret_cast<char*>(target_ + original_bytes_.size()));
    if (!restore_page_protections()) {
      return false;
    }
    applied_ = false;
    return true;
  }

private:
  struct page_protection
  {
    void* address;
    int protection;
  };

  bool capture_page_protections()
  {
    if (!page_protections_.empty()) {
      return true;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    const auto address = reinterpret_cast<std::uintptr_t>(target_);
    if (page_size <= 0 || patch_bytes_.size() - 1 > std::numeric_limits<std::uintptr_t>::max() - address) {
      return false;
    }
    page_size_ = static_cast<std::size_t>(page_size);
    const auto first = address - address % page_size_;
    const auto last = address + patch_bytes_.size() - 1;
    std::ifstream maps{ "/proc/self/maps" };
    std::string line;
    auto page = first;
    while (std::getline(maps, line)) {
      unsigned long long start = 0;
      unsigned long long end = 0;
      char permissions[5]{};
      if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, permissions) != 3) {
        continue;
      }
      while (page >= start && page < end) {
        if (permissions[0] != 'r') {
          page_protections_.clear();
          return false;
        }
        const int protection = PROT_READ | (permissions[1] == 'w' ? PROT_WRITE : 0) |
          (permissions[2] == 'x' ? PROT_EXEC : 0);
        page_protections_.push_back({ reinterpret_cast<void*>(page), protection });
        if (last - page < page_size_) {
          return true;
        }
        page += page_size_;
      }
    }
    page_protections_.clear();
    return false;
  }

  bool restore_page_protections() const
  {
    bool restored = true;
    for (const auto& page : page_protections_) {
      if (mprotect(page.address, page_size_, page.protection) != 0) {
        print("[byte_patch] failed to restore protection for %p\n", page.address);
        restored = false;
      }
    }
    return restored;
  }

  bool make_pages_writable() const
  {
    for (const auto& page : page_protections_) {
      if (mprotect(page.address, page_size_, page.protection | PROT_WRITE) != 0) {
        print("[byte_patch] mprotect failed for %p\n", page.address);
        restore_page_protections();
        return false;
      }
    }
    return true;
  }

  std::vector<page_protection> page_protections_{};
  std::size_t page_size_ = 0;

  std::uint8_t* target_ = nullptr;
  std::vector<std::uint8_t> patch_bytes_{};
  std::vector<std::uint8_t> original_bytes_{};
  bool applied_ = false;
};

#endif
