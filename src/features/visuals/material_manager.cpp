#include "features/visuals/material_manager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>

#include "core/config/config_store.hpp"
#include "core/logger.hpp"
namespace {

std::string lower_copy(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool valid_name(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  const std::filesystem::path path{name};
  return path == path.filename() && name.find_first_of("\\/:*?\"<>|") == std::string::npos;
}

bool truthy_material_key(const std::string& vmt, const char* key) {
  const std::string lower = lower_copy(vmt);
  const std::string needle = lower_copy(key);
  const std::size_t position = lower.find(needle);
  if (position == std::string::npos) return false;
  const std::size_t value = lower.find_first_not_of(" \t\"", position + needle.size());
  return value != std::string::npos && lower.compare(value, 1, "0") != 0;
}

std::string normalize_vmt(std::string vmt) {
  const std::string lower = lower_copy(vmt);
  if (lower.find("$model") != std::string::npos &&
      (lower.find("vertexlitgeneric") == std::string::npos && lower.find("unlitgeneric") == std::string::npos ||
       lower.find("$basetexture") != std::string::npos)) {
    return vmt;
  }
  const std::size_t closing_brace = vmt.rfind('}');
  if (closing_brace == std::string::npos) return vmt;
  std::string additions{};
  if (lower.find("$model") == std::string::npos) additions += "\n\t$model \"1\"";
  if ((lower.find("vertexlitgeneric") != std::string::npos || lower.find("unlitgeneric") != std::string::npos) &&
      lower.find("$basetexture") == std::string::npos) {
    additions += "\n\t$basetexture \"white\"";
  }
  if (additions.empty()) return vmt;
  vmt.insert(closing_brace, additions + "\n");
  return vmt;
}

}

Material* material_manager::create_material(const std::string& name, const std::string& vmt) {
  if (key_values_constructor_original == nullptr || key_values_load_from_buffer_original == nullptr || material_system == nullptr) {
    return nullptr;
  }
  auto* key_values = new KeyValues{name.c_str()};
  if (!key_values->load_from_buffer(name.c_str(), vmt.c_str())) {
    delete key_values;
    return nullptr;
  }
  return material_system->create_material(name.c_str(), key_values);
}

void material_manager::release_material(material_definition& definition) {
  if (definition.material == nullptr) return;

  definition.material->decrement_reference_count();
  definition.material = nullptr;
  definition.phong_tint = nullptr;
  definition.envmap_tint = nullptr;
}

void material_manager::retire_material(material_definition& definition) {
  if (definition.material == nullptr) return;
  definition.phong_tint = nullptr;
  definition.envmap_tint = nullptr;
  retired_materials_.emplace_back(std::move(definition));
  definition = {};
}

void material_manager::initialize_material(material_definition& definition) {
  if (definition.material != nullptr) return;
  definition.material = create_material(
    "monolilth_material_" + std::to_string(++generation_) + "_" + definition.name, definition.vmt);
  if (definition.material == nullptr) return;

  bool found = false;
  definition.phong_tint = definition.material->find_var("$phongtint", &found, false);
  if (!found) definition.phong_tint = nullptr;
  definition.envmap_tint = definition.material->find_var("$envmaptint", &found, false);
  if (!found) definition.envmap_tint = nullptr;
}

void material_manager::store_material(const std::string& name, const std::string& vmt, const bool locked) {
  material_definition definition{};
  definition.name = name;
  definition.vmt = normalize_vmt(vmt);
  definition.locked = locked;
  definition.invert_cull = truthy_material_key(vmt, "$invertcull");
  definition.block_occluded = truthy_material_key(vmt, "$blockoccluded");
  materials_.insert_or_assign(name, std::move(definition));
}

void material_manager::add_builtin_materials() {
  store_material("None", "\"UnlitGeneric\"\n{\n\t$color2 \"[0 0 0]\"\n\t$additive \"1\"\n}", true);
  store_material("Flat", "\"UnlitGeneric\"\n{\n\t$basetexture \"white\"\n}", true);
  store_material("Shaded", "\"VertexLitGeneric\"\n{\n\t$basetexture \"white\"\n}", true);
  store_material("Wireframe", "\"UnlitGeneric\"\n{\n\t$basetexture \"white\"\n\t$wireframe \"1\"\n}", true);
  store_material("Fresnel", "\"VertexLitGeneric\"\n{\n\t$basetexture \"white\"\n\t$bumpmap \"models/player/shared/shared_normal\"\n\t$color2 \"[0 0 0]\"\n\t$additive \"1\"\n\t$phong \"1\"\n\t$phongfresnelranges \"[0 0.5 1]\"\n\t$envmap \"skybox/sky_dustbowl_01\"\n\t$envmapfresnel \"1\"\n}", true);
  store_material("Shine", "\"VertexLitGeneric\"\n{\n\t$additive \"1\"\n\t$envmap \"cubemaps/cubemap_sheen002.hdr\"\n\t$envmaptint \"[1 1 1]\"\n}", true);
  store_material("Tint", "\"VertexLitGeneric\"\n{\n\t$basetexture \"models/player/shared/ice_player\"\n\t$bumpmap \"models/player/shared/shared_normal\"\n\t$additive \"1\"\n\t$phong \"1\"\n\t$phongfresnelranges \"[0 0.001 0.001]\"\n\t$envmap \"skybox/sky_dustbowl_01\"\n\t$envmapfresnel \"1\"\n\t$selfillum \"1\"\n\t$selfillumtint \"[0 0 0]\"\n}", true);
}

bool material_manager::prepare_unlocked() {
  if (prepared_) return true;
  std::error_code error{};
  std::filesystem::create_directories(directory(), error);
  if (error) return false;

  add_builtin_materials();
  for (const auto& entry : std::filesystem::directory_iterator{directory(), error}) {
    if (error || !entry.is_regular_file(error) || entry.path().extension() != ".vmt") continue;
    const std::string name = entry.path().stem().string();
    if (name == "Original" || materials_.contains(name)) continue;
    std::ifstream stream{entry.path(), std::ios::binary};
    if (stream.is_open()) {
      store_material(name, {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}}, false);
    }
  }
  prepared_ = true;
  return true;
}

bool material_manager::prepare() {
  const std::unique_lock lock{mutex_};
  return prepare_unlocked();
}

bool material_manager::load() {
  const std::unique_lock lock{mutex_};
  if (!prepare_unlocked()) return false;
  for (auto& [name, definition] : materials_) initialize_material(definition);
  loaded_ = true;
  return true;
}

bool material_manager::reload() {
  const std::unique_lock lock{mutex_};
  for (auto& [name, definition] : materials_) retire_material(definition);
  materials_.clear();
  prepared_ = false;
  loaded_ = false;
  return prepare_unlocked();
}

void material_manager::shutdown() {
  const std::unique_lock lock{mutex_};
  for (auto& [name, definition] : materials_) release_material(definition);
  for (auto& definition : retired_materials_) release_material(definition);
  materials_.clear();
  retired_materials_.clear();
  prepared_ = false;
  loaded_ = false;
}

void material_manager::abandon() {
  const std::unique_lock lock{mutex_};
  for (auto& [name, definition] : materials_) {
    definition.material = nullptr;
    definition.phong_tint = nullptr;
    definition.envmap_tint = nullptr;
  }
  for (auto& definition : retired_materials_) {
    definition.material = nullptr;
    definition.phong_tint = nullptr;
    definition.envmap_tint = nullptr;
  }
  materials_.clear();
  retired_materials_.clear();
  prepared_ = false;
  loaded_ = false;
}

std::optional<material_definition> material_manager::find(const std::string& name) const {
  if (name.empty() || name.data() == nullptr) return std::nullopt;

  const std::shared_lock lock{mutex_};
  if (name == "Original") return std::nullopt;
  const auto iterator = materials_.find(name);
  return iterator == materials_.end() ? std::nullopt : std::optional<material_definition>{iterator->second};
}

std::vector<material_definition> material_manager::definitions() const {
  const std::shared_lock lock{mutex_};
  std::vector<material_definition> result{};
  result.reserve(materials_.size());
  for (const auto& [name, definition] : materials_) result.emplace_back(definition);
  return result;
}

std::vector<std::string> material_manager::selectable_names() const {
  const std::shared_lock lock{mutex_};
  std::vector<std::string> result{"Original"};
  for (const auto& [name, definition] : materials_) result.emplace_back(name);
  std::ranges::sort(result.begin() + 1, result.end());
  return result;
}

std::filesystem::path material_manager::directory() const {
  return cathook::core::root_directory() / "materials";
}

bool material_manager::add(const std::string& name) {
  const std::unique_lock lock{mutex_};
  if (!valid_name(name) || name == "Original" || materials_.contains(name) || !prepare_unlocked()) return false;
  const std::string vmt{"\"VertexLitGeneric\"\n{\n\t$basetexture \"white\"\n}"};
  std::ofstream stream{directory() / (name + ".vmt"), std::ios::binary | std::ios::trunc};
  stream << vmt;
  if (!stream.good()) return false;
  store_material(name, vmt, false);
  if (loaded_) {
    initialize_material(materials_.at(name));
    if (materials_.at(name).material == nullptr) loaded_ = false;
  }
  return true;
}

bool material_manager::edit(const std::string& name, const std::string& vmt) {
  const std::unique_lock lock{mutex_};
  const auto iterator = materials_.find(name);
  if (iterator == materials_.end() || iterator->second.locked || vmt.empty()) return false;
  const std::string normalized_vmt = normalize_vmt(vmt);
  std::ofstream stream{directory() / (name + ".vmt"), std::ios::binary | std::ios::trunc};
  stream << normalized_vmt;
  if (!stream.good()) return false;
  retire_material(iterator->second);
  iterator->second.name = name;
  iterator->second.vmt = normalized_vmt;
  iterator->second.invert_cull = truthy_material_key(normalized_vmt, "$invertcull");
  iterator->second.block_occluded = truthy_material_key(normalized_vmt, "$blockoccluded");
  if (loaded_) {
    initialize_material(iterator->second);
    if (iterator->second.material == nullptr) loaded_ = false;
  }
  return true;
}

bool material_manager::remove(const std::string& name) {
  const std::unique_lock lock{mutex_};
  const auto iterator = materials_.find(name);
  if (iterator == materials_.end() || iterator->second.locked) return false;
  retire_material(iterator->second);
  materials_.erase(iterator);
  std::error_code error{};
  return std::filesystem::remove(directory() / (name + ".vmt"), error) && !error;
}

void material_manager::set_color(const material_definition* definition, const RGBA_float& color) const {
  if (render_view == nullptr) return;
  const auto resolved_color = color.resolved();
  render_view->set_color_modulation(&resolved_color);
  render_view->set_blend(resolved_color.a);
  if (definition != nullptr) {
    if (definition->phong_tint != nullptr) definition->phong_tint->set_vec_value(color);
    if (definition->envmap_tint != nullptr) definition->envmap_tint->set_vec_value(color);
  }
}

bool material_manager::loaded() const {
  const std::shared_lock lock{mutex_};
  return loaded_;
}
