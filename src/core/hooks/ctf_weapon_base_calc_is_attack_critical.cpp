#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

using ctf_weapon_base_calc_is_attack_critical_fn = bool (*)(Weapon*);

ctf_weapon_base_calc_is_attack_critical_fn ctf_weapon_base_calc_is_attack_critical_original = nullptr;
ctf_weapon_base_calc_is_attack_critical_fn ctf_weapon_base_melee_calc_is_attack_critical_original = nullptr;

namespace
{

constexpr int tf_weapon_primary_mode = 0;

struct weapon_mode_guard
{
  Weapon* weapon = nullptr;
  int previous_mode = tf_weapon_primary_mode;

  explicit weapon_mode_guard(Weapon* target)
    : weapon(target)
  {
    if (weapon != nullptr) {
      previous_mode = weapon->weapon_mode();
      weapon->weapon_mode() = tf_weapon_primary_mode;
    }
  }

  ~weapon_mode_guard()
  {
    if (weapon != nullptr) {
      weapon->weapon_mode() = previous_mode;
    }
  }
};

struct crit_prediction_state
{
  float crit_token_bucket = 0.0f;
  int crit_checks = 0;
  int crit_seed_requests = 0;
  int last_crit_check_frame = 0;
  float last_crit_check_time = 0.0f;
  float last_rapid_fire_crit_check_time = 0.0f;
  float crit_time = 0.0f;
  int current_seed = 0;
};

struct framecount_guard
{
  bool active = false;
  int previous_framecount = 0;

  explicit framecount_guard(Weapon* weapon)
  {
    if (weapon == nullptr || global_vars == nullptr) {
      return;
    }

    Entity* owner_entity = weapon->to_entity()->get_owner_entity();
    Player* owner = reinterpret_cast<Player*>(owner_entity);
    user_cmd* command = owner != nullptr ? owner->current_command() : nullptr;
    if (command == nullptr || command->command_number <= 0) {
      return;
    }

    previous_framecount = global_vars->framecount;
    global_vars->framecount = command->command_number;
    active = true;
  }

  ~framecount_guard()
  {
    if (active && global_vars != nullptr) {
      global_vars->framecount = previous_framecount;
    }
  }
};

crit_prediction_state read_crit_prediction_state(Weapon* weapon)
{
  return {
    weapon->crit_token_bucket(),
    weapon->crit_checks(),
    weapon->crit_seed_requests(),
    weapon->last_crit_check_frame(),
    weapon->last_crit_check_time(),
    weapon->last_rapid_fire_crit_check_time(),
    weapon->crit_time(),
    weapon->current_seed()
  };
}

void restore_crit_prediction_state(Weapon* weapon, const crit_prediction_state& state)
{
  weapon->crit_token_bucket() = state.crit_token_bucket;
  weapon->crit_checks() = state.crit_checks;
  weapon->crit_seed_requests() = state.crit_seed_requests;
  weapon->last_crit_check_frame() = state.last_crit_check_frame;
  weapon->last_crit_check_time() = state.last_crit_check_time;
  weapon->last_rapid_fire_crit_check_time() = state.last_rapid_fire_crit_check_time;
  weapon->crit_time() = state.crit_time;
  weapon->current_seed() = state.current_seed;
}

bool run_calc_is_attack_critical_hook(
  Weapon* weapon,
  ctf_weapon_base_calc_is_attack_critical_fn original,
  bool force_primary_mode)
{
  if (weapon == nullptr || original == nullptr) {
    return false;
  }

  weapon_mode_guard mode_guard{ force_primary_mode ? weapon : nullptr };
  framecount_guard frame_guard{ weapon };

  if (prediction == nullptr || prediction->first_time_predicted) {
    return original(weapon);
  }

  const crit_prediction_state state = read_crit_prediction_state(weapon);
  const bool result = original(weapon);
  restore_crit_prediction_state(weapon, state);
  return result;
}

}

bool ctf_weapon_base_calc_is_attack_critical_hook(Weapon* weapon)
{
  CATHOOK_HOOK_GUARD();
  return run_calc_is_attack_critical_hook(
    weapon,
    ctf_weapon_base_calc_is_attack_critical_original,
    true);
}

bool ctf_weapon_base_melee_calc_is_attack_critical_hook(Weapon* weapon)
{
  CATHOOK_HOOK_GUARD();
  return run_calc_is_attack_critical_hook(
    weapon,
    ctf_weapon_base_melee_calc_is_attack_critical_original,
    false);
}
