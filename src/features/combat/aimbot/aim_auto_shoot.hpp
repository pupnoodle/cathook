#ifndef AIM_AUTO_SHOOT_HPP
#define AIM_AUTO_SHOOT_HPP
#include "aim_state.hpp"
#include "aim_utils.hpp"
#include "aim_spread.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

namespace aim_auto_shoot {

struct result {
  bool requested = false;
  bool primary_attack = false;
  bool release_attack = false;
};

inline bool weapon_has_primary_ammo(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  if (aimbot_is_projectile_weapon(weapon)) {
    return false;
  }

  return weapon->is_melee() || weapon->get_clip1() != 0;
}

inline bool weapon_has_release_shot_ready(Weapon* weapon, bool hitscan_solution) {
  return hitscan_solution && weapon != nullptr &&
    weapon->get_def_id() == Sniper_m_TheClassic &&
    weapon->get_charged_damage() > 0.0f;
}

inline result apply(user_cmd* user_cmd,
  Weapon* weapon,
  const aimbot_candidate& candidate,
  bool hitscan_solution,
  bool melee_solution) {
  result r{};
  if (user_cmd == nullptr || weapon == nullptr ||
      !weapon_has_primary_ammo(weapon) || (!hitscan_solution && !melee_solution)) {
    return r;
  }

  if (weapon_has_release_shot_ready(weapon, hitscan_solution)) {
    user_cmd->buttons &= ~IN_ATTACK;
    r.requested = true;
    r.release_attack = true;
    return r;
  }

  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  const bool precision_rune = localplayer != nullptr && localplayer->in_cond(TF_COND_RUNE_PRECISION);
  if (hitscan_solution && aimbot_modifier_enabled(Aim::hitscan_mod_tapfire) &&
      !precision_rune &&
      aim_spread::weapon_hitscan_spread(weapon) > 0.0f &&
      candidate.distance > config.aimbot.tapfire_distance &&
      global_vars != nullptr) {
    const float time_since_last_shot = global_vars->curtime - weapon->get_last_attack();
    const float tapfire_delay = weapon->get_bullets_per_shot() > 1 ? 0.25f : 1.25f;
    if (std::isfinite(time_since_last_shot) && time_since_last_shot >= 0.0f && time_since_last_shot <= tapfire_delay) {
      user_cmd->buttons &= ~IN_ATTACK;
      return r;
    }
  }

  user_cmd->buttons |= IN_ATTACK;
  r.requested = true;
  r.primary_attack = true;
  return r;
}

}
#endif
