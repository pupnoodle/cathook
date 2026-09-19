#include "features/visuals/material_manager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>

#include "core/config/config_store.hpp"
#include "core/logger.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"
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

bool has_material_key(const std::string& vmt, const char* key) {
  const std::string lower = lower_copy(vmt);
  const std::string needle = lower_copy(key);
  std::size_t position = 0;
  while ((position = lower.find(needle, position)) != std::string::npos) {
    const bool key_start = position == 0 || std::isspace(static_cast<unsigned char>(lower[position - 1])) ||
      lower[position - 1] == '{';
    const std::size_t after_key = position + needle.size();
    const bool key_end = after_key >= lower.size() || std::isspace(static_cast<unsigned char>(lower[after_key])) ||
      lower[after_key] == '"';
    if (key_start && key_end) return true;
    position = after_key;
  }
  return false;
}

bool truthy_material_key(const std::string& vmt, const char* key) {
  const std::string lower = lower_copy(vmt);
  const std::string needle = lower_copy(key);
  std::size_t position = 0;
  while ((position = lower.find(needle, position)) != std::string::npos) {
    const bool key_start = position == 0 || std::isspace(static_cast<unsigned char>(lower[position - 1])) ||
      lower[position - 1] == '{';
    const std::size_t after_key = position + needle.size();
    const bool key_end = after_key >= lower.size() || std::isspace(static_cast<unsigned char>(lower[after_key])) ||
      lower[after_key] == '"';
    if (!key_start || !key_end) {
      position = after_key;
      continue;
    }
    const std::size_t value = lower.find_first_not_of(" \t\r\n\"", after_key);
    if (value == std::string::npos) return false;
    return lower.compare(value, 1, "0") != 0 && lower.compare(value, 5, "false") != 0;
  }
  return false;
}

std::string normalize_vmt(std::string vmt) {
  const std::string lower = lower_copy(vmt);
  const std::size_t body = lower.find('{');
  const std::string shader = lower.substr(0, body == std::string::npos ? lower.size() : body);
  const bool model_shader = shader.find("vertexlitgeneric") != std::string::npos ||
    shader.find("unlitgeneric") != std::string::npos;
  if (!model_shader) return vmt;
  const std::size_t closing_brace = vmt.rfind('}');
  if (closing_brace == std::string::npos) return vmt;
  std::string additions{};
  if (!has_material_key(lower, "$model")) additions += "\n\t$model \"1\"";
  if (lower.find("$basetexture") == std::string::npos) {
    additions += "\n\t$basetexture \"white\"";
  }
  if (additions.empty()) return vmt;
  vmt.insert(closing_brace, additions + "\n");
  return vmt;
}

}

Material* material_manager::create_material(const std::string& name, const std::string& vmt) {
  if (key_values_system_original == nullptr || key_values_constructor_original == nullptr ||
      key_values_load_from_buffer_original == nullptr || material_system == nullptr) {
    return nullptr;
  }
  auto* key_values = new KeyValues{name.c_str()};
  if (!key_values->load_from_buffer(name.c_str(), vmt.c_str())) {
    key_values->delete_this();
    return nullptr;
  }
  Material* material = material_system->create_material(name.c_str(), key_values);
  // CreateMaterial consumes the KeyValues object, including on failure.
  if (material != nullptr && material->is_error_material()) {
    material->decrement_reference_count();
    material = nullptr;
  }
  return material;
}

void material_manager::release_material(material_definition& definition) {
  if (definition.material == nullptr) return;

  definition.material->decrement_reference_count();
  definition.material = nullptr;
  definition.phong_tint = nullptr;
  definition.envmap_tint = nullptr;
  definition.variables_initialized = false;
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
    "monolilth_material_" + std::to_string(++generation_) + "_" + definition.name,
    normalize_vmt(definition.vmt));
  if (definition.material == nullptr) return;
  definition.material->set_material_flag(MATERIAL_VAR_WIREFRAME, definition.wireframe);
}

void material_manager::store_material(const std::string& name, const std::string& vmt, const bool locked) {
  material_definition definition{};
  definition.name = name;
  definition.vmt = vmt;
  definition.locked = locked;
  const std::string lower_vmt = lower_copy(vmt);
  definition.needs_tint_variables = lower_vmt.find("$phongtint") != std::string::npos ||
    lower_vmt.find("$envmaptint") != std::string::npos;
  definition.wireframe = truthy_material_key(definition.vmt, "$wireframe");
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
  loaded_ = false;
  for (auto& [name, definition] : materials_) {
    initialize_material(definition);
    loaded_ = loaded_ || definition.material != nullptr;
  }
  return loaded_;
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
    definition.variables_initialized = false;
  }
  for (auto& definition : retired_materials_) {
    definition.material = nullptr;
    definition.phong_tint = nullptr;
    definition.envmap_tint = nullptr;
    definition.variables_initialized = false;
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
  return puphook::core::root_directory() / "materials";
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
    if (materials_.at(name).material == nullptr) {
      loaded_ = std::ranges::any_of(materials_, [](const auto& entry) {
        return entry.second.material != nullptr;
      });
    }
  }
  return true;
}

bool material_manager::edit(const std::string& name, const std::string& vmt) {
  const std::unique_lock lock{mutex_};
  const auto iterator = materials_.find(name);
  if (iterator == materials_.end() || iterator->second.locked || vmt.empty()) return false;
  std::ofstream stream{directory() / (name + ".vmt"), std::ios::binary | std::ios::trunc};
  stream << vmt;
  if (!stream.good()) return false;
  retire_material(iterator->second);
  iterator->second.name = name;
  iterator->second.vmt = vmt;
  const std::string lower_vmt = lower_copy(vmt);
  iterator->second.needs_tint_variables = lower_vmt.find("$phongtint") != std::string::npos ||
    lower_vmt.find("$envmaptint") != std::string::npos;
  iterator->second.wireframe = truthy_material_key(vmt, "$wireframe");
  iterator->second.invert_cull = truthy_material_key(vmt, "$invertcull");
  iterator->second.block_occluded = truthy_material_key(vmt, "$blockoccluded");
  if (loaded_) {
    initialize_material(iterator->second);
    if (iterator->second.material == nullptr) {
      loaded_ = std::ranges::any_of(materials_, [](const auto& entry) {
        return entry.second.material != nullptr;
      });
    }
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

Material* material_manager::create_runtime_material(const std::string& name, const std::string& vmt) {
  const std::unique_lock lock{mutex_};
  return create_material(name, vmt);
}

void material_manager::set_color(material_definition* definition, const RGBA_float& color) {
  if (render_view == nullptr) return;
  const auto resolved_color = color.resolved();
  render_view->set_color_modulation(&resolved_color);
  render_view->set_blend(resolved_color.a);
  if (definition == nullptr || definition->material == nullptr || !definition->needs_tint_variables ||
      definition->variables_initialized) {
    if (definition != nullptr && definition->variables_initialized) {
      if (definition->phong_tint != nullptr) definition->phong_tint->set_vec_value(color);
      if (definition->envmap_tint != nullptr) definition->envmap_tint->set_vec_value(color);
    }
    return;
  }

  MaterialVar* phong_tint = nullptr;
  MaterialVar* envmap_tint = nullptr;
  {
    const std::unique_lock lock{mutex_};
    auto iterator = materials_.find(definition->name);
    material_definition* stored = iterator != materials_.end() && iterator->second.material == definition->material
      ? &iterator->second
      : definition;
    if (!stored->variables_initialized) {
      bool found = false;
      stored->phong_tint = stored->material->find_var("$phongtint", &found, false);
      if (!found) stored->phong_tint = nullptr;
      stored->envmap_tint = stored->material->find_var("$envmaptint", &found, false);
      if (!found) stored->envmap_tint = nullptr;
      stored->variables_initialized = true;
    }
    definition->phong_tint = stored->phong_tint;
    definition->envmap_tint = stored->envmap_tint;
    definition->variables_initialized = true;
    phong_tint = definition->phong_tint;
    envmap_tint = definition->envmap_tint;
  }

  if (phong_tint != nullptr) phong_tint->set_vec_value(color);
  if (envmap_tint != nullptr) envmap_tint->set_vec_value(color);
}

bool material_manager::loaded() const {
  const std::shared_lock lock{mutex_};
  return loaded_;
}
