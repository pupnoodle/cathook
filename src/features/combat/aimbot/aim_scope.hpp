#ifndef AIM_SCOPE_HPP
#define AIM_SCOPE_HPP
#include "aim_utils.hpp"

namespace aim_scope {

enum class action {
  none,
  scope,
  unscope
};

struct decision {
  action requested = action::none;
  aimbot_debug_reason reason = aimbot_debug_reason::none;
};

inline int pending_scope_state = -1;
inline float pending_scope_request_time = -FLT_MAX;
inline float auto_scope_last_target_time = -FLT_MAX;
inline int last_scope_transition_state = -1;
inline float last_scope_transition_time = -FLT_MAX;

inline bool is_sniper_rifle(Player* localplayer, Weapon* weapon) {
  return localplayer != nullptr &&
    weapon != nullptr &&
    localplayer->get_tf_class() == tf_class::SNIPER &&
    weapon->is_sniper_rifle();
}

inline bool can_toggle(Player* localplayer, Weapon* weapon) {
  return is_sniper_rifle(localplayer, weapon);
}

inline void reset_auto_scope() {
  pending_scope_state = -1;
  pending_scope_request_time = -FLT_MAX;
  auto_scope_last_target_time = -FLT_MAX;
  last_scope_transition_state = -1;
  last_scope_transition_time = -FLT_MAX;
}

inline bool scoped_only(Player* localplayer, Weapon* weapon) {
  return is_sniper_rifle(localplayer, weapon) &&
    aimbot_modifier_enabled(Aim::hitscan_mod_scoped_only);
}

inline bool target_allows_no_scope(Player* localplayer, Entity* target) {
  if (localplayer == nullptr || target == nullptr ||
      target->get_class_id() != class_id::PLAYER) {
    return false;
  }

  if (localplayer->get_weapon() != nullptr &&
      localplayer->get_weapon()->get_def_id() == Sniper_m_TheMachina) {
    return false;
  }

  const int health = aimbot_entity_health(target);
  if (health <= (localplayer->get_tf_class() == tf_class::SNIPER ? 50 : 0)) {
    return health > 0;
  }
  if (localplayer->is_crit_boosted() && health <= 150) {
    return true;
  }
  if (localplayer->in_cond(TF_COND_CRITBOOSTED_RUNE_TEMP) && health <= 68) {
    return true;
  }
  return false;
}

inline bool target_within_auto_scope_range(Player* localplayer, const Vec3& target_origin) {
  if (localplayer == nullptr || !aimbot_vec3_is_finite(target_origin)) {
    return false;
  }

  const float auto_scope_range = std::clamp(config.aimbot.sniper_scope_distance, 250.0f, 4000.0f);
  const Vec3 delta = target_origin - localplayer->get_origin();
  if (!aimbot_vec3_is_finite(delta)) {
    return false;
  }
  return (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z) <=
    auto_scope_range * auto_scope_range;
}

inline bool target_within_auto_scope_range(Player* localplayer, Entity* entity) {
  return entity != nullptr && !entity->is_dormant() &&
    target_within_auto_scope_range(localplayer, entity->get_origin());
}

inline bool enemy_target_within_auto_scope_range(Player* localplayer) {
  if (localplayer == nullptr) {
    return false;
  }

  Weapon* weapon = localplayer->get_weapon();
  if (weapon == nullptr) {
    return false;
  }

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    if (entry.player != nullptr &&
        aimbot_player_skip_reason_for(localplayer, entry, weapon) == aimbot_player_skip_reason::none &&
        !target_allows_no_scope(localplayer, entry.player) &&
        target_within_auto_scope_range(localplayer, entry.origin)) {
      return true;
    }
  }

  constexpr class_id building_ids[] = {
    class_id::SENTRY,
    class_id::DISPENSER,
    class_id::TELEPORTER
  };
  for (const class_id building_id : building_ids) {
    for (Entity* building : entity_cache_entities(building_id)) {
      if (building != nullptr &&
          !aimbot_should_skip_non_player_target(localplayer, building) &&
          target_within_auto_scope_range(localplayer, building)) {
        return true;
      }
    }
  }

  for (Entity* npc : entity_cache_npcs()) {
    if (npc != nullptr &&
        aimbot_entity_is_enemy_owned(localplayer, npc) &&
        target_within_auto_scope_range(localplayer, npc)) {
      return true;
    }
  }
  return false;
}

inline decision resolve(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!can_toggle(localplayer, weapon)) {
    reset_auto_scope();
    return {};
  }

  const float now = global_vars != nullptr ? global_vars->curtime : 0.0f;
  const bool target_found = candidate.entity != nullptr;
  if (target_found) {
    auto_scope_last_target_time = now;
  }

  const bool selected_target_needs_scope = target_found &&
    !target_allows_no_scope(localplayer, candidate.entity);
  const bool should_scope = config.aimbot.sniper_auto_scope &&
    (selected_target_needs_scope || enemy_target_within_auto_scope_range(localplayer));
  const bool scope_confirmed = aimbot_sniper_scope_confirmed(localplayer);
  const bool should_unscope = config.aimbot.sniper_auto_unscope &&
    !target_found &&
    scope_confirmed &&
    auto_scope_last_target_time > -FLT_MAX &&
    now - auto_scope_last_target_time >=
      std::max(0.0f, config.aimbot.sniper_scope_cancel_time) &&
    !enemy_target_within_auto_scope_range(localplayer);
  if (!should_scope && !should_unscope) {
    pending_scope_state = -1;
    pending_scope_request_time = -FLT_MAX;
    return {};
  }
  if (should_scope && scope_confirmed) {
    pending_scope_state = -1;
    pending_scope_request_time = -FLT_MAX;
    return {};
  }

  const int desired_state = should_scope ? 1 : 0;

  constexpr float scope_toggle_retry_seconds = 0.35f;
  if (pending_scope_state == desired_state &&
      now >= pending_scope_request_time &&
      now - pending_scope_request_time < scope_toggle_retry_seconds) {
    return {};
  }

  pending_scope_state = desired_state;
  pending_scope_request_time = now;
  last_scope_transition_state = desired_state;
  last_scope_transition_time = now;
  return {
    .requested = should_scope ? action::scope : action::unscope,
    .reason = should_scope ? aimbot_debug_reason::auto_scope : aimbot_debug_reason::auto_unscope
  };
}

inline bool fire_ready(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) {
    return false;
  }

  if (scoped_only(localplayer, weapon) && !aimbot_sniper_scope_confirmed(localplayer)) {
    return false;
  }

  if (scoped_only(localplayer, weapon) &&
      aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot) &&
      weapon->is_headshot_weapon() &&
      !aimbot_sniper_scope_time_ready(localplayer)) {
    return false;
  }

  if (pending_scope_state == 1 && !aimbot_sniper_scope_confirmed(localplayer)) {
    return false;
  }

  if (pending_scope_state == 0 && aimbot_sniper_scope_confirmed(localplayer)) {
    return false;
  }

  if (last_scope_transition_state == 0 &&
      global_vars != nullptr &&
      global_vars->curtime - last_scope_transition_time < 0.35f) {
    return false;
  }

  if (!aimbot_sniper_scope_confirmed(localplayer) &&
      aimbot_weapon_requires_scope(weapon)) {
    return false;
  }
  return true;
}

inline bool apply(user_cmd* cmd, const decision& value) {
  if (cmd == nullptr || value.requested == action::none) {
    return false;
  }

  cmd->buttons |= IN_ATTACK2;
  cmd->buttons &= ~IN_ATTACK;
  return true;
}

}
#endif
