#ifndef MATERIAL_MANAGER_HPP
#define MATERIAL_MANAGER_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "games/tf2/sdk/interfaces/material_system.hpp"

struct material_definition {
  Material* material = nullptr;
  std::string name{};
  std::string vmt{};
  bool locked = false;
  MaterialVar* phong_tint = nullptr;
  MaterialVar* envmap_tint = nullptr;
  bool invert_cull = false;
  bool block_occluded = false;
};

class material_manager final {
public:
  bool prepare();
  bool load();
  bool reload();
  void shutdown();
  void abandon();

  [[nodiscard]] std::optional<material_definition> find(const std::string& name) const;
  [[nodiscard]] std::vector<material_definition> definitions() const;
  [[nodiscard]] std::vector<std::string> selectable_names() const;
  [[nodiscard]] std::filesystem::path directory() const;

  bool add(const std::string& name);
  bool edit(const std::string& name, const std::string& vmt);
  bool remove(const std::string& name);
  void set_color(const material_definition* definition, const RGBA_float& color) const;
  [[nodiscard]] bool loaded() const;

private:
  std::unordered_map<std::string, material_definition> materials_{};
  std::vector<material_definition> retired_materials_{};
  mutable std::shared_mutex mutex_{};
  bool prepared_ = false;
  bool loaded_ = false;
  std::uint64_t generation_ = 0;

  Material* create_material(const std::string& name, const std::string& vmt);
  void initialize_material(material_definition& definition);
  void store_material(const std::string& name, const std::string& vmt, bool locked);
  void add_builtin_materials();
  bool prepare_unlocked();
  static void release_material(material_definition& definition);
  void retire_material(material_definition& definition);
};

inline material_manager materials{};

#endif
