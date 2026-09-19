#include "features/visuals/groups/visual_groups.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include "core/math/math.hpp"
#include "core/entity_cache.hpp"
#include "core/dormancy.hpp"
#include "core/player_manager.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "games/tf2/sdk/base_handle.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include <functional>
#include <vector>

namespace
{

constexpr std::size_t visual_group_not_found = static_cast<std::size_t>(-1);

}

namespace visual_groups
{

struct visual_group_snapshot
{
  std::vector<visual_group> groups{};
  uint32_t active_group_mask = 0;
  uint32_t overlay_targets = 0;
  uint32_t model_targets = 0;
  bool need_screen_overlay = false;
  bool need_model_effects = false;
  bool need_backtrack_visualizer = false;
  bool need_pickup_timers = false;
  bool need_head_emojis = false;
  std::unordered_map<std::uint32_t, std::size_t> entity_groups{};
  std::unordered_map<std::uint32_t, std::size_t> model_groups{};
};

}

namespace
{

std::atomic<std::shared_ptr<const visual_groups::visual_group_snapshot>> g_group_snapshot{};
thread_local bool g_fake_angle_model = false;
thread_local bool g_viewmodel_model = false;
std::uint32_t next_group_id = 1;

[[nodiscard]] std::uint32_t snapshot_handle_for_entity(Entity* entity)
{
  if (entity == nullptr) {
    return invalid_ehandle_index;
  }

  const auto handle = static_cast<std::uint32_t>(entity->get_ref_handle());
  return handle == 0 ? invalid_ehandle_index : handle;
}

[[nodiscard]] Entity* entity_from_snapshot_handle(std::uint32_t handle)
{
  if (entity_list == nullptr || handle == 0 || handle == invalid_ehandle_index) {
    return nullptr;
  }

  return entity_list->entity_from_handle(static_cast<int>(handle));
}

[[nodiscard]] bool text_contains(std::string_view text, std::string_view needle)
{
  if (needle.empty()) {
    return true;
  }

  if (text.find(needle) != std::string_view::npos) {
    return true;
  }

  return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
  }) != text.end();
}

[[nodiscard]] bool text_equals(std::string_view left, std::string_view right)
{
  return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char lhs, char rhs) {
    return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
  });
}

[[nodiscard]] bool text_starts_with(std::string_view text, std::string_view prefix)
{
  return text.size() >= prefix.size() && text_equals(text.substr(0, prefix.size()), prefix);
}

[[nodiscard]] std::string_view safe_text(const char* value)
{
  return value != nullptr ? std::string_view{value} : std::string_view{};
}

[[nodiscard]] Player* as_player(Entity* entity)
{
  return entity != nullptr && entity->get_class_id() == class_id::PLAYER ? reinterpret_cast<Player*>(entity) : nullptr;
}

[[nodiscard]] Player* owner_player_for_entity(Entity* entity)
{
  if (entity == nullptr) {
    return nullptr;
  }

  if (auto* player = as_player(entity)) {
    return player;
  }

  auto* owner = entity->get_owner_entity();
  if (auto* player = as_player(owner)) {
    return player;
  }

  if (auto* parent = entity->move_parent()) {
    if (auto* player = as_player(parent)) {
      return player;
    }
  }

  if (entity->is_building()) {
    auto* builder = reinterpret_cast<Building*>(entity)->get_owner_entity();
    if (auto* player = as_player(builder)) {
      return player;
    }
  }

  return nullptr;
}

[[nodiscard]] bool is_priority_player(Player* player)
{
  if (player == nullptr) {
    return false;
  }

  return puphook::core::players::is_prioritized(puphook::core::players::account_id_for_player_index(player->get_index()));
}

[[nodiscard]] bool is_pup_player(Player* player)
{
  if (player == nullptr) {
    return false;
  }

  return puphook::core::players::is_pup(puphook::core::players::account_id_for_player_index(player->get_index()));
}

[[nodiscard]] bool is_party_player(Player* player)
{
  if (player == nullptr) {
    return false;
  }

  return puphook::core::players::state_for(puphook::core::players::account_id_for_player_index(player->get_index())) == puphook::core::players::player_state::party;
}

[[nodiscard]] bool is_target_player(Player* player)
{
  return player != nullptr && player == aimbot::active_target_player();
}

[[nodiscard]] uint32_t player_class_flag(Player* player)
{
  if (player == nullptr) {
    return 0;
  }

  switch (player->get_tf_class()) {
    case tf_class::SCOUT:
      return visual_group::player_scout;
    case tf_class::SOLDIER:
      return visual_group::player_soldier;
    case tf_class::PYRO:
      return visual_group::player_pyro;
    case tf_class::DEMOMAN:
      return visual_group::player_demoman;
    case tf_class::HEAVYWEAPONS:
      return visual_group::player_heavy;
    case tf_class::ENGINEER:
      return visual_group::player_engineer;
    case tf_class::MEDIC:
      return visual_group::player_medic;
    case tf_class::SNIPER:
      return visual_group::player_sniper;
    case tf_class::SPY:
      return visual_group::player_spy;
    default:
      return 0;
  }
}

[[nodiscard]] bool player_is_invisible(Player* player)
{
  return player != nullptr && player->is_cloaked();
}

[[nodiscard]] bool player_filter_matches(const visual_group& group, Player* player)
{
  if (player == nullptr || !player->is_alive()) {
    return false;
  }

  if ((group.players & visual_group::player_classes) != 0 && (group.players & player_class_flag(player)) == 0) {
    return false;
  }

  if ((group.players & visual_group::player_conditions) == 0) {
    return true;
  }

  if ((group.players & visual_group::player_invulnerable) != 0 && !player->is_invulnerable()) {
    return false;
  }

  if ((group.players & visual_group::player_crits) != 0 && !player->is_crit_boosted()) {
    return false;
  }

  if ((group.players & visual_group::player_invisible) != 0 && !player_is_invisible(player)) {
    return false;
  }

  if ((group.players & visual_group::player_not_invisible) != 0 && player_is_invisible(player)) {
    return false;
  }

  if ((group.players & visual_group::player_disguise) != 0 && !player->in_cond(TF_COND_DISGUISED)) {
    return false;
  }

  if ((group.players & visual_group::player_hurt) != 0 && player->get_health() >= player->get_max_health()) {
    return false;
  }

  return true;
}

[[nodiscard]] uint32_t building_class_flag(Entity* entity)
{
  if (entity == nullptr) {
    return 0;
  }

  switch (entity->get_class_id()) {
    case class_id::SENTRY:
      return visual_group::building_sentry;
    case class_id::DISPENSER:
    case class_id::OBJECT_CART_DISPENSER:
      return visual_group::building_dispenser;
    case class_id::TELEPORTER:
      return visual_group::building_teleporter;
    default:
      return 0;
  }
}

[[nodiscard]] bool building_filter_matches(const visual_group& group, Entity* entity)
{
  if (entity == nullptr || !entity->is_building()) {
    return false;
  }

  auto* building = reinterpret_cast<Building*>(entity);
  if (building->get_health() <= 0 || building->is_carried()) {
    return false;
  }

  if ((group.buildings & visual_group::building_classes) != 0 && (group.buildings & building_class_flag(entity)) == 0) {
    return false;
  }

  if ((group.buildings & visual_group::building_hurt) != 0 && building->get_health() >= building->get_max_health()) {
    return false;
  }

  return true;
}

[[nodiscard]] uint32_t projectile_class_flag(Entity* entity)
{
  if (entity == nullptr) {
    return 0;
  }

  const std::string_view network_name = safe_text(entity->get_network_name());
  const std::string_view model_name = safe_text(entity->get_model_name());

  switch (entity->get_class_id()) {
    case class_id::ROCKET:
      return visual_group::projectile_rocket;
    case class_id::PILL_OR_STICKY:
      return text_contains(model_name, "sticky") ? visual_group::projectile_sticky : visual_group::projectile_pipe;
    case class_id::ARROW:
      return visual_group::projectile_arrow;
    case class_id::CROSSBOW_BOLT:
      return visual_group::projectile_heal;
    case class_id::FLARE:
      return visual_group::projectile_flare;
    default:
      break;
  }

  if (text_contains(network_name, "rocket")) {
    return visual_group::projectile_rocket;
  }
  if (text_contains(network_name, "pipebomb") || text_contains(model_name, "pipebomb")) {
    return text_contains(model_name, "sticky") ? visual_group::projectile_sticky : visual_group::projectile_pipe;
  }
  if (text_contains(network_name, "arrow")) {
    return visual_group::projectile_arrow;
  }
  if (text_contains(network_name, "healingbolt")) {
    return visual_group::projectile_heal;
  }
  if (text_contains(network_name, "flare")) {
    return visual_group::projectile_flare;
  }
  if (text_contains(network_name, "balloffire")) {
    return visual_group::projectile_fire;
  }
  if (text_contains(network_name, "cleaver")) {
    return visual_group::projectile_cleaver;
  }
  if (text_contains(network_name, "jarmilk") || text_contains(network_name, "bread")) {
    return visual_group::projectile_milk;
  }
  if (text_contains(network_name, "jargas")) {
    return visual_group::projectile_gas;
  }
  if (text_contains(network_name, "jar")) {
    return visual_group::projectile_jarate;
  }
  if (text_contains(network_name, "stunball")) {
    return visual_group::projectile_baseball;
  }
  if (text_contains(network_name, "energy")) {
    return visual_group::projectile_energy;
  }
  if (text_contains(network_name, "mechanicalarm")) {
    return visual_group::projectile_short_circuit;
  }
  if (text_contains(network_name, "meteorshower")) {
    return visual_group::projectile_meteor_shower;
  }
  if (text_contains(network_name, "lightning")) {
    return visual_group::projectile_lightning;
  }
  if (text_contains(network_name, "fireball")) {
    return visual_group::projectile_fireball;
  }
  if (text_contains(network_name, "merasmusgrenade")) {
    return visual_group::projectile_bomb;
  }
  if (text_contains(network_name, "spellbats")) {
    return visual_group::projectile_bats;
  }
  if (text_contains(network_name, "pumpkin")) {
    return visual_group::projectile_pumpkin;
  }
  if (text_contains(network_name, "spawnboss")) {
    return visual_group::projectile_monoculus;
  }
  if (text_contains(network_name, "spawnhorde") || text_contains(network_name, "spawnzombie")) {
    return visual_group::projectile_skeleton;
  }

  return visual_group::projectile_misc;
}

[[nodiscard]] bool is_projectile(Entity* entity)
{
  if (entity == nullptr) {
    return false;
  }

  switch (entity->get_class_id()) {
    case class_id::ROCKET:
    case class_id::PILL_OR_STICKY:
    case class_id::FLARE:
    case class_id::ARROW:
    case class_id::CROSSBOW_BOLT:
      return true;
    default:
      break;
  }

  const std::string_view network_name = safe_text(entity->get_network_name());
  return text_starts_with(network_name, "CTFProjectile_") ||
    text_equals(network_name, "CTFGrenadePipebombProjectile") ||
    text_equals(network_name, "CTFGrenadeCaltropProjectile") ||
    text_equals(network_name, "CTFStunBall") ||
    text_equals(network_name, "CTFBall_Ornament");
}

[[nodiscard]] bool read_bool_netvar(Entity* entity, const char* prop_name)
{
  if (entity == nullptr || prop_name == nullptr) {
    return false;
  }

  const auto* client_class = entity->get_typed_client_class();
  if (client_class == nullptr || client_class->recv_table_ptr == nullptr) {
    return false;
  }

  auto* table = client_class->recv_table_ptr;
  struct cached_offsets {
    int critical = 0;
    int minicrit = 0;
    bool resolved = false;
  };
  static std::unordered_map<const tf2_netvars::recv_table*, cached_offsets> offset_cache{};
  auto& cached = offset_cache[table];
  if (!cached.resolved) {
    const std::vector<const char*> crit_path{"m_bCritical"};
    const std::vector<const char*> mini_path{"m_bMiniCrit"};
    std::vector<tf2_netvars::recv_table*> visiting{};
    cached.critical = tf2_netvars::find_offset_in_table(table, crit_path, 0, 0, visiting);
    visiting.clear();
    cached.minicrit = tf2_netvars::find_offset_in_table(table, mini_path, 0, 0, visiting);
    cached.resolved = true;
  }

  const int offset = std::strcmp(prop_name, "m_bCritical") == 0 ? cached.critical : cached.minicrit;
  if (offset <= 0) {
    return false;
  }

  return *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(entity) + static_cast<uintptr_t>(offset));
}

[[nodiscard]] bool projectile_is_critical(Entity* entity)
{
  return read_bool_netvar(entity, "m_bCritical");
}

[[nodiscard]] bool projectile_is_minicrit(Entity* entity)
{
  return read_bool_netvar(entity, "m_bMiniCrit");
}

[[nodiscard]] bool projectile_filter_matches(const visual_group& group, Entity* entity)
{
  if (!is_projectile(entity)) {
    return false;
  }

  if ((group.projectiles & visual_group::projectile_classes) != 0 && (group.projectiles & projectile_class_flag(entity)) == 0) {
    return false;
  }

  const uint32_t projectile_conditions = group.projectiles & visual_group::projectile_conditions;
  if (projectile_conditions != 0) {
    bool condition_matches = false;
    if ((projectile_conditions & visual_group::projectile_crit) != 0 && projectile_is_critical(entity)) {
      condition_matches = true;
    }
    if ((projectile_conditions & visual_group::projectile_minicrit) != 0 && projectile_is_minicrit(entity)) {
      condition_matches = true;
    }
    if (!condition_matches) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool is_ragdoll(Entity* entity)
{
  if (entity == nullptr) {
    return false;
  }

  return text_contains(safe_text(entity->get_network_name()), "ragdoll");
}

[[nodiscard]] bool is_npc(Entity* entity)
{
  if (entity == nullptr) {
    return false;
  }

  const std::string_view name = safe_text(entity->get_network_name());
  return text_contains(name, "boss") ||
    text_contains(name, "merasmus") ||
    text_contains(name, "eyeball") ||
    text_contains(name, "headless") ||
    text_contains(name, "zombie") ||
    text_contains(name, "tank");
}

[[nodiscard]] bool is_money(Entity* entity)
{
  return entity != nullptr && (text_contains(safe_text(entity->get_network_name()), "currency") || text_contains(safe_text(entity->get_model_name()), "currencypack"));
}

[[nodiscard]] bool is_powerup(Entity* entity)
{
  return entity != nullptr && (text_contains(safe_text(entity->get_network_name()), "powerup") || text_contains(safe_text(entity->get_model_name()), "powerup"));
}

[[nodiscard]] bool is_spellbook(Entity* entity)
{
  return entity != nullptr && text_contains(safe_text(entity->get_model_name()), "spellbook");
}

[[nodiscard]] bool is_bomb(Entity* entity)
{
  if (entity == nullptr) {
    return false;
  }

  const std::string_view network_name = safe_text(entity->get_network_name());
  const std::string_view model_name = safe_text(entity->get_model_name());
  return text_contains(network_name, "tfpumpkinbomb") ||
    text_contains(network_name, "tf_pumpkin_bomb") ||
    text_contains(model_name, "pumpkin_explode");
}

[[nodiscard]] bool is_gargoyle(Entity* entity)
{
  return entity != nullptr && (text_contains(safe_text(entity->get_network_name()), "halloweengift") || text_contains(safe_text(entity->get_model_name()), "gargoyle"));
}

[[nodiscard]] bool is_viewmodel_entity(Entity* entity)
{
  if (entity == nullptr) {
    return false;
  }

  const std::string_view network_name = safe_text(entity->get_network_name());
  const std::string_view model_name = safe_text(entity->get_model_name());
  return entity->get_class_id() == class_id::WEARABLE_VM ||
    text_contains(network_name, "viewmodel") ||
    text_contains(network_name, "wearablevm") ||
    text_contains(model_name, "viewmodel") ||
    text_contains(model_name, "c_arms") ||
    text_contains(model_name, "/v_");
}

[[nodiscard]] bool is_viewmodel_hands(Entity* entity)
{
  if (!is_viewmodel_entity(entity)) {
    return false;
  }

  const std::string_view network_name = safe_text(entity->get_network_name());
  const std::string_view model_name = safe_text(entity->get_model_name());
  return entity->get_class_id() == class_id::WEARABLE_VM ||
    text_contains(network_name, "wearablevm") ||
    text_contains(model_name, "arms") ||
    text_contains(model_name, "hands");
}

[[nodiscard]] uint32_t target_for_entity(Entity* entity, bool models, Player* localplayer)
{
  if (entity == nullptr) {
    return 0;
  }

  if (models) {
    if (g_viewmodel_model && localplayer != nullptr && entity != localplayer->to_entity()) {
      auto* owner = owner_player_for_entity(entity);
      if (owner == localplayer && entity->is_base_combat_weapon()) {
        return visual_group::target_viewmodel_weapon;
      }
    }

    if (is_viewmodel_hands(entity)) {
      return visual_group::target_viewmodel_hands;
    }
    if (is_viewmodel_entity(entity)) {
      return visual_group::target_viewmodel_weapon;
    }

    if (auto* owner = owner_player_for_entity(entity); owner != nullptr && owner->to_entity() != entity) {
      return visual_group::target_players;
    }
  }

  switch (entity->get_class_id()) {
    case class_id::PLAYER:
      return visual_group::target_players;
    case class_id::SENTRY:
    case class_id::DISPENSER:
    case class_id::OBJECT_CART_DISPENSER:
    case class_id::TELEPORTER:
      return visual_group::target_buildings;
    case class_id::ROCKET:
    case class_id::PILL_OR_STICKY:
    case class_id::ARROW:
    case class_id::CROSSBOW_BOLT:
    case class_id::FLARE:
    case class_id::SENTRY_ROCKET:
      return visual_group::target_projectiles;
    case class_id::CAPTURE_FLAG:
      return visual_group::target_objective;
    case class_id::HEALTH_PACK:
      return visual_group::target_health;
    case class_id::AMMO:
      return visual_group::target_ammo;
    case class_id::AMMO_OR_HEALTH_PACK: {
      const enum pickup_type pickup = entity->get_pickup_type();
      if (pickup == pickup_type::MEDKIT) {
        return visual_group::target_health;
      }
      if (pickup == pickup_type::AMMOPACK) {
        return visual_group::target_ammo;
      }
      break;
    }
    case class_id::MVM_CURRENCY:
      return visual_group::target_money;
    case class_id::PUMPKIN:
      return visual_group::target_bombs;
    default:
      break;
  }

  if (is_projectile(entity)) {
    return visual_group::target_projectiles;
  }
  if (is_ragdoll(entity)) {
    return visual_group::target_ragdolls;
  }
  if (is_npc(entity)) {
    return visual_group::target_npcs;
  }
  if (is_money(entity)) {
    return visual_group::target_money;
  }
  if (is_powerup(entity)) {
    return visual_group::target_powerups;
  }
  if (is_spellbook(entity)) {
    return visual_group::target_spellbook;
  }
  if (is_bomb(entity)) {
    return visual_group::target_bombs;
  }
  if (is_gargoyle(entity)) {
    return visual_group::target_gargoyle;
  }

  return 0;
}

[[nodiscard]] bool team_conditions_match(const visual_group& group, Entity* entity, Player* localplayer, Player* owner)
{
  if (localplayer == nullptr) {
    return false;
  }

  if (owner != nullptr) {
    if ((group.conditions & visual_group::condition_local) != 0 && owner == localplayer) {
      return true;
    }
    if ((group.conditions & visual_group::condition_friends) != 0 && owner->is_friend()) {
      return true;
    }
    if ((group.conditions & visual_group::condition_ignored) != 0 && owner->is_ignored()) {
      return true;
    }
    if ((group.conditions & visual_group::condition_pup) != 0 && is_pup_player(owner)) {
      return true;
    }
    if ((group.conditions & visual_group::condition_party) != 0 && is_party_player(owner)) {
      return true;
    }
    if ((group.conditions & visual_group::condition_priority) != 0 && is_priority_player(owner)) {
      return true;
    }
    if ((group.conditions & visual_group::condition_target) != 0 && is_target_player(owner)) {
      return true;
    }
  }

  const tf_team entity_team = owner != nullptr ? owner->get_team() : entity->get_team();
  if (entity_team == tf_team::BLU && (group.conditions & visual_group::condition_blu) == 0) {
    return false;
  }
  if (entity_team == tf_team::RED && (group.conditions & visual_group::condition_red) == 0) {
    return false;
  }
  if (entity_team == tf_team::UNKNOWN || entity_team == tf_team::SPECTATOR) {
    return true;
  }

  if (entity_team == localplayer->get_team()) {
    return (group.conditions & visual_group::condition_team) != 0;
  }

  return (group.conditions & visual_group::condition_enemy) != 0;
}

[[nodiscard]] bool role_conditions_match(const visual_group& group, Player* owner)
{
  if (group.roles.empty()) {
    return true;
  }
  if (owner == nullptr) {
    return false;
  }

  const std::uint32_t account_id = puphook::core::players::account_id_for_player_index(owner->get_index());
  return std::ranges::any_of(group.roles, [account_id](const int role) {
    return puphook::core::players::role_matches(account_id, role);
  });
}

[[nodiscard]] bool target_filters_match(const visual_group& group, Entity* entity, uint32_t target)
{
  if (target == visual_group::target_players) {
    if (auto* player = as_player(entity)) {
      return player_filter_matches(group, player);
    }

    return player_filter_matches(group, owner_player_for_entity(entity));
  }

  if (target == visual_group::target_buildings) {
    return building_filter_matches(group, entity);
  }

  if (target == visual_group::target_projectiles) {
    return projectile_filter_matches(group, entity);
  }

  return true;
}

[[nodiscard]] bool group_matches_entity(const visual_group& group, Entity* entity, Player* localplayer, bool models,
  uint32_t target, Player* owner)
{
  if (entity == nullptr || localplayer == nullptr) {
    return false;
  }

  const bool dormant = entity->is_dormant();
  if (dormant && (!config.visuals.dormant_esp || !dormancy::usable(entity))) {
    return false;
  }
  if ((group.conditions & visual_group::condition_dormant) != 0) {
    if (!dormant) {
      return false;
    }
  } else if (dormant && !config.visuals.dormant_esp) {
    return false;
  }

  if (models && g_fake_angle_model && entity == localplayer->to_entity() && anti_aim::has_visual_angles() && (group.targets & visual_group::target_fake_angle) != 0) {
    return true;
  }

  if (target == 0 || (group.targets & target) == 0) {
    return false;
  }

  if (!target_filters_match(group, entity, target)) {
    return false;
  }

  if (!role_conditions_match(group, owner)) {
    return false;
  }
  return team_conditions_match(group, entity, localplayer, owner);
}

[[nodiscard]] std::size_t find_group_index(Entity* entity, Player* localplayer, bool models, const visual_groups::visual_group_snapshot& snapshot)
{
  if (snapshot.active_group_mask == 0 || snapshot.groups.empty() || entity == nullptr || localplayer == nullptr) {
    return visual_group_not_found;
  }

  const uint32_t target = target_for_entity(entity, models, localplayer);
  Player* owner = owner_player_for_entity(entity);
  const std::vector<visual_group>& groups = snapshot.groups;

  for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
    if ((snapshot.active_group_mask & (1u << group_index)) == 0) {
      continue;
    }

    const auto& group = groups[group_index];
    if (group_matches_entity(group, entity, localplayer, models, target, owner)) {
      return group_index;
    }
  }

  return visual_group_not_found;
}

[[nodiscard]] visual_group make_group(const char* name, uint32_t targets, uint32_t conditions, RGBA_float color, uint32_t esp_mask)
{
  visual_group group{};
  group.bind_id = visual_groups::allocate_group_id();
  group.name = name;
  group.targets = targets;
  group.conditions = conditions;
  group.color = color;
  group.esp.draw_mask = esp_mask;
  return group;
}

[[nodiscard]] uint32_t active_bit(std::size_t index)
{
  return index < visual_group_config::max_groups ? 1u << index : 0u;
}

[[nodiscard]] bool group_has_screen_overlay(const visual_group& group)
{
  return group.esp.draw_mask != 0 ||
    group.offscreen_arrows ||
    group.pickup_timer ||
    (group.backtrack & visual_group::backtrack_enabled) != 0 ||
    (group.trajectory & visual_group::trajectory_enabled) != 0 ||
    (group.sightlines & visual_group::sightline_enabled) != 0;
}

void update_snapshot_capabilities(visual_groups::visual_group_snapshot& snapshot)
{
  snapshot.need_screen_overlay = false;
  snapshot.need_model_effects = false;
  snapshot.need_backtrack_visualizer = false;
  snapshot.need_pickup_timers = false;
  snapshot.need_head_emojis = false;
  snapshot.overlay_targets = 0;
  snapshot.model_targets = 0;

  if (snapshot.active_group_mask == 0) {
    return;
  }

  for (std::size_t index = 0; index < snapshot.groups.size(); ++index) {
    if ((snapshot.active_group_mask & active_bit(index)) == 0) {
      continue;
    }

    const auto& group = snapshot.groups[index];
    snapshot.need_screen_overlay = snapshot.need_screen_overlay || group_has_screen_overlay(group);
    snapshot.need_model_effects = snapshot.need_model_effects || group.chams.active() || group.glow.active() ||
      group.backtrack_visuals.active();
    snapshot.need_backtrack_visualizer = snapshot.need_backtrack_visualizer || (group.backtrack & visual_group::backtrack_enabled) != 0;
    snapshot.need_pickup_timers = snapshot.need_pickup_timers || group.pickup_timer;
    snapshot.need_head_emojis = snapshot.need_head_emojis || (group.esp.draw_mask & group_esp_settings::head_emoji) != 0;
    if (group_has_screen_overlay(group)) {
      snapshot.overlay_targets |= group.targets;
    }
    if (group.chams.active() || group.glow.active() || group.backtrack_visuals.active()) {
      snapshot.model_targets |= group.targets;
    }
  }
}

}

namespace visual_groups
{

std::uint32_t allocate_group_id()
{
  for (;;) {
    const std::uint32_t candidate = next_group_id++;
    if (candidate == 0) {
      continue;
    }

    const bool used = std::ranges::any_of(config.visual_groups.groups, [candidate](const visual_group& group) {
      return group.bind_id == candidate;
    });
    if (!used) {
      return candidate;
    }
  }
}

void ensure_defaults()
{
  if (!config.visual_groups.groups.empty()) {
    if (config.visual_groups.groups.size() > visual_group_config::max_groups) {
      config.visual_groups.groups.resize(visual_group_config::max_groups);
    }
    std::vector<std::uint32_t> used_ids{};
    used_ids.reserve(config.visual_groups.groups.size());
    for (visual_group& group : config.visual_groups.groups) {
      if (group.bind_id == 0 || std::ranges::find(used_ids, group.bind_id) != used_ids.end()) {
        group.bind_id = allocate_group_id();
      }
      used_ids.push_back(group.bind_id);
    }
    config.visual_groups.active_group_mask &= config.visual_groups.groups.size() >= visual_group_config::max_groups ? 0xFFFFFFFFu : ((1u << config.visual_groups.groups.size()) - 1u);
    return;
  }

  constexpr uint32_t enemy_conditions = visual_group::condition_enemy | visual_group::condition_blu | visual_group::condition_red;
  constexpr uint32_t esp_player = group_esp_settings::name | group_esp_settings::box | group_esp_settings::health_bar | group_esp_settings::flags;

  config.visual_groups.groups.reserve(1);
  config.visual_groups.groups.emplace_back(make_group("Enemy players", visual_group::target_players, enemy_conditions, RGBA_float{1.0f, 0.501960784f, 0.0f, 1.0f}, esp_player));
  config.visual_groups.active_group_mask = active_bit(0);
}

void begin_fake_angle_model()
{
  g_fake_angle_model = true;
}

void end_fake_angle_model()
{
  g_fake_angle_model = false;
}

void begin_viewmodel_model()
{
  g_viewmodel_model = true;
}

void end_viewmodel_model()
{
  g_viewmodel_model = false;
}

void store(Player* localplayer)
{
  ensure_defaults();
  if (nographics::is_enabled()) {
    g_group_snapshot.store(std::shared_ptr<const visual_group_snapshot>{}, std::memory_order_release);
    return;
  }

  auto next_snapshot = std::make_shared<visual_group_snapshot>();
  next_snapshot->groups = config.visual_groups.groups;
  next_snapshot->active_group_mask = config.visual_groups.active_group_mask;
  update_snapshot_capabilities(*next_snapshot);

  if (!groups_active() || localplayer == nullptr || entity_list == nullptr || engine == nullptr || !engine->is_in_game()) {
    g_group_snapshot.store(std::shared_ptr<const visual_group_snapshot>{next_snapshot}, std::memory_order_release);
    return;
  }

  if (!next_snapshot->need_screen_overlay && !next_snapshot->need_model_effects) {
    g_group_snapshot.store(std::shared_ptr<const visual_group_snapshot>{next_snapshot}, std::memory_order_release);
    return;
  }

  auto consider = [&](Entity* entity) {
    if (entity == nullptr) {
      return;
    }
    if (entity->is_dormant() && (!config.visuals.dormant_esp || !dormancy::usable(entity))) {
      return;
    }

    const std::uint32_t handle = snapshot_handle_for_entity(entity);
    if (handle == invalid_ehandle_index) {
      return;
    }

    if (next_snapshot->need_screen_overlay) {
      const std::size_t entity_group = find_group_index(entity, localplayer, false, *next_snapshot);
      if (entity_group != visual_group_not_found) {
        next_snapshot->entity_groups.emplace(handle, entity_group);
      }
    }
    if (next_snapshot->need_model_effects) {
      const std::size_t model_group = find_group_index(entity, localplayer, true, *next_snapshot);
      if (model_group != visual_group_not_found) {
        next_snapshot->model_groups.emplace(handle, model_group);
      }
    }
  };

  if (config.visuals.dormant_esp) {
    const int max_entities = std::max(entity_list->get_max_entities(), 0);
    next_snapshot->entity_groups.reserve(static_cast<std::size_t>(max_entities));
    next_snapshot->model_groups.reserve(static_cast<std::size_t>(max_entities));
    for (int index = 1; index < max_entities; ++index) {
      consider(entity_list->entity_from_index(static_cast<unsigned int>(index)));
    }
  } else {
    std::size_t live = 0;
    for (const auto& bucket : entity_cache.buckets) {
      live += bucket.size();
    }
    live += entity_cache_npcs().size();
    next_snapshot->entity_groups.reserve(live);
    next_snapshot->model_groups.reserve(live);
    for (const auto& bucket : entity_cache.buckets) {
      for (Entity* entity : bucket) {
        consider(entity);
      }
    }
    for (Entity* entity : entity_cache_npcs()) {
      consider(entity);
    }
  }

  g_group_snapshot.store(std::shared_ptr<const visual_group_snapshot>{next_snapshot}, std::memory_order_release);
}

visual_group_match group_for_entity(Entity* entity, bool models)
{
  if (entity == nullptr || nographics::is_enabled()) {
    return {};
  }

  std::shared_ptr<const visual_group_snapshot> snapshot = g_group_snapshot.load(std::memory_order_acquire);
  if (snapshot == nullptr) {
    return {};
  }

  if (models && (g_fake_angle_model || g_viewmodel_model) && entity_list != nullptr) {
    auto* localplayer = entity_list->get_localplayer();
    const std::size_t group_index = find_group_index(entity, localplayer, true, *snapshot);
    if (group_index != visual_group_not_found && group_index < snapshot->groups.size()) {
      visual_group_match match{};
      match.snapshot = std::move(snapshot);
      match.group = &match.snapshot->groups[group_index];
      return match;
    }
    return {};
  }

  const std::uint32_t handle = snapshot_handle_for_entity(entity);
  if (handle == invalid_ehandle_index) {
    return {};
  }

  const std::unordered_map<std::uint32_t, std::size_t>& groups = models ? snapshot->model_groups : snapshot->entity_groups;
  const auto found = groups.find(handle);
  if (found == groups.end() || found->second >= snapshot->groups.size()) {
    return {};
  }

  const visual_group* group = &snapshot->groups[found->second];
  visual_group_match match{};
  match.snapshot = std::move(snapshot);
  match.group = group;
  return match;
}

void visit_screen_entities(const std::function<void(Entity*, const visual_group_match&)>& fn)
{
  if (!fn) {
    return;
  }

  std::shared_ptr<const visual_group_snapshot> snapshot = g_group_snapshot.load(std::memory_order_acquire);
  if (snapshot == nullptr) {
    return;
  }

  for (const auto& [handle, group_index] : snapshot->entity_groups) {
    Entity* entity = entity_from_snapshot_handle(handle);
    if (entity == nullptr || group_index >= snapshot->groups.size()) {
      continue;
    }
    visual_group_match match{};
    match.snapshot = snapshot;
    match.group = &snapshot->groups[group_index];
    fn(entity, match);
  }
}

bool groups_active()
{
  return config.visual_groups.active_group_mask != 0 && !config.visual_groups.groups.empty();
}

template <typename Predicate>
[[nodiscard]] bool snapshot_or_config_capability(Predicate&& predicate)
{
  std::shared_ptr<const visual_group_snapshot> snapshot = g_group_snapshot.load(std::memory_order_acquire);
  if (snapshot != nullptr) {
    return predicate(*snapshot);
  }

  visual_group_snapshot fallback{};
  fallback.groups = config.visual_groups.groups;
  fallback.active_group_mask = config.visual_groups.active_group_mask;
  update_snapshot_capabilities(fallback);
  return predicate(fallback);
}

bool groups_need_screen_overlay()
{
  return snapshot_or_config_capability([](const visual_group_snapshot& snapshot) {
    return snapshot.need_screen_overlay;
  });
}

bool groups_need_model_effects()
{
  if (nographics::is_enabled()) {
    return false;
  }

  return snapshot_or_config_capability([](const visual_group_snapshot& snapshot) {
    return snapshot.need_model_effects;
  });
}

bool groups_need_backtrack_visualizer()
{
  return snapshot_or_config_capability([](const visual_group_snapshot& snapshot) {
    return snapshot.need_backtrack_visualizer;
  });
}

bool groups_need_pickup_timers()
{
  return snapshot_or_config_capability([](const visual_group_snapshot& snapshot) {
    return snapshot.need_pickup_timers;
  });
}

bool groups_need_head_emojis()
{
  return snapshot_or_config_capability([](const visual_group_snapshot& snapshot) {
    return snapshot.need_head_emojis;
  });
}

void move_group(int from, int to)
{
  ensure_defaults();
  auto& groups = config.visual_groups.groups;
  if (from < 0 || to < 0 || from >= static_cast<int>(groups.size()) || to >= static_cast<int>(groups.size()) || from == to) {
    return;
  }

  const visual_group moved_group = groups[static_cast<std::size_t>(from)];
  const bool moved_active = (config.visual_groups.active_group_mask & active_bit(static_cast<std::size_t>(from))) != 0;
  groups.erase(groups.begin() + from);
  groups.insert(groups.begin() + to, moved_group);

  uint32_t new_mask = 0;
  for (std::size_t index = 0; index < groups.size(); ++index) {
    int old_index = static_cast<int>(index);
    if (from < to) {
      if (old_index >= from && old_index < to) {
        ++old_index;
      } else if (old_index == to) {
        old_index = from;
      }
    } else {
      if (old_index > to && old_index <= from) {
        --old_index;
      } else if (old_index == to) {
        old_index = from;
      }
    }

    const bool active = static_cast<int>(index) == to ? moved_active : (config.visual_groups.active_group_mask & active_bit(static_cast<std::size_t>(old_index))) != 0;
    if (active) {
      new_mask |= active_bit(index);
    }
  }

  config.visual_groups.active_group_mask = new_mask;
}

RGBA_float color_for_entity(Entity* entity, const visual_group& group)
{
  if (!group.tags_override_color) {
    return group.color;
  }

  auto color = group.color;
  auto* owner = owner_player_for_entity(entity);
  if (owner == nullptr) {
    return color;
  }

  if (owner->is_ignored()) {
    color.r = 0.55f;
    color.g = 0.55f;
    color.b = 0.55f;
    return color;
  }

  if (is_priority_player(owner)) {
    color.r = 1.0f;
    color.g = 0.1f;
    color.b = 0.1f;
    return color;
  }

  if (is_pup_player(owner)) {
    color.r = 0.0f;
    color.g = 0.8f;
    color.b = 0.35f;
    return color;
  }

  if (owner->is_friend()) {
    color.r = 0.0f;
    color.g = 0.862745098f;
    color.b = 0.31372549f;
    return color;
  }

  return color;
}

RGBA_float resolve_color(Entity* entity, const visual_group& group, bool override_enabled, const RGBA_float& override_color)
{
  return override_enabled ? override_color : color_for_entity(entity, group);
}

float alpha_for_entity(Entity* entity, float start, float end, bool smooth_alpha)
{
  if (entity == nullptr || entity_list == nullptr) {
    return 1.0f;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    return 1.0f;
  }

  start = std::max(0.0f, start);
  end = std::max(start, end);
  const Vec3 delta = dormancy::origin(entity) - localplayer->get_collision_origin();
  const float distance = std::sqrt((delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z));
  if (distance < start) {
    return 0.0f;
  }
  if (distance > end) {
    return 0.0f;
  }
  if (!smooth_alpha) {
    return dormancy::alpha(entity);
  }

  constexpr float max_fade_distance = 256.0f;
  const float range = end - start;
  if (range <= 0.0f) {
    return 0.0f;
  }

  const float fade_distance = std::min(max_fade_distance, range * 0.5f);
  const auto smoothstep = [](float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - (2.0f * clamped));
  };

  float alpha = 1.0f;
  if (start > 0.0f) {
    alpha *= smoothstep((distance - start) / fade_distance);
  }
  alpha *= smoothstep((end - distance) / fade_distance);
  return alpha * dormancy::alpha(entity);
}

const char* label_for_entity(Entity* entity)
{
  if (entity == nullptr) {
    return "";
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    return "PLAYER";
  }
  if (entity->is_building()) {
    switch (entity->get_class_id()) {
      case class_id::SENTRY:
        return "SENTRY";
      case class_id::DISPENSER:
      case class_id::OBJECT_CART_DISPENSER:
        return "DISPENSER";
      case class_id::TELEPORTER:
        return "TELEPORTER";
      default:
        return "BUILDING";
    }
  }
  if (entity->get_pickup_type() == pickup_type::MEDKIT || entity->get_class_id() == class_id::HEALTH_PACK) {
    return "HEALTH";
  }
  if (entity->get_pickup_type() == pickup_type::AMMOPACK || entity->get_class_id() == class_id::AMMO) {
    return "AMMO";
  }
  if (entity->get_class_id() == class_id::CAPTURE_FLAG) {
    return "FLAG";
  }
  if (is_projectile(entity)) {
    return "PROJECTILE";
  }
  if (is_ragdoll(entity)) {
    return "RAGDOLL";
  }
  if (is_npc(entity)) {
    return "NPC";
  }
  if (is_money(entity)) {
    return "MONEY";
  }
  if (is_powerup(entity)) {
    return "POWERUP";
  }
  if (is_spellbook(entity)) {
    return "SPELLBOOK";
  }
  if (is_bomb(entity)) {
    return "BOMB";
  }
  if (is_gargoyle(entity)) {
    return "GARGOYLE";
  }

  const char* network_name = entity->get_network_name();
  return network_name != nullptr && network_name[0] != '\0' ? network_name : "ENTITY";
}

}
