/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_goals.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/navbot/navbot_goals.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>
#include "core/entity_cache.hpp"
#include "core/math/math.hpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/aimbot/aim_utils.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/capture_flag.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/entities/team_objective_resource.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace navbot
{

namespace
{

goal_candidate make_candidate(goal_type type, float score, const Vec3& destination, nav_area_id destination_area);
goal_candidate make_entity_candidate(goal_type type, float score, const Vec3& destination, nav_area_id destination_area, int entity_index);
bool goal_enabled(goal_type type);
void choose_best(goal_candidate& best, const goal_candidate& candidate);

const std::vector<goal_rejection>* active_rejections = nullptr;

bool same_goal_area(nav_area_id left, nav_area_id right)
{
  return left.valid() && right.valid() && left.value == right.value;
}

bool goal_rejection_matches(const goal_rejection& rejection, const goal_candidate& candidate)
{
  if (rejection.type != candidate.type || !same_goal_area(rejection.destination_area, candidate.destination_area))
  {
    return false;
  }

  if (rejection.entity_index != 0)
  {
    return candidate.entity_index == rejection.entity_index || candidate.entity_index == 0;
  }

  return candidate.entity_index == 0;
}

bool candidate_is_rejected(const goal_candidate& candidate)
{
  if (active_rejections == nullptr)
  {
    return false;
  }

  return std::any_of(active_rejections->begin(), active_rejections->end(), [&candidate](const goal_rejection& rejection)
  {
    return goal_rejection_matches(rejection, candidate);
  });
}

bool pickup_recently_taken(Entity* pickup, float current_time)
{
  if (pickup == nullptr)
  {
    return true;
  }

  auto pickup_origin = pickup->get_origin();
  for (const auto& pickup_item : pickup_item_cache)
  {
    if (pickup_item.time < current_time)
    {
      continue;
    }

    if (distance_squared_2d(pickup_origin, pickup_item.location) <= 24.0f * 24.0f)
    {
      return true;
    }
  }

  return false;
}

bool mvm_game_rules_active()
{
  if (entity_list == nullptr)
  {
    return true;
  }

  auto* proxy = entity_list->get_game_rules_proxy();
  if (proxy == nullptr)
  {
    return true;
  }

  static const int mvm_offset = tf2_netvars::find_offset(
    "DT_TFGameRulesProxy", {"m_bPlayingMannVsMachine"});
  return mvm_offset <= 0
    || *reinterpret_cast<const bool*>(reinterpret_cast<uintptr_t>(proxy) + mvm_offset);
}

bool mvm_map_active(const navbot_mesh& mesh)
{
  return mesh.map_name().starts_with("mvm_") && mvm_game_rules_active();
}

bool mvm_mode_enabled(const navbot_mesh& mesh)
{
  return config.misc.automation.navbot_behavior
    == Misc::Automation::navbot_mode::COMPLETE_MVM_SNIPER
    && mvm_map_active(mesh);
}

bool mvm_enemy_player(Player* localplayer, Player* player)
{
  return localplayer != nullptr && player != nullptr && player != localplayer
    && !player->is_dormant() && player->is_alive()
    && player->get_team() != localplayer->get_team();
}

goal_candidate choose_mvm_tank_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr)
  {
    return best;
  }

  auto local_origin = localplayer->get_origin();
  for (auto* entity : entity_cache_npcs())
  {
    if (entity == nullptr || entity->is_dormant() || !entity->is_network_class("CTFTankBoss"))
    {
      continue;
    }
    if (reinterpret_cast<Building*>(entity)->get_health() <= 0)
    {
      continue;
    }
    auto area_id = mesh.find_closest_area(entity->get_origin());
    if (!area_id.valid())
    {
      continue;
    }
    auto score = 260.0f - std::sqrt(distance_squared_2d(local_origin, entity->get_origin())) * 0.03f;
    choose_best(best, make_entity_candidate(goal_type::mvm_tank, score, entity->get_origin(), area_id, entity->get_index()));
  }
  return best;
}

goal_candidate choose_mvm_robot_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr)
  {
    return best;
  }

  auto local_origin = localplayer->get_origin();
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (!mvm_enemy_player(localplayer, player))
    {
      continue;
    }
    auto area_id = mesh.find_closest_area(player->get_origin());
    if (!area_id.valid())
    {
      continue;
    }
    auto distance = std::sqrt(distance_squared_2d(local_origin, player->get_origin()));
    auto score = 210.0f - distance * 0.035f;
    choose_best(best, make_entity_candidate(goal_type::mvm_combat, score, player->get_origin(), area_id, player->get_index()));
  }
  return best;
}

goal_candidate choose_mvm_money_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr || localplayer->get_tf_class() != tf_class::SCOUT)
  {
    return best;
  }

  auto local_origin = localplayer->get_origin();
  for (auto* entity : entity_cache[class_id::MVM_CURRENCY])
  {
    if (entity == nullptr || entity->is_dormant())
    {
      continue;
    }
    if (distance_squared_2d(local_origin, entity->get_origin()) > 1600.0f * 1600.0f)
    {
      continue;
    }
    static const int distributed_offset = tf2_netvars::find_offset("DT_CurrencyPack", {"m_bDistributed"});
    if (distributed_offset > 0
      && *reinterpret_cast<const bool*>(reinterpret_cast<uintptr_t>(entity) + distributed_offset))
    {
      continue;
    }
    auto area_id = mesh.find_closest_area(entity->get_origin());
    if (!area_id.valid())
    {
      continue;
    }
    auto distance = std::sqrt(distance_squared_2d(local_origin, entity->get_origin()));
    choose_best(best, make_entity_candidate(goal_type::mvm_money, 180.0f - distance * 0.03f,
      entity->get_origin(), area_id, entity->get_index()));
  }
  return best;
}

goal_candidate choose_mvm_upgrade_station_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr || localplayer->in_upgrade_zone()
    || !config.misc.automation.mvm_buybot)
  {
    return best;
  }

  const auto local_origin = localplayer->get_origin();
  for (auto* entity : entity_cache[class_id::MVM_UPGRADE_STATION])
  {
    if (entity == nullptr || entity->is_dormant())
    {
      continue;
    }
    const auto area_id = mesh.find_closest_area(entity->get_origin());
    if (!area_id.valid())
    {
      continue;
    }
    const auto distance = std::sqrt(distance_squared_2d(local_origin, entity->get_origin()));
    choose_best(best, make_entity_candidate(goal_type::mvm_upgrade_station,
      320.0f - distance * 0.03f, entity->get_origin(), area_id, entity->get_index()));
  }
  return best;
}

goal_candidate choose_mvm_teleporter_goal(const navbot_mesh& mesh, Player* localplayer, bool wave_started)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (!wave_started || localplayer == nullptr)
  {
    return best;
  }

  Building* entrance = nullptr;
  Building* exit = nullptr;
  for (auto* entity : entity_cache[class_id::TELEPORTER])
  {
    auto* teleporter = reinterpret_cast<Building*>(entity);
    if (teleporter == nullptr || teleporter->is_dormant()
      || teleporter->get_team() != localplayer->get_team() || teleporter->get_health() <= 0)
    {
      continue;
    }
    if (teleporter->get_object_mode() == 0)
    {
      entrance = teleporter;
    }
    else if (teleporter->get_object_mode() == 1)
    {
      exit = teleporter;
    }
  }

  if (entrance == nullptr || exit == nullptr
    || distance_squared_2d(localplayer->get_origin(), exit->get_origin()) > 150.0f * 150.0f
    || std::fabs(localplayer->get_origin().z - exit->get_origin().z) > 96.0f)
  {
    if (entrance != nullptr)
    {
      auto area_id = mesh.find_closest_area(entrance->get_origin());
      if (area_id.valid())
      {
        choose_best(best, make_entity_candidate(goal_type::mvm_teleporter, 300.0f,
          entrance->get_origin(), area_id, entrance->get_index()));
      }
    }
  }
  return best;
}

goal_candidate choose_mvm_frontline_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr)
  {
    return best;
  }

  const auto enemy_spawn_attribute = localplayer->get_team() == tf_team::RED
    ? nav_tf_spawn_room_blue
    : nav_tf_spawn_room_red;
  auto local_origin = localplayer->get_origin();
  for (const auto& area : mesh.cache().areas)
  {
    if ((area.tf_attributes & enemy_spawn_attribute) == 0
      || (area.flags & (nav_area_flag_blocked | nav_area_flag_setup_gate)) != 0)
    {
      continue;
    }

    auto distance = std::sqrt(distance_squared_2d(local_origin, area.center));
    choose_best(best, make_candidate(goal_type::mvm_frontline,
      145.0f - distance * 0.02f, area.center, area.id));
  }
  if (best.destination_area.valid())
  {
    return best;
  }

  const auto& preferred = localplayer->get_tf_class() == tf_class::SNIPER
    ? mesh.cache().sniper_spot_areas
    : mesh.cache().control_point_areas;
  for (auto area_id : preferred)
  {
    auto* area = mesh.find_area(area_id);
    if (area == nullptr || (area->flags & (nav_area_flag_blocked | nav_area_flag_setup_gate | nav_area_flag_spawn_room)) != 0)
    {
      continue;
    }
    auto distance = std::sqrt(distance_squared_2d(local_origin, area->center));
    auto score = (localplayer->get_tf_class() == tf_class::SNIPER ? 125.0f : 100.0f) - distance * 0.02f;
    if ((area->flags & nav_area_flag_sniper_spot) != 0)
    {
      score += 35.0f;
    }
    choose_best(best, make_candidate(goal_type::mvm_frontline, score, area->center, area_id));
  }
  return best;
}

goal_candidate make_candidate(goal_type type, float score, const Vec3& destination, nav_area_id destination_area)
{
  goal_candidate candidate{};
  candidate.type = type;
  candidate.score = score;
  candidate.destination = destination;
  candidate.destination_area = destination_area;
  candidate.rejected = candidate_is_rejected(candidate);
  return candidate;
}

goal_candidate make_entity_candidate(goal_type type, float score, const Vec3& destination, nav_area_id destination_area, int entity_index)
{
  goal_candidate candidate{};
  candidate.type = type;
  candidate.score = score;
  candidate.destination = destination;
  candidate.destination_area = destination_area;
  candidate.entity_index = entity_index;
  candidate.rejected = candidate_is_rejected(candidate);
  return candidate;
}

void choose_best(goal_candidate& best, const goal_candidate& candidate)
{
  if (!candidate.destination_area.valid()
    || !goal_enabled(candidate.type)
    || candidate.rejected)
  {
    return;
  }

  if (!best.destination_area.valid() || candidate.score > best.score)
  {
    best = candidate;
  }
}

float clamp01(float value)
{
  return std::clamp(value, 0.0f, 1.0f);
}

float normalize_yaw(float yaw)
{
  while (yaw > 180.0f)
  {
    yaw -= 360.0f;
  }
  while (yaw < -180.0f)
  {
    yaw += 360.0f;
  }

  return yaw;
}

bool goal_enabled(goal_type type)
{
  if (!goal_type_can_be_excluded(type))
  {
    return true;
  }

  return (config.misc.automation.navbot_excluded_jobs_mask & goal_type_bit(type)) == 0;
}

bool map_is_payload(const std::string& map_name)
{
  return map_name.starts_with("pl_") || map_name.starts_with("plr_");
}

bool area_is_roam_candidate(const navbot_mesh& mesh, nav_area_id area_id)
{
  auto area = mesh.find_area(area_id);
  if (area == nullptr)
  {
    return false;
  }

  if ((area->flags & (nav_area_flag_blocked | nav_area_flag_setup_gate)) != 0)
  {
    return false;
  }

  return true;
}

float roam_area_score(const nav_area_data& area, const Vec3& local_origin, bool prefer_spawn_exit)
{
  auto score = 0.0f;
  auto distance_sq = distance_squared_2d(local_origin, area.center);

  if (distance_sq < 250.0f * 250.0f)
  {
    score -= 2000.0f;
  }

  score += std::min(distance_sq, 4000.0f * 4000.0f) * 0.00002f;

  if ((area.flags & nav_area_flag_spawn_exit) != 0)
  {
    score += prefer_spawn_exit ? 200.0f : 20.0f;
  }
  if ((area.flags & nav_area_flag_spawn_room) != 0)
  {
    score -= 150.0f;
  }

  return score;
}

goal_candidate make_roam_candidate(const navbot_mesh& mesh, Player* localplayer, nav_area_id area_id, bool prefer_spawn_exit)
{
  auto area = mesh.find_area(area_id);
  if (area == nullptr)
  {
    return {};
  }

  return make_candidate(goal_type::roam, roam_area_score(*area, localplayer->get_origin(), prefer_spawn_exit), area->center, area_id);
}

goal_candidate choose_pickup_goal(const navbot_mesh& mesh, Player* localplayer, class_id cache_id, goal_type type, float base_score)
{
  goal_candidate best{};
  best.score = -1.0f;

  auto local_origin = localplayer->get_origin();
  auto current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  for (auto* entity : entity_cache[cache_id])
  {
    if (entity == nullptr || entity->is_dormant() || pickup_recently_taken(entity, current_time))
    {
      continue;
    }

    auto area_id = mesh.find_closest_area(entity->get_origin());
    if (!area_id.valid())
    {
      continue;
    }

    auto score = base_score - distance_squared_2d(local_origin, entity->get_origin()) * 0.00001f;
    choose_best(best, make_entity_candidate(type, score, entity->get_origin(), area_id, entity->get_index()));
  }

  return best;
}

goal_candidate choose_pickup_area_goal(const navbot_mesh& mesh, Player* localplayer, const std::vector<nav_area_id>& areas, goal_type type, float base_score)
{
  goal_candidate best{};
  best.score = -1.0f;

  auto local_origin = localplayer->get_origin();
  for (auto area_id : areas)
  {
    auto area = mesh.find_area(area_id);
    if (area == nullptr)
    {
      continue;
    }

    auto score = base_score - distance_squared_2d(local_origin, area->center) * 0.00001f;
    choose_best(best, make_candidate(type, score, area->center, area_id));
  }

  return best;
}

bool weapon_needs_reload(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto* weapon = localplayer->get_weapon();
  if (weapon == nullptr || weapon->get_clip1() != 0)
  {
    return false;
  }

  return localplayer->get_ammo_count(weapon->get_primary_ammo_type()) > 0;
}

Weapon* goal_primary_weapon(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon != nullptr && weapon->get_slot() == 0)
    {
      return weapon;
    }
  }

  return nullptr;
}

bool goal_primary_weapon_needs_ammo(Player* localplayer)
{
  auto* weapon = goal_primary_weapon(localplayer);
  if (weapon == nullptr || weapon->is_melee())
  {
    return false;
  }

  const auto clip = weapon->get_clip1();
  const auto reserve = localplayer->get_ammo_count(weapon->get_primary_ammo_type());
  return reserve <= 0 && clip <= 0;
}

goal_candidate choose_reload_weapons_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (localplayer == nullptr)
  {
    return best;
  }

  auto enemy_origins = std::vector<Vec3>{};
  enemy_origins.reserve(entity_cache[class_id::PLAYER].size());
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->is_dormant())
    {
      continue;
    }

    if (player->get_team() == localplayer->get_team() || !player->is_alive())
    {
      continue;
    }

    enemy_origins.emplace_back(player->get_origin());
  }

  auto local_origin = localplayer->get_origin();
  auto nearest_enemy_distance = [&enemy_origins](const Vec3& origin)
  {
    if (enemy_origins.empty())
    {
      return 1200.0f;
    }

    auto best_distance_sq = std::numeric_limits<float>::max();
    for (const auto& enemy_origin : enemy_origins)
    {
      best_distance_sq = std::min(best_distance_sq, distance_squared_2d(origin, enemy_origin));
    }

    return std::sqrt(best_distance_sq);
  };

  constexpr float max_reload_route_distance = 1800.0f;
  for (const auto& area : mesh.cache().areas)
  {
    if ((area.flags & (nav_area_flag_blocked | nav_area_flag_setup_gate | nav_area_flag_spawn_room)) != 0)
    {
      continue;
    }

    auto local_distance = std::sqrt(distance_squared_2d(local_origin, area.center));
    if (local_distance > max_reload_route_distance)
    {
      continue;
    }

    auto enemy_distance = nearest_enemy_distance(area.center);
    auto score = 115.0f;
    score -= local_distance * 0.030f;
    score += std::min(enemy_distance, 1400.0f) * 0.060f;

    if (enemy_distance < 350.0f)
    {
      score -= 90.0f;
    }
    if ((area.flags & nav_area_flag_health) != 0)
    {
      score += 10.0f;
    }
    if ((area.flags & nav_area_flag_ammo) != 0)
    {
      score += 8.0f;
    }
    if ((area.flags & nav_area_flag_spawn_exit) != 0)
    {
      score += 16.0f;
    }
    if ((area.flags & (nav_area_flag_control_point | nav_area_flag_sentry_spot)) != 0)
    {
      score -= 12.0f;
    }

    choose_best(best, make_candidate(goal_type::reload_weapons, score, area.center, area.id));
  }

  if (best.destination_area.valid())
  {
    return best;
  }

  auto local_area_id = mesh.find_closest_area(local_origin);
  auto local_area = mesh.find_area(local_area_id);
  if (local_area == nullptr)
  {
    return best;
  }

  return make_candidate(goal_type::reload_weapons, 1.0f, local_area->center, local_area->id);
}

goal_candidate choose_control_point_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (localplayer == nullptr)
  {
    return best;
  }

  auto local_origin = localplayer->get_origin();
  auto local_team_value = static_cast<int>(localplayer->get_team());

  std::vector<Vec3> teammate_origins;
  std::vector<Vec3> enemy_origins;
  teammate_origins.reserve(8);
  enemy_origins.reserve(8);
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->is_dormant() || !player->is_alive())
    {
      continue;
    }
    if (static_cast<int>(player->get_team()) == local_team_value)
    {
      teammate_origins.emplace_back(player->get_origin());
    }
    else
    {
      enemy_origins.emplace_back(player->get_origin());
    }
  }

  constexpr float cap_radius = 150.0f;
  constexpr float cap_radius_sq = cap_radius * cap_radius;
  constexpr float sticky_radius_sq = 220.0f * 220.0f;

  for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
  {
    auto* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
    if (objective == nullptr)
    {
      continue;
    }

    auto point_count = std::clamp(objective->get_num_control_points(), 0, MAX_CONTROL_POINTS);
    const bool playing_mini_rounds = objective->is_playing_mini_rounds();
    for (int point_index = 0; point_index < point_count; ++point_index)
    {
      if (playing_mini_rounds && !objective->is_in_mini_round(point_index))
      {
        continue;
      }

      if (objective->is_locked(point_index))
      {
        continue;
      }

      const auto owning_team = objective->get_owning_team(point_index);
      const bool can_cap = objective->can_team_capture(point_index, localplayer->get_team());
      if (owning_team == local_team_value || !can_cap)
      {
        continue;
      }

      auto origin = objective->get_origin(point_index);
      auto area_id = mesh.find_closest_area(origin);
      if (!area_id.valid())
      {
        continue;
      }

      auto distance_2d = std::sqrt(distance_squared_2d(local_origin, origin));
      auto score = 95.0f;
      score -= std::min(distance_2d, 2500.0f) * 0.015f;

      auto teammates_in_cap = 0;
      auto enemies_in_cap = 0;
      for (const auto& teammate_origin : teammate_origins)
      {
        if (distance_squared_2d(teammate_origin, origin) <= cap_radius_sq)
        {
          ++teammates_in_cap;
        }
      }
      for (const auto& enemy_origin : enemy_origins)
      {
        if (distance_squared_2d(enemy_origin, origin) <= cap_radius_sq)
        {
          ++enemies_in_cap;
        }
      }

      score += std::min(teammates_in_cap, 4) * 6.0f;

      if (enemies_in_cap == 0)
      {
        score += 12.0f;
      }
      if (enemies_in_cap > 0 && teammates_in_cap >= enemies_in_cap)
      {
        score += 10.0f;
      }

      if (distance_squared_2d(local_origin, origin) <= sticky_radius_sq)
      {
        score += 40.0f;
      }

      choose_best(best, make_candidate(goal_type::capture_objective, score, origin, area_id));
    }
  }

  if (best.score < 0.0f)
  {
    std::vector<nav_area_id> active_cp_areas;
    for (auto* entity : entity_cache[class_id::OBJECTIVE_RESOURCE])
    {
      auto* objective = reinterpret_cast<TeamObjectiveResource*>(entity);
      if (objective == nullptr)
      {
        continue;
      }

      auto point_count = std::clamp(objective->get_num_control_points(), 0, MAX_CONTROL_POINTS);
      const bool playing_mini_rounds = objective->is_playing_mini_rounds();
      for (int point_index = 0; point_index < point_count; ++point_index)
      {
        if (playing_mini_rounds && !objective->is_in_mini_round(point_index))
        {
          continue;
        }

        if (objective->is_locked(point_index))
        {
          continue;
        }

        if (objective->get_owning_team(point_index) == local_team_value ||
            !objective->can_team_capture(point_index, localplayer->get_team()))
        {
          continue;
        }

        auto origin = objective->get_origin(point_index);
        auto area_id = mesh.find_closest_area(origin);
        if (area_id.valid())
        {
          active_cp_areas.push_back(area_id);
        }
      }
    }

    if (!active_cp_areas.empty())
    {
      best = choose_pickup_area_goal(mesh, localplayer, active_cp_areas, goal_type::capture_objective, 65.0f);
    }
  }

  return best;
}

bool is_payload_model(const char* model_name)
{
  if (model_name == nullptr)
  {
    return false;
  }

  constexpr const char* known_payload_models[] = {
    "models/props_trainyard/bomb_cart.mdl",
    "models/custom/dirty_bomb_cart.mdl",
    "models/lilchewchew/lilchewchew_v3.mdl",
    "models/props_trainyard/bomb_redmond.mdl",
    "models/props_snowycoast/gasoline_bomb_cart.mdl",
    "models/props_xmas/rudy.mdl",
    "models/props_trainyard/bomb_blutarch.mdl",
    "models/props_trainyard/bomb_cart_red.mdl"
  };

  for (auto* known_model : known_payload_models)
  {
    if (std::strcmp(model_name, known_model) == 0)
    {
      return true;
    }
  }

  return false;
}

bool is_payload_cart(Entity* entity)
{
  if (entity == nullptr)
  {
    return false;
  }

  return entity->get_class_id() == class_id::OBJECT_CART_DISPENSER || is_payload_model(entity->get_model_name());
}

Vec3 payload_origin(Entity* entity)
{
  if (entity == nullptr)
  {
    return {};
  }

  if (entity->get_class_id() == class_id::OBJECT_CART_DISPENSER)
  {
    return entity->get_abs_origin();
  }

  return entity->get_origin();
}

float payload_distance_score(float base_score, const Vec3& local_origin, const Vec3& cart_origin)
{
  return base_score - std::sqrt(distance_squared_2d(local_origin, cart_origin)) * 0.006f;
}

Vec3 choose_payload_push_destination(const navbot_mesh& mesh, Player* localplayer, const Vec3& cart_origin, nav_area_id cart_area_id)
{
  auto destination = mesh.get_nearest_point(cart_area_id, cart_origin);
  if (localplayer == nullptr)
  {
    return destination;
  }

  auto local_origin = localplayer->get_origin();
  if (std::sqrt(distance_squared_2d(local_origin, cart_origin)) <= 150.0f)
  {
    destination.z = local_origin.z;
  }

  return destination;
}

bool payload_push_destination_in_range(const Vec3& destination, const Vec3& cart_origin)
{
  constexpr float max_push_destination_distance = 110.0f;
  return distance_squared_2d(destination, cart_origin) <= max_push_destination_distance * max_push_destination_distance;
}

goal_candidate choose_payload_defend_destination(const navbot_mesh& mesh, Player* localplayer, const Vec3& cart_origin, nav_area_id cart_area_id, float base_score)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (localplayer == nullptr)
  {
    return best;
  }

  constexpr float preferred_defend_distance = 430.0f;
  constexpr float min_defend_distance = 225.0f;
  constexpr float max_defend_distance = 725.0f;

  auto local_origin = localplayer->get_origin();
  for (const auto& area : mesh.cache().areas)
  {
    if ((area.flags & (nav_area_flag_blocked | nav_area_flag_setup_gate | nav_area_flag_spawn_room)) != 0)
    {
      continue;
    }

    auto cart_distance = std::sqrt(distance_squared_2d(area.center, cart_origin));
    if (cart_distance < min_defend_distance || cart_distance > max_defend_distance)
    {
      continue;
    }

    auto distance_error = std::fabs(cart_distance - preferred_defend_distance);
    auto score = base_score;
    score -= std::sqrt(distance_squared_2d(local_origin, area.center)) * 0.004f;
    score -= distance_error * 0.035f;

    if ((area.flags & nav_area_flag_control_point) != 0)
    {
      score += 5.0f;
    }
    if ((area.flags & nav_area_flag_sentry_spot) != 0)
    {
      score += 4.0f;
    }
    if ((area.flags & nav_area_flag_sniper_spot) != 0)
    {
      score += 3.0f;
    }

    choose_best(best, make_candidate(goal_type::defend_payload, score, area.center, area.id));
  }

  if (best.destination_area.valid())
  {
    return best;
  }

  auto fallback_area = mesh.find_area(cart_area_id);
  if (fallback_area == nullptr)
  {
    return best;
  }

  return make_candidate(goal_type::defend_payload, base_score - 20.0f, fallback_area->center, cart_area_id);
}

bool goal_is_objective(goal_type type)
{
  return type == goal_type::capture_objective
    || type == goal_type::push_payload
    || type == goal_type::defend_payload;
}

bool enemy_close_to_payload_cart(Player* localplayer)
{
  if (localplayer == nullptr || entity_list == nullptr)
  {
    return false;
  }

  constexpr float payload_threat_distance = 325.0f;
  auto payload_threat_distance_sq = payload_threat_distance * payload_threat_distance;
  auto max_entities = entity_list->get_max_entities();

  for (int entity_index = 1; entity_index < max_entities; ++entity_index)
  {
    auto* entity = entity_list->entity_from_index(entity_index);
    if (entity == nullptr || entity->is_dormant() || !is_payload_cart(entity))
    {
      continue;
    }

    auto cart_origin = payload_origin(entity);
    for (auto* player_entity : entity_cache[class_id::PLAYER])
    {
      auto* player = reinterpret_cast<Player*>(player_entity);
      if (player == nullptr || player == localplayer || player->is_dormant())
      {
        continue;
      }
      if (player->get_team() == localplayer->get_team() || player->get_health() <= 0)
      {
        continue;
      }

      if (distance_squared_2d(player->get_origin(), cart_origin) <= payload_threat_distance_sq)
      {
        return true;
      }
    }
  }

  return false;
}

goal_candidate choose_payload_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;

  if (entity_list == nullptr)
  {
    return best;
  }

  auto local_origin = localplayer->get_origin();
  auto max_entities = entity_list->get_max_entities();
  for (int entity_index = 1; entity_index < max_entities; ++entity_index)
  {
    auto* entity = entity_list->entity_from_index(entity_index);
    if (entity == nullptr || entity->is_dormant())
    {
      continue;
    }

    if (!is_payload_cart(entity))
    {
      continue;
    }

    auto origin = payload_origin(entity);
    auto area_id = mesh.find_closest_area(origin);
    if (!area_id.valid())
    {
      continue;
    }

    auto goal = entity->get_team() == localplayer->get_team()
      ? goal_type::push_payload
      : goal_type::defend_payload;
    if (!goal_enabled(goal))
    {
      continue;
    }
    if (goal == goal_type::push_payload)
    {
      auto destination = choose_payload_push_destination(mesh, localplayer, origin, area_id);
      if (!payload_push_destination_in_range(destination, origin))
      {
        continue;
      }

      choose_best(best, make_candidate(goal, payload_distance_score(65.0f, local_origin, origin), destination, area_id));
      continue;
    }

    auto defend_goal = choose_payload_defend_destination(mesh, localplayer, origin, area_id, payload_distance_score(48.0f, local_origin, origin));
    choose_best(best, defend_goal);
  }

  return best;
}

struct enemy_range_profile
{
  float preferred_distance = 325.0f;
  float low_health_distance = 225.0f;
  float min_distance = 125.0f;
  float max_distance = 525.0f;
  float side_bias = 1.0f;
};

enemy_range_profile build_enemy_range_profile(tf_class class_type)
{
  switch (class_type)
  {
    case tf_class::SCOUT:
      return enemy_range_profile{185.0f, 115.0f, 80.0f, 280.0f, 0.55f};
    case tf_class::SNIPER:
      return enemy_range_profile{950.0f, 700.0f, 450.0f, 1500.0f, 1.25f};
    case tf_class::SOLDIER:
      return enemy_range_profile{430.0f, 260.0f, 140.0f, 650.0f, 1.05f};
    case tf_class::DEMOMAN:
      return enemy_range_profile{500.0f, 300.0f, 160.0f, 725.0f, 1.10f};
    case tf_class::MEDIC:
      return enemy_range_profile{360.0f, 230.0f, 140.0f, 525.0f, 1.00f};
    case tf_class::HEAVYWEAPONS:
      return enemy_range_profile{240.0f, 165.0f, 100.0f, 360.0f, 0.70f};
    case tf_class::PYRO:
      return enemy_range_profile{170.0f, 100.0f, 75.0f, 260.0f, 0.45f};
    case tf_class::SPY:
      return enemy_range_profile{150.0f, 95.0f, 60.0f, 220.0f, 0.35f};
    case tf_class::ENGINEER:
      return enemy_range_profile{315.0f, 210.0f, 115.0f, 440.0f, 0.85f};
    case tf_class::UNDEFINED:
    default:
      return enemy_range_profile{};
  }
}

float choose_enemy_distance(const enemy_range_profile& profile, Player* player)
{
  if (player == nullptr)
  {
    return profile.preferred_distance;
  }

  auto max_health = player->get_max_health();
  if (max_health <= 0)
  {
    return profile.preferred_distance;
  }

  auto health_ratio = clamp01(static_cast<float>(player->get_health()) / static_cast<float>(max_health));
  auto aggression = clamp01((0.55f - health_ratio) / 0.55f);
  return profile.preferred_distance + (profile.low_health_distance - profile.preferred_distance) * aggression;
}

float choose_enemy_orbit_phase(float current_time)
{
  auto phase = std::fmod(current_time * 110.0f, 360.0f);
  if (phase < 0.0f)
  {
    phase += 360.0f;
  }

  return phase;
}

bool vec3_is_finite(const Vec3& value)
{
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

unsigned int enemy_line_of_fire_trace_mask()
{
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass)
  {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

bool enemy_line_of_fire_clear(Player* localplayer, Player* enemy, const Vec3& shoot_pos, const Vec3& target_pos)
{
  if (localplayer == nullptr || enemy == nullptr || engine_trace == nullptr)
  {
    return false;
  }
  if (!vec3_is_finite(shoot_pos) || !vec3_is_finite(target_pos))
  {
    return false;
  }

  Vec3 start = shoot_pos;
  Vec3 end = target_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_hitscan_trace_filter(&filter, localplayer, enemy);
  trace_t trace{};
  engine_trace->trace_ray(&ray, enemy_line_of_fire_trace_mask(), &filter, &trace);

  Entity* traced_entity = static_cast<Entity*>(trace.entity);
  if (traced_entity != nullptr && traced_entity->get_index() == enemy->get_index())
  {
    return true;
  }

  return !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
}

bool enemy_goal_has_line_of_fire(Player* localplayer, Player* enemy, const Vec3& destination)
{
  if (localplayer == nullptr || enemy == nullptr)
  {
    return false;
  }

  const Vec3 shoot_pos = destination + localplayer->get_view_offset();
  const Vec3 enemy_origin = enemy->get_origin();
  const Vec3 enemy_view_offset = enemy->get_view_offset();
  const Vec3 enemy_eye = enemy_origin + enemy_view_offset;
  const Vec3 enemy_chest = enemy_origin + Vec3{0.0f, 0.0f, std::max(enemy_view_offset.z * 0.62f, 36.0f)};
  const Vec3 enemy_low = enemy_origin + Vec3{0.0f, 0.0f, 28.0f};
  const Vec3 target_points[] = {enemy_eye, enemy_chest, enemy_low};

  for (const Vec3& target_point : target_points)
  {
    if (enemy_line_of_fire_clear(localplayer, enemy, shoot_pos, target_point))
    {
      return true;
    }
  }

  return false;
}

bool navbot_melee_chase_allowed(Player* localplayer, Player* enemy, float distance)
{
  if (localplayer == nullptr || enemy == nullptr ||
      distance > std::clamp(config.misc.automation.navbot_melee_target_range, 150.0f, 4000.0f) ||
      std::fabs(enemy->get_origin().z - localplayer->get_origin().z) > melee_chase_vertical_limit)
  {
    return false;
  }

  if (localplayer->get_tf_class() == tf_class::SPY)
  {
    return localplayer->in_cond(TF_COND_STEALTHED) || distance <= melee_chase_spy_approach_distance;
  }

  return distance <= melee_chase_switch_distance;
}

Vec3 navbot_melee_destination(Player* localplayer, Player* enemy)
{
  if (localplayer == nullptr || enemy == nullptr || localplayer->get_tf_class() != tf_class::SPY)
  {
    return enemy != nullptr ? enemy->get_origin() : Vec3{};
  }

  if (distance_3d(localplayer->get_origin(), enemy->get_origin()) <= 96.0f)
  {
    return enemy->get_origin();
  }

  Vec3 forward{};
  angle_vectors(enemy->get_eye_angles(), &forward, nullptr, nullptr);
  forward.z = 0.0f;
  const float length = std::sqrt((forward.x * forward.x) + (forward.y * forward.y));
  if (length <= 0.0001f)
  {
    return enemy->get_origin();
  }

  forward = forward * (1.0f / length);
  const Vec3 side{-forward.y, forward.x, 0.0f};
  const float side_sign = ((localplayer->get_index() + enemy->get_index()) & 1) != 0 ? 1.0f : -1.0f;
  return enemy->get_origin() - forward * 68.0f + side * (side_sign * 28.0f);
}

goal_candidate choose_melee_chase_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -std::numeric_limits<float>::max();
  if (localplayer == nullptr)
  {
    return best;
  }

  const Vec3 local_origin = localplayer->get_origin();
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* enemy = reinterpret_cast<Player*>(entity);
    if (enemy == nullptr || aimbot_should_skip_player(localplayer, enemy))
    {
      continue;
    }

    const float distance = distance_3d(local_origin, enemy->get_origin());
    if (!navbot_melee_chase_allowed(localplayer, enemy, distance))
    {
      continue;
    }

    const Vec3 destination = navbot_melee_destination(localplayer, enemy);
    const auto area_id = mesh.find_closest_area(destination);
    if (!area_id.valid())
    {
      continue;
    }

    auto score = 140.0f - distance * 0.12f;
    if (localplayer->get_weapon() != nullptr && localplayer->get_weapon()->is_melee())
    {
      score += 25.0f;
    }
    choose_best(best, make_entity_candidate(goal_type::melee_chase, score, destination, area_id, enemy->get_index()));
  }

  return best;
}

goal_candidate choose_yolo_enemy_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -std::numeric_limits<float>::max();
  if (localplayer == nullptr)
  {
    return best;
  }

  const Vec3 local_origin = localplayer->get_origin();
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* enemy = reinterpret_cast<Player*>(entity);
    if (enemy == nullptr || enemy == localplayer || enemy->is_dormant()
      || enemy->get_team() == localplayer->get_team() || enemy->get_health() <= 0)
    {
      continue;
    }

    const Vec3 destination = enemy->get_origin();
    const auto area_id = mesh.find_closest_area(destination);
    if (!area_id.valid())
    {
      continue;
    }

    const auto score = 55.0f - distance_squared_2d(local_origin, destination) * 0.00001f;
    choose_best(best, make_entity_candidate(goal_type::hold_range_on_enemy, score, destination, area_id, enemy->get_index()));
  }

  return best;
}

void choose_enemy_range_candidate(goal_candidate& best_visible, goal_candidate& best_blocked, const goal_candidate& candidate, bool has_line_of_fire)
{
  if (has_line_of_fire)
  {
    choose_best(best_visible, candidate);
    return;
  }

  goal_candidate blocked_candidate = candidate;
  blocked_candidate.score -= 85.0f;
  choose_best(best_blocked, blocked_candidate);
}

float enemy_goal_score(const nav_area_data& area, const Vec3& local_origin, const Vec3& enemy_origin, float desired_distance, const enemy_range_profile& profile)
{
  auto local_distance = std::sqrt(distance_squared_2d(local_origin, area.center));
  auto enemy_distance = std::sqrt(distance_squared_2d(enemy_origin, area.center));
  auto distance_error = std::fabs(enemy_distance - desired_distance);
  auto score = 55.0f;

  score -= distance_squared_2d(local_origin, area.center) * 0.00001f;
  score -= distance_error * 0.075f;

  if (enemy_distance < profile.min_distance)
  {
    score -= (profile.min_distance - enemy_distance) * 0.30f;
  }
  if (enemy_distance > profile.max_distance)
  {
    score -= (enemy_distance - profile.max_distance) * 0.12f;
  }

  auto enemy_to_area_yaw = std::atan2(area.center.y - enemy_origin.y, area.center.x - enemy_origin.x) * radpi;
  auto enemy_to_local_yaw = std::atan2(local_origin.y - enemy_origin.y, local_origin.x - enemy_origin.x) * radpi;
  auto yaw_delta = std::fabs(normalize_yaw(enemy_to_area_yaw - enemy_to_local_yaw));
  auto side_amount = clamp01(1.0f - std::fabs(yaw_delta - 90.0f) / 90.0f);
  score += side_amount * 12.0f * profile.side_bias;

  if ((area.flags & nav_area_flag_sniper_spot) != 0)
  {
    score += profile.preferred_distance >= 700.0f ? 18.0f : 3.0f;
  }
  if ((area.flags & nav_area_flag_spawn_room) != 0)
  {
    score -= 50.0f;
  }
  if ((area.flags & nav_area_flag_blocked) != 0)
  {
    score -= 250.0f;
  }

  if (local_distance < 90.0f)
  {
    score -= 8.0f;
  }

  return score;
}

goal_candidate choose_enemy_goal(const navbot_mesh& mesh, Player* localplayer, float current_time)
{
  if (config.misc.automation.enemy_stalk_mode == Misc::Automation::navbot_enemy_stalk_mode::YOLO)
  {
    return choose_yolo_enemy_goal(mesh, localplayer);
  }

  goal_candidate best_melee = choose_melee_chase_goal(mesh, localplayer);
  goal_candidate best_visible{};
  best_visible.score = -std::numeric_limits<float>::max();
  goal_candidate best_blocked{};
  best_blocked.score = -std::numeric_limits<float>::max();
  auto local_origin = localplayer->get_origin();
  auto profile = build_enemy_range_profile(localplayer->get_tf_class());
  auto orbit_phase = choose_enemy_orbit_phase(current_time);
  constexpr float orbit_offsets[] = {0.0f, 60.0f, -60.0f, 120.0f, -120.0f, 180.0f};

  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player == localplayer || player->is_dormant())
    {
      continue;
    }
    if (player->get_team() == localplayer->get_team() || player->get_health() <= 0)
    {
      continue;
    }

    auto enemy_origin = player->get_origin();
    auto desired_distance = choose_enemy_distance(profile, player);
    auto fallback_area_id = mesh.find_closest_area(enemy_origin);
    auto fallback_area = mesh.find_area(fallback_area_id);
    if (fallback_area != nullptr)
    {
      auto fallback_score = enemy_goal_score(*fallback_area, local_origin, enemy_origin, desired_distance, profile) - 6.0f;
      const Vec3 destination = fallback_area->center;
      const goal_candidate candidate = make_candidate(goal_type::hold_range_on_enemy, fallback_score, destination, fallback_area_id);
      choose_enemy_range_candidate(best_visible, best_blocked, candidate, enemy_goal_has_line_of_fire(localplayer, player, destination));
    }

    for (auto orbit_offset : orbit_offsets)
    {
      auto angle = (orbit_phase + orbit_offset) * pideg;
      auto orbit_point = Vec3{
        enemy_origin.x + std::cos(angle) * desired_distance,
        enemy_origin.y + std::sin(angle) * desired_distance,
        enemy_origin.z
      };

      auto area_id = mesh.find_closest_area(orbit_point);
      if (!area_id.valid())
      {
        continue;
      }

      auto area = mesh.find_area(area_id);
      if (area == nullptr)
      {
        continue;
      }

      auto destination = mesh.get_nearest_point(area_id, orbit_point);
      auto score = enemy_goal_score(*area, local_origin, enemy_origin, desired_distance, profile);
      score -= distance_squared_2d(destination, orbit_point) * 0.00002f;
      const goal_candidate candidate = make_candidate(goal_type::hold_range_on_enemy, score, destination, area_id);
      choose_enemy_range_candidate(best_visible, best_blocked, candidate, enemy_goal_has_line_of_fire(localplayer, player, destination));
    }
  }

  goal_candidate best = best_melee;
  choose_best(best, best_visible);
  choose_best(best, best_blocked);
  return best;
}

goal_candidate choose_heal_follow_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (localplayer == nullptr || localplayer->get_tf_class() != tf_class::MEDIC)
  {
    return best;
  }

  auto* target = medic_automation::controller().heal_target();
  if (target == nullptr || target->is_dormant() || !target->is_alive() || target->get_team() != localplayer->get_team())
  {
    return best;
  }

  auto destination = target->get_origin();
  auto area_id = mesh.find_closest_area(destination);
  if (!area_id.valid())
  {
    return best;
  }

  auto score = 88.0f;
  if (target->get_max_health() > 0 && target->get_health() < target->get_max_health())
  {
    score += (1.0f - std::clamp(static_cast<float>(target->get_health()) / static_cast<float>(target->get_max_health()), 0.0f, 1.0f)) * 18.0f;
  }
  return make_entity_candidate(goal_type::heal_follow, score, destination, area_id, target->get_index());
}

}

void navbot_goals::reset_flag_home_cache()
{
  red_flag_home_ = {};
  blu_flag_home_ = {};
}

void navbot_goals::update_flag_home_cache(tf_team team, const Vec3& origin)
{
  switch (team)
  {
    case tf_team::RED:
      red_flag_home_.valid = true;
      red_flag_home_.origin = origin;
      break;
    case tf_team::BLU:
      blu_flag_home_.valid = true;
      blu_flag_home_.origin = origin;
      break;
    default:
      break;
  }
}

navbot_goals::cached_flag_home navbot_goals::flag_home_for_team(tf_team team) const
{
  switch (team)
  {
    case tf_team::RED:
      return red_flag_home_;
    case tf_team::BLU:
      return blu_flag_home_;
    default:
      return {};
  }
}

goal_candidate navbot_goals::choose_flag_goal(const navbot_mesh& mesh, Player* localplayer)
{
  goal_candidate best{};
  best.score = -1.0f;
  auto* localplayer_entity = localplayer->to_entity();
  CaptureFlag* own_flag = nullptr;
  CaptureFlag* enemy_flag = nullptr;

  for (auto* entity : entity_cache[class_id::CAPTURE_FLAG])
  {
    auto* flag = reinterpret_cast<CaptureFlag*>(entity);
    if (flag == nullptr)
    {
      continue;
    }

    if (flag->get_status() == flag_status::HOME)
    {
      update_flag_home_cache(flag->get_team(), flag->get_origin());
    }

    if (flag->get_team() == localplayer->get_team())
    {
      own_flag = flag;
    }
    else
    {
      enemy_flag = flag;
    }
  }

  const bool carrying_enemy_flag = enemy_flag != nullptr
    && enemy_flag->get_status() == flag_status::STOLEN
    && enemy_flag->get_owner_entity() == localplayer_entity;

  if (carrying_enemy_flag && goal_enabled(goal_type::return_flag))
  {
    auto own_home = flag_home_for_team(localplayer->get_team());
    auto own_base_origin = own_home.valid
      ? own_home.origin
      : (own_flag != nullptr ? own_flag->get_origin() : localplayer->get_origin());
    auto own_base_area = mesh.find_closest_area(own_base_origin);
    if (own_base_area.valid())
    {
      auto score = 95.0f - distance_squared_2d(localplayer->get_origin(), own_base_origin) * 0.00001f;
      choose_best(best, make_candidate(goal_type::return_flag, score, own_base_origin, own_base_area));
    }
  }

  if (enemy_flag != nullptr && !carrying_enemy_flag && goal_enabled(goal_type::get_flag))
  {
    auto origin = enemy_flag->get_origin();
    auto area_id = mesh.find_closest_area(origin);
    if (area_id.valid())
    {
      auto score = 60.0f - distance_squared_2d(localplayer->get_origin(), origin) * 0.00001f;
      if (enemy_flag->get_status() == flag_status::DROPPED)
      {
        score += 10.0f;
      }
      choose_best(best, make_candidate(goal_type::get_flag, score, origin, area_id));
    }
  }

  return best;
}

goal_candidate navbot_goals::choose_roam_goal(const navbot_mesh& mesh, Player* localplayer, float current_time)
{
  goal_candidate best{};
  best.score = -1.0f;

  auto local_area_id = mesh.find_closest_area(localplayer->get_origin());
  auto local_area = mesh.find_area(local_area_id);
  if (local_area == nullptr)
  {
    return best;
  }

  auto prefer_spawn_exit = (local_area->flags & nav_area_flag_spawn_room) != 0;

  auto make_local_fallback = [&]()
  {
    rejected_goals_.erase(std::remove_if(rejected_goals_.begin(), rejected_goals_.end(), [local_area_id](const goal_rejection& rejection)
    {
      return rejection.type == goal_type::roam
        && rejection.destination_area.value == local_area_id.value;
    }), rejected_goals_.end());

    auto fallback = make_candidate(goal_type::roam, 1.0f, local_area->center, local_area->id);
    last_roam_area_ = fallback.destination_area;
    next_roam_refresh_time_ = current_time + 2.0f;
    return fallback;
  };

  if (last_roam_area_.valid() && current_time < next_roam_refresh_time_)
  {
    auto persisted_area = mesh.find_area(last_roam_area_);
    if (persisted_area != nullptr
      && area_is_roam_candidate(mesh, last_roam_area_)
      && distance_squared_2d(localplayer->get_origin(), persisted_area->center) > 250.0f * 250.0f)
    {
      auto persisted_candidate = make_roam_candidate(mesh, localplayer, last_roam_area_, prefer_spawn_exit);
      if (!persisted_candidate.rejected)
      {
        return persisted_candidate;
      }
    }
  }

  struct scored_roam_candidate
  {
    nav_area_id id{};
    float score = 0.0f;
  };

  const auto local_origin = localplayer->get_origin();
  auto candidates = std::vector<scored_roam_candidate>{};
  candidates.reserve(mesh.cache().areas.size());

  for (const auto& area : mesh.cache().areas)
  {
    if (area.id.value == local_area_id.value
      || !area_is_roam_candidate(mesh, area.id)
      || (area.flags & nav_area_flag_spawn_room) != 0)
    {
      continue;
    }

    candidates.emplace_back(scored_roam_candidate{
      area.id,
      roam_area_score(area, local_origin, prefer_spawn_exit)
    });
  }

  if (candidates.empty())
  {
    return make_local_fallback();
  }

  std::sort(candidates.begin(), candidates.end(), [](const scored_roam_candidate& left, const scored_roam_candidate& right)
  {
    return left.score > right.score;
  });

  auto available_candidate_ids = std::vector<nav_area_id>{};
  constexpr size_t max_roam_candidates = 10;
  available_candidate_ids.reserve(std::min(candidates.size(), max_roam_candidates));
  for (const auto& candidate : candidates)
  {
    if (!make_roam_candidate(mesh, localplayer, candidate.id, prefer_spawn_exit).rejected)
    {
      available_candidate_ids.emplace_back(candidate.id);
      if (available_candidate_ids.size() == max_roam_candidates)
      {
        break;
      }
    }
  }

  if (available_candidate_ids.empty())
  {
    return make_local_fallback();
  }

  static std::mt19937 random_engine{std::random_device{}()};
  auto candidate_limit = available_candidate_ids.size();
  std::uniform_int_distribution<size_t> random_candidate(0, candidate_limit - 1);
  auto selected_index = random_candidate(random_engine);

  best = make_roam_candidate(mesh, localplayer, available_candidate_ids[selected_index], prefer_spawn_exit);
  last_roam_area_ = best.destination_area;
  next_roam_refresh_time_ = current_time + 2.0f;
  return best;
}

goal_candidate navbot_goals::choose_mvm_goal(const navbot_mesh& mesh, Player* localplayer, bool wave_started)
{
  goal_candidate best{};
  best.score = -1.0f;
  if (!mvm_mode_enabled(mesh) || localplayer == nullptr)
  {
    return best;
  }

  auto take_if_valid = [&best](goal_candidate candidate) {
    if (!candidate.destination_area.valid()
      || !goal_enabled(candidate.type)
      || candidate.rejected)
    {
      return false;
    }
    best = std::move(candidate);
    return true;
  };
  if (!wave_started && take_if_valid(choose_mvm_upgrade_station_goal(mesh, localplayer)))
  {
    return best;
  }
  if (take_if_valid(choose_mvm_teleporter_goal(mesh, localplayer, wave_started)))
  {
    return best;
  }
  if (take_if_valid(choose_mvm_tank_goal(mesh, localplayer)))
  {
    return best;
  }
  if (take_if_valid(choose_mvm_robot_goal(mesh, localplayer)))
  {
    return best;
  }
  if (take_if_valid(choose_mvm_money_goal(mesh, localplayer)))
  {
    return best;
  }

  auto max_health = localplayer->get_max_health();
  auto health_ratio = max_health > 0
    ? static_cast<float>(localplayer->get_health()) / static_cast<float>(max_health)
    : 1.0f;
  if (health_ratio < 0.35f)
  {
    choose_best(best, choose_pickup_goal(mesh, localplayer, class_id::HEALTH_PACK, goal_type::get_health, 190.0f));
    choose_best(best, choose_pickup_area_goal(mesh, localplayer, mesh.cache().health_areas, goal_type::get_health, 165.0f));
    if (best.destination_area.valid())
    {
      return best;
    }
  }
  if (goal_primary_weapon_needs_ammo(localplayer))
  {
    choose_best(best, choose_pickup_goal(mesh, localplayer, class_id::AMMO, goal_type::get_ammo, 175.0f));
    choose_best(best, choose_pickup_area_goal(mesh, localplayer, mesh.cache().ammo_areas, goal_type::get_ammo, 155.0f));
    if (best.destination_area.valid())
    {
      return best;
    }
  }

  return choose_mvm_frontline_goal(mesh, localplayer);
}

void navbot_goals::reset_job_availability()
{
  for (size_t index = 0; index < goal_type_count; ++index)
  {
    const auto type = static_cast<goal_type>(index);
    job_availability_[index] = navbot_job_availability{
      goal_enabled(type),
      false
    };
  }
}

bool navbot_goals::note_job_candidate(const goal_candidate& candidate)
{
  if (!candidate.destination_area.valid()
    || !goal_enabled(candidate.type)
    || candidate.rejected)
  {
    return false;
  }

  job_availability_[goal_type_index(candidate.type)].candidate_available = true;
  return true;
}

navbot_goal_state navbot_goals::select_goal(const navbot_mesh& mesh, Player* localplayer, float current_time, bool mvm_wave_started)
{
  navbot_goal_state state{};
  reset_job_availability();
  if (!mesh.is_ready() || localplayer == nullptr)
  {
    return state;
  }

  struct rejection_scope
  {
    const std::vector<goal_rejection>* previous = active_rejections;
    explicit rejection_scope(const std::vector<goal_rejection>& rejections)
    {
      active_rejections = &rejections;
    }
    ~rejection_scope()
    {
      active_rejections = previous;
    }
  } scope(rejected_goals_);

  goal_candidate best{};
  best.score = -1.0f;
  auto consider = [this, &best](goal_candidate candidate)
  {
    note_job_candidate(candidate);
    choose_best(best, candidate);
  };

  rejected_goals_.erase(std::remove_if(rejected_goals_.begin(), rejected_goals_.end(), [current_time](const goal_rejection& rejection)
  {
    return rejection.expire_time > 0.0f && rejection.expire_time <= current_time;
  }), rejected_goals_.end());

  if (cached_map_name_ != mesh.map_name())
  {
    cached_map_name_ = mesh.map_name();
    reset_flag_home_cache();
    rejected_goals_.clear();
  }

  if (mvm_mode_enabled(mesh))
  {
    auto mvm_goal = choose_mvm_goal(mesh, localplayer, mvm_wave_started);
    if (note_job_candidate(mvm_goal))
    {
      state.valid = true;
      state.score = mvm_goal.score;
      state.goal = mvm_goal;
      return state;
    }
  }

  if (mvm_map_active(mesh) && config.misc.automation.mvm_buybot)
  {
    const auto station_goal = choose_mvm_upgrade_station_goal(mesh, localplayer);
    if (note_job_candidate(station_goal))
    {
      state.valid = true;
      state.score = station_goal.score;
      state.goal = station_goal;
      return state;
    }
  }

  auto max_health = localplayer->get_max_health();
  auto health_ratio = max_health > 0 ? static_cast<float>(localplayer->get_health()) / static_cast<float>(max_health) : 1.0f;

  if (health_ratio < 0.70f)
  {
    if (goal_enabled(goal_type::get_health))
    {
      consider(choose_pickup_goal(mesh, localplayer, class_id::HEALTH_PACK, goal_type::get_health, 100.0f));
      consider(choose_pickup_area_goal(mesh, localplayer, mesh.cache().health_areas, goal_type::get_health, 60.0f));
    }
  }

  if (weapon_needs_reload(localplayer) && goal_enabled(goal_type::reload_weapons))
  {
    consider(choose_reload_weapons_goal(mesh, localplayer));
  }

  if (goal_primary_weapon_needs_ammo(localplayer) && goal_enabled(goal_type::get_ammo))
  {
    consider(choose_pickup_goal(mesh, localplayer, class_id::AMMO, goal_type::get_ammo, 80.0f));
    consider(choose_pickup_area_goal(mesh, localplayer, mesh.cache().ammo_areas, goal_type::get_ammo, 50.0f));
  }

  if (goal_enabled(goal_type::get_flag) || goal_enabled(goal_type::return_flag))
  {
    const auto flag_goal = choose_flag_goal(mesh, localplayer);
    if (flag_goal.type == goal_type::return_flag && note_job_candidate(flag_goal))
    {
      state.valid = true;
      state.score = flag_goal.score;
      state.goal = flag_goal;
      return state;
    }
    consider(flag_goal);
  }

  auto best_before_objectives = best;
  if (goal_enabled(goal_type::push_payload) || goal_enabled(goal_type::defend_payload))
  {
    consider(choose_payload_goal(mesh, localplayer));
  }
  if (goal_enabled(goal_type::capture_objective) && !map_is_payload(mesh.map_name()))
  {
    consider(choose_control_point_goal(mesh, localplayer));
  }

  if (goal_enabled(goal_type::heal_follow))
  {
    consider(choose_heal_follow_goal(mesh, localplayer));
  }

  auto have_priority_objective = goal_is_objective(best.type) && best.score > best_before_objectives.score;
  auto allow_enemy_override = !have_priority_objective || enemy_close_to_payload_cart(localplayer);

  const bool seeking_ammo = best.destination_area.valid() && best.type == goal_type::get_ammo;
  if (goal_enabled(goal_type::hold_range_on_enemy) && allow_enemy_override && !seeking_ammo)
  {
    consider(choose_enemy_goal(mesh, localplayer, current_time));
  }

  if (!best.destination_area.valid())
  {
    consider(choose_roam_goal(mesh, localplayer, current_time));
  }

  if (best.destination_area.valid())
  {
    state.valid = true;
    state.score = best.score;
    state.goal = best;
  }

  return state;
}

navbot_goal_state navbot_goals::select_roam_goal(const navbot_mesh& mesh, Player* localplayer, float current_time)
{
  navbot_goal_state state{};
  reset_job_availability();
  if (!mesh.is_ready() || localplayer == nullptr)
  {
    return state;
  }

  struct rejection_scope
  {
    const std::vector<goal_rejection>* previous = active_rejections;
    explicit rejection_scope(const std::vector<goal_rejection>& rejections)
    {
      active_rejections = &rejections;
    }
    ~rejection_scope()
    {
      active_rejections = previous;
    }
  } scope(rejected_goals_);

  rejected_goals_.erase(std::remove_if(rejected_goals_.begin(), rejected_goals_.end(), [current_time](const goal_rejection& rejection)
  {
    return rejection.expire_time > 0.0f && rejection.expire_time <= current_time;
  }), rejected_goals_.end());

  auto candidate = choose_roam_goal(mesh, localplayer, current_time);
  if (note_job_candidate(candidate))
  {
    state.valid = true;
    state.score = candidate.score;
    state.goal = candidate;
  }

  return state;
}

const std::array<navbot_job_availability, goal_type_count>& navbot_goals::job_availability() const
{
  return job_availability_;
}

void navbot_goals::reject_goal(const goal_candidate& goal, float current_time)
{
  if (!goal.destination_area.valid())
  {
    return;
  }

  constexpr float failed_goal_blacklist_seconds = 30.0f;
  rejected_goals_.erase(std::remove_if(rejected_goals_.begin(), rejected_goals_.end(), [current_time](const goal_rejection& rejection)
  {
    return rejection.expire_time > 0.0f && rejection.expire_time <= current_time;
  }), rejected_goals_.end());

  auto existing = std::find_if(rejected_goals_.begin(), rejected_goals_.end(), [&goal](const goal_rejection& rejection)
  {
    return goal_rejection_matches(rejection, goal);
  });
  const auto expire_time = current_time + failed_goal_blacklist_seconds;
  if (existing != rejected_goals_.end())
  {
    existing->expire_time = std::max(existing->expire_time, expire_time);
    return;
  }

  rejected_goals_.push_back(goal_rejection{
    goal.type,
    goal.destination_area,
    goal.destination,
    goal.entity_index,
    expire_time
  });
}

bool navbot_goals::is_goal_rejected(const goal_candidate& goal, float current_time) const
{
  return std::any_of(rejected_goals_.begin(), rejected_goals_.end(), [current_time, &goal](const goal_rejection& rejection)
  {
    return (rejection.expire_time <= 0.0f || rejection.expire_time > current_time)
      && goal_rejection_matches(rejection, goal);
  });
}

}
