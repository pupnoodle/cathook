#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/combat_offsets.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

using ctf_weapon_base_calc_is_attack_critical_fn = bool (*)(Weapon*);

ctf_weapon_base_calc_is_attack_critical_fn ctf_weapon_base_calc_is_attack_critical_original = nullptr;
ctf_weapon_base_calc_is_attack_critical_fn ctf_weapon_base_melee_calc_is_attack_critical_original = nullptr;

namespace
{

constexpr int tf_weapon_primary_mode = 0;

int& first_predicted_seed()
{
  static int seed = -1;
  return seed;
}

bool crit_field_offset_sane(int offset)
{
  return offset >= 256 && offset < 8192;
}

bool can_restore_crit_state()
{
  return crit_field_offset_sane(tf2_combat::weapon::crit_token_bucket()) &&
    crit_field_offset_sane(tf2_combat::weapon::crit_checks()) &&
    crit_field_offset_sane(tf2_combat::weapon::crit_seed_requests()) &&
    crit_field_offset_sane(tf2_combat::weapon::crit_time()) &&
    crit_field_offset_sane(tf2_combat::weapon::last_rapid_fire_crit_check_time()) &&
    crit_field_offset_sane(tf2_combat::weapon::current_seed());
}

bool can_write_weapon_mode()
{
  return crit_field_offset_sane(tf2_combat::weapon::weapon_mode());
}

bool first_time_predicted()
{
  return prediction != nullptr && prediction->first_time_predicted;
}

bool run_calc_is_attack_critical_hook(
  Weapon* weapon,
  ctf_weapon_base_calc_is_attack_critical_fn original,
  bool force_primary_mode)
{
  if (weapon == nullptr || original == nullptr) {
    return false;
  }

  int previous_mode = tf_weapon_primary_mode;
  const bool write_mode = force_primary_mode && can_write_weapon_mode();
  if (write_mode) {
    previous_mode = weapon->weapon_mode();
    weapon->weapon_mode() = tf_weapon_primary_mode;
  }

  if (first_time_predicted()) {
    const bool result = original(weapon);
    if (crit_field_offset_sane(tf2_combat::weapon::current_seed())) {
      first_predicted_seed() = weapon->current_seed();
    }
    if (write_mode) {
      weapon->weapon_mode() = previous_mode;
    }
    return result;
  }

  const bool restore = can_restore_crit_state();
  const float crit_token_bucket = restore ? weapon->crit_token_bucket() : 0.0f;
  const int crit_checks = restore ? weapon->crit_checks() : 0;
  const int crit_seed_requests = restore ? weapon->crit_seed_requests() : 0;
  const float last_rapid_fire_crit_check_time =
    restore ? weapon->last_rapid_fire_crit_check_time() : 0.0f;
  const float crit_time = restore ? weapon->crit_time() : 0.0f;
  const bool result = original(weapon);
  if (restore) {
    weapon->crit_token_bucket() = crit_token_bucket;
    weapon->crit_checks() = crit_checks;
    weapon->crit_seed_requests() = crit_seed_requests;
    weapon->last_rapid_fire_crit_check_time() = last_rapid_fire_crit_check_time;
    weapon->crit_time() = crit_time;
    if (first_predicted_seed() >= 0) {
      weapon->current_seed() = first_predicted_seed();
    }
  }
  if (write_mode) {
    weapon->weapon_mode() = previous_mode;
  }
  return result;
}

}

bool ctf_weapon_base_calc_is_attack_critical_hook(Weapon* weapon)
{
  PUPHOOK_HOOK_GUARD();
  return run_calc_is_attack_critical_hook(
    weapon,
    ctf_weapon_base_calc_is_attack_critical_original,
    true);
}

bool ctf_weapon_base_melee_calc_is_attack_critical_hook(Weapon* weapon)
{
  PUPHOOK_HOOK_GUARD();
  return run_calc_is_attack_critical_hook(
    weapon,
    ctf_weapon_base_melee_calc_is_attack_critical_original,
    false);
}
