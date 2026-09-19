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

#include "core/memory/maps.hpp"
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
      return true;
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

    if (std::memcmp(target_, patch_bytes_.data(), patch_bytes_.size()) != 0)
    {
      print("[byte_patch] %p was patched after us; leaving it alone\n", static_cast<void*>(target_));
      applied_ = false;
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
    auto page = first;
    bool covered = false;
    puphook::core::memory::for_each([&](const puphook::core::memory::entry& region) {
      while (page >= region.start && page < region.end) {
        if ((region.protection & PROT_READ) == 0) {
          page_protections_.clear();
          return false;
        }
        page_protections_.push_back({ reinterpret_cast<void*>(page), region.protection });
        if (last - page < page_size_) {
          covered = true;
          return false;
        }
        page += page_size_;
      }
      return true;
    });
    if (!covered) {
      page_protections_.clear();
    }
    return covered;
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
