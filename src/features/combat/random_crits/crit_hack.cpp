#include "crit_hack.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/net_messages.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"
#include "external/MD5/MD5.hpp"
#include <algorithm>
#include <climits>
#include <cmath>

namespace crit_hack {

namespace {

inline float math_remap(float val, float a, float b, float c, float d) {
  if (a == b) return c;
  return c + (val - a) * (d - c) / (b - a);
}

class c_valve_random {
public:
  void set_seed(int seed) {
    idum = (seed < 0 ? seed : -seed);
    iy = 0;
  }

  int generate_random_number() {
    int j;
    int k;

    if (idum <= 0 || !iy) {
      if (-idum < 1)
        idum = 1;
      else
        idum = -idum;

      for (j = 32 + 7; j >= 0; j--) {
        k = idum / 127773;
        idum = 16807 * (idum - k * 127773) - 2836 * k;
        if (idum < 0)
          idum += 2147483647;
        if (j < 32)
          iv[j] = idum;
      }
      iy = iv[0];
    }
    k = idum / 127773;
    idum = 16807 * (idum - k * 127773) - 2836 * k;
    if (idum < 0)
      idum += 2147483647;
    j = iy / (1 + (2147483646) / 32);

    if (j >= 32 || j < 0) {
      j &= 32 - 1;
    }

    iy = iv[j];
    iv[j] = idum;

    return iy;
  }

  int random_int(int low, int high) {
    unsigned int max_acceptable;
    unsigned int x = high - low + 1;
    unsigned int n;

    if (x <= 1 || 0x7FFFFFFFUL < x - 1) {
      return low;
    }

    max_acceptable = 0x7FFFFFFFUL - ((0x7FFFFFFFUL + 1) % x);
    do {
      n = generate_random_number();
    } while (n > max_acceptable);

    return low + (n % x);
  }

private:
  int idum = 0;
  int iy = 0;
  int iv[32]{};
};

c_valve_random valve_rand;

int crit_damage = 0;
int ranged_damage = 0;
int melee_damage = 0;
int resource_damage = 0;
int desync_damage = 0;
std::unordered_map<int, health_history_t> health_history{};

bool crit_banned = false;
float damage_till_flip = 0.0f;

float current_damage = 0.0f;
float current_cost = 0.0f;
float current_bucket = 0.0f;
float current_bucket_cap = 1000.0f;
float current_observed_chance = 0.0f;
float current_allowed_chance = 0.0f;
int available_crits = 0;
int potential_crits = 0;
int next_crit = 0;
int damage_till_crit = 0;
int queued_crit_command = 0;
int queued_ticks = 0;
bool queued_force_active = false;
bool current_hold_attack = false;
queue_state current_queue_state = queue_state::idle;
bool pending_force_attack = false;
int pending_force_weapon_ent_index = 0;

int weapon_ent_index = 0;
bool is_melee_weapon = false;
float crit_chance = 0.0f;
float mult_crit_chance = 1.0f;

Weapon* cached_weapon = nullptr;
float cached_bucket = 0.0f;
int cached_crit_checks = 0;
int cached_crit_seed_requests = 0;
bool weapon_info_cache_valid = false;

constexpr int max_safe_choked_commands = 21;

enum class crit_request {
  any,
  crit,
  skip
};

void reset_weapon_info() {
  current_damage = 0.0f;
  current_cost = 0.0f;
  current_bucket = 0.0f;
  current_bucket_cap = 1000.0f;
  current_observed_chance = 0.0f;
  current_allowed_chance = 0.0f;
  available_crits = 0;
  potential_crits = 0;
  next_crit = 0;
  damage_till_crit = 0;
  queued_crit_command = 0;
  queued_ticks = 0;
  queued_force_active = false;
  current_hold_attack = false;
  current_queue_state = queue_state::idle;
  pending_force_attack = false;
  pending_force_weapon_ent_index = 0;
  weapon_ent_index = 0;
  is_melee_weapon = false;
  crit_chance = 0.0f;
  mult_crit_chance = 1.0f;
  cached_weapon = nullptr;
  cached_bucket = 0.0f;
  cached_crit_checks = 0;
  cached_crit_seed_requests = 0;
  weapon_info_cache_valid = false;
}

inline Entity* get_player_resource_entity() {
  return player_resource_entity();
}

template <typename value_type>
inline value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index) {
  if (player_resource == nullptr || player_index <= 0) {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) + (static_cast<std::uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

int command_to_seed(int command_number, Weapon* weapon, bool melee) {
  int seed = MD5_PseudoRandom(command_number) & std::numeric_limits<int>::max();
  int local_player = engine->get_localplayer_index();
  int mask = melee
    ? (weapon->to_entity()->get_index() << 16) | (local_player << 8)
    : (weapon->to_entity()->get_index() << 8) | local_player;
  return seed ^ mask;
}

bool is_crit_seed(int seed, Weapon* weapon, bool crit, bool safe, bool melee) {
  if (seed == weapon->current_seed())
    return false;

  valve_rand.set_seed(seed);
  int random_val = valve_rand.random_int(0, 9999);

  if (safe) {
    int lower, upper;
    if (melee)
      lower = 1500, upper = 6000;
    else
      lower = 100, upper = 800;

    lower = static_cast<int>(lower * mult_crit_chance);
    upper = static_cast<int>(upper * mult_crit_chance);

    if (crit ? lower >= 0 : upper < 10000)
      return crit ? random_val < lower : !(random_val < upper);
  }

  int range = static_cast<int>(crit_chance * 10000.0f);
  return crit ? random_val < range : !(random_val < range);
}

bool is_crit_command(int command_number, Weapon* weapon, bool crit, bool safe, bool melee) {
  int seed = command_to_seed(command_number, weapon, melee);
  return is_crit_seed(seed, weapon, crit, safe, melee);
}

int get_crit_command(Weapon* weapon, int command_number, int max_commands, bool crit, bool safe, bool melee) {
  if (max_commands <= 0) {
    return 0;
  }

  for (int i = command_number; i < command_number + max_commands; i++) {
    if (is_crit_command(i, weapon, crit, safe, melee))
      return i;
  }
  return 0;
}

float active_rapid_fire_crit_check_time(Weapon* weapon) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  static Convar* tf_weapon_criticals_nopred = nullptr;
  if (tf_weapon_criticals_nopred == nullptr && convar_system != nullptr) {
    tf_weapon_criticals_nopred = convar_system->find_var("tf_weapon_criticals_nopred");
  }

  return tf_weapon_criticals_nopred != nullptr && tf_weapon_criticals_nopred->get_int() != 0
    ? weapon->last_crit_check_time()
    : weapon->last_rapid_fire_crit_check_time();
}

int safe_queued_command_window() {
  const int choked = client_state != nullptr ? std::clamp(client_state->chokedcommands, 0, max_safe_choked_commands) : 0;
  const int protocol_room = std::max(0, max_new_commands - choked);
  const int choke_room = std::max(0, max_safe_choked_commands - choked);
  return std::min(protocol_room, choke_room);
}

float get_crit_bucket_cap() {
  static Convar* cvar = nullptr;
  if (cvar == nullptr && convar_system != nullptr) {
    cvar = convar_system->find_var("tf_weapon_criticals_bucket_cap");
  }

  return cvar != nullptr ? cvar->get_float() : 1000.0f;
}

float crit_cost_multiplier(bool melee, int checks, int seed_requests) {
  if (melee) {
    return 0.5f;
  }

  if (checks <= 0) {
    return 1.0f;
  }

  return math_remap(
    std::clamp(static_cast<float>(seed_requests) / static_cast<float>(checks), 0.1f, 1.0f),
    0.1f,
    1.0f,
    1.0f,
    3.0f);
}

int count_withdrawable_crits(
  float bucket,
  int checks,
  int seed_requests,
  float bucket_damage,
  float withdraw_damage,
  float bucket_cap,
  bool melee,
  int max_tests = 1000) {
  int crits = 0;

  for (int i = 0; i < max_tests; ++i) {
    ++checks;
    ++seed_requests;

    if (bucket < bucket_cap) {
      bucket = std::min(bucket + bucket_damage, bucket_cap);
    }

    const float cost = withdraw_damage * 3.0f * crit_cost_multiplier(melee, checks, seed_requests);
    if (cost > bucket) {
      break;
    }

    bucket -= cost;
    ++crits;
  }

  return crits;
}

int compute_damage_till_bucket_allows_crit(
  float bucket,
  int checks,
  int seed_requests,
  float bucket_damage,
  float withdraw_damage,
  float bucket_cap,
  bool melee,
  int current_available) {
  if (bucket_damage <= 0.0f) {
    return 0;
  }

  float test_bucket = bucket;
  int test_checks = checks;
  int shots = 0;

  for (; shots < 1000; ++shots) {
    const int crits = count_withdrawable_crits(
      test_bucket,
      test_checks,
      seed_requests,
      bucket_damage,
      withdraw_damage,
      bucket_cap,
      melee,
      1000);
    if (crits > current_available || (current_available <= 0 && crits > 0)) {
      break;
    }

    ++test_checks;
    if (test_bucket < bucket_cap) {
      test_bucket = std::min(test_bucket + bucket_damage, bucket_cap);
    }

  }

  return static_cast<int>(std::ceil(static_cast<float>(shots) * bucket_damage));
}

void update_weapon_info(Player* local, Weapon* weapon) {
  if (local == nullptr || weapon == nullptr || weapon->to_entity() == nullptr) {
    reset_weapon_info();
    return;
  }

  weapon_ent_index = weapon->to_entity()->get_index();
  is_melee_weapon = weapon->is_melee();

  if (is_melee_weapon) {
    crit_chance = 0.15f * local->get_crit_mult();
  } else if (weapon->is_rapid_fire()) {
    crit_chance = 0.02f * local->get_crit_mult();
    float non_crit_duration = (2.0f / crit_chance) - 2.0f;
    crit_chance = 1.0f / non_crit_duration;
  } else {
    crit_chance = 0.02f * local->get_crit_mult();
  }

  mult_crit_chance = attribute_manager->attrib_hook_value(1.0f, "mult_crit_chance", weapon->to_entity());
  crit_chance *= mult_crit_chance;

  const float bucket = weapon->crit_token_bucket();
  const int crit_checks_val = weapon->crit_checks();
  const int crit_seed_requests_val = weapon->crit_seed_requests();
  const bool unchanged = weapon_info_cache_valid &&
    weapon == cached_weapon &&
    bucket == cached_bucket &&
    crit_checks_val == cached_crit_checks &&
    crit_seed_requests_val == cached_crit_seed_requests;

  cached_weapon = weapon;
  cached_bucket = bucket;
  cached_crit_checks = crit_checks_val;
  cached_crit_seed_requests = crit_seed_requests_val;
  weapon_info_cache_valid = true;

  if (unchanged)
    return;

  const float bucket_cap = get_crit_bucket_cap();
  bool rapid_fire = weapon->is_rapid_fire();
  float fire_rate = weapon->get_fire_rate();

  float damage = static_cast<float>(weapon->get_damage());
  int projectiles_per_shot = weapon->get_bullets_per_shot();
  if (!is_melee_weapon && projectiles_per_shot > 0)
    projectiles_per_shot = static_cast<int>(attribute_manager->attrib_hook_value(static_cast<float>(projectiles_per_shot), "mult_bullets_per_shot", weapon->to_entity()));
  else
    projectiles_per_shot = 1;

  float base_damage = damage * projectiles_per_shot;
  if (rapid_fire) {
    damage = base_damage * (2.0f / fire_rate);
    if (damage * 3.0f > bucket_cap)
      damage = bucket_cap / 3.0f;
  } else {
    damage = base_damage;
  }

  float mult = crit_cost_multiplier(is_melee_weapon, crit_checks_val + 1, crit_seed_requests_val + 1);
  float cost = damage * 3.0f;

  const int available = count_withdrawable_crits(
    bucket,
    crit_checks_val,
    crit_seed_requests_val,
    base_damage,
    damage,
    bucket_cap,
    is_melee_weapon);
  const int potential = count_withdrawable_crits(
    bucket_cap,
    crit_checks_val,
    crit_seed_requests_val,
    base_damage,
    damage,
    bucket_cap,
    is_melee_weapon);

  int next = 0;
  if (available != potential) {
    int test_shots = crit_checks_val;
    int test_crits = crit_seed_requests_val;
    float test_bucket = bucket;
    float tick_base = global_vars->curtime;
    float last_rapid_crit_time = active_rapid_fire_crit_check_time(weapon);
    for (int i = 0; i < 1000; i++) {
      int crits = 0;
      {
        int test_shots2 = test_shots;
        int test_crits2 = test_crits;
        float test_bucket2 = test_bucket;
        for (int j = 0; j < 1000; j++) {
          test_shots2++;
          test_crits2++;

          float test_mult = crit_cost_multiplier(is_melee_weapon, test_shots2, test_crits2);
          if (test_bucket2 < bucket_cap)
            test_bucket2 = std::min(test_bucket2 + base_damage, bucket_cap);
          test_bucket2 -= cost * test_mult;
          if (test_bucket2 < 0.0f)
            break;

          crits++;
        }
      }
      if (available < crits)
        break;

      if (!rapid_fire) {
        test_shots++;
      } else {
        tick_base += std::ceil(fire_rate / 0.015f) * 0.015f;
        if (tick_base >= last_rapid_crit_time + 1.0f || (i == 0 && test_bucket == bucket_cap)) {
          test_shots++;
          last_rapid_crit_time = tick_base;
        }
      }

      if (test_bucket < bucket_cap)
        test_bucket = std::min(test_bucket + base_damage, bucket_cap);

      next++;
    }
  }

  current_damage = base_damage;
  current_cost = cost * mult;
  current_bucket = bucket;
  current_bucket_cap = bucket_cap;
  potential_crits = potential;
  available_crits = available;
  next_crit = next;
  damage_till_crit = compute_damage_till_bucket_allows_crit(
    bucket,
    crit_checks_val,
    crit_seed_requests_val,
    base_damage,
    damage,
    bucket_cap,
    is_melee_weapon,
    available);
}

void update_info(Player* local, Weapon* weapon) {
  update_weapon_info(local, weapon);

  crit_banned = false;
  damage_till_flip = 0.0f;
  current_observed_chance = 0.0f;
  current_allowed_chance = std::clamp(crit_chance + 0.1f, 0.0f, 1.0f);
  if (!is_melee_weapon) {
    const float normalized_crit_damage = static_cast<float>(crit_damage) / 3.0f;
    const float non_crit_damage = static_cast<float>(ranged_damage - crit_damage);
    if (ranged_damage > 0 && crit_damage > 0) {
      current_observed_chance = normalized_crit_damage / (normalized_crit_damage + std::max(0.0f, non_crit_damage));
    }

    const float weapon_observed = weapon->observed_crit_chance();
    if (std::isfinite(weapon_observed) && weapon_observed > 0.0f) {
      current_observed_chance = std::max(current_observed_chance, weapon_observed);
    }

    crit_banned = current_observed_chance > current_allowed_chance;

    if (crit_banned && normalized_crit_damage > 0.0f && current_allowed_chance > 0.0f) {
      damage_till_flip = std::max(
        0.0f,
        (normalized_crit_damage / current_allowed_chance) - normalized_crit_damage - std::max(0.0f, non_crit_damage));
    }
  }

  auto* resource = get_player_resource_entity();
  if (resource != nullptr) {
    static const int damage_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iDamage" });
    if (damage_offset > 0) {
      resource_damage = read_player_resource_value<int>(resource, damage_offset, engine->get_localplayer_index());
      desync_damage = ranged_damage + melee_damage - resource_damage;
    }
  }
}

crit_request get_crit_request(user_cmd* cmd, Weapon* weapon) {
  bool can_crit = available_crits > 0 && !crit_banned;
  bool pressed = config.crithack.force_crits;
  if (config.crithack.always_melee && is_melee_weapon) {
    pressed = true;
  }

  bool skip = config.crithack.avoid_random;
  bool desync = command_to_seed(cmd->command_number, weapon, is_melee_weapon) == weapon->current_seed();

  return can_crit && pressed ? crit_request::crit : (skip || desync ? crit_request::skip : crit_request::any);
}

bool is_attack_command(user_cmd* cmd, Weapon* weapon) {
  if (cmd == nullptr || weapon == nullptr) {
    return false;
  }

  if (is_melee_weapon) {
    if (weapon->can_primary_attack() && (cmd->buttons & IN_ATTACK)) {
      return true;
    }

    return weapon->get_weapon_id() == TF_WEAPON_FISTS
        && weapon->can_primary_attack()
        && (cmd->buttons & IN_ATTACK2);
  }

  return (cmd->buttons & IN_ATTACK) != 0;
}

void suppress_attack(user_cmd* cmd, Weapon* weapon) {
  if (cmd == nullptr) {
    return;
  }

  cmd->buttons &= ~IN_ATTACK;
  if (weapon != nullptr && is_melee_weapon && weapon->get_weapon_id() == TF_WEAPON_FISTS) {
    cmd->buttons &= ~IN_ATTACK2;
  }
}

}

create_move_result on_create_move(user_cmd* cmd, bool aimbot_requested_shot) {
  create_move_result result{};
  current_hold_attack = false;
  queued_force_active = false;
  queued_crit_command = 0;
  queued_ticks = 0;
  current_queue_state = queue_state::idle;

  auto* local = entity_list->get_localplayer();
  if (!config.crithack.enabled || local == nullptr || !local->is_alive() || local->is_dormant()) {
    reset_weapon_info();
    return result;
  }

  auto* weapon = local->get_weapon();
  if (weapon == nullptr || !weapon_can_crit(weapon)) {
    reset_weapon_info();
    return result;
  }

  const int current_weapon_ent_index = weapon->to_entity()->get_index();
  if (pending_force_attack && pending_force_weapon_ent_index != current_weapon_ent_index) {
    pending_force_attack = false;
    pending_force_weapon_ent_index = 0;
  }

  update_info(local, weapon);

  if (local->is_crit_boosted() || weapon->crit_time() > global_vars->curtime) {
    pending_force_attack = false;
    pending_force_weapon_ent_index = 0;
    return result;
  }

  const bool force_enabled = config.crithack.force_crits ||
    (config.crithack.always_melee && is_melee_weapon);
  if (!force_enabled || crit_banned) {
    pending_force_attack = false;
    pending_force_weapon_ent_index = 0;
  }

  for (int i = 1; i <= global_vars->max_clients; i++) {
    auto* player = entity_list->player_from_index(i);
    if (player && player->is_alive() && !player->is_dormant()) {
      store_health_history(i, player->get_health(), player);
    }
  }

  bool attacking = is_attack_command(cmd, weapon) || aimbot_requested_shot;

  // A forced request can outlive the single input command that created it.
  // This matters for a one-frame manual click and for aimbot shots that were
  // suppressed while waiting for the selected command seed.
  if (!attacking && pending_force_attack &&
      pending_force_weapon_ent_index == current_weapon_ent_index &&
      weapon->can_primary_attack()) {
    cmd->buttons |= IN_ATTACK;
    attacking = true;
  }

  if (!attacking) {
    return result;
  }

  if (weapon->is_rapid_fire() && global_vars->curtime < active_rapid_fire_crit_check_time(weapon) + 1.0f) {
    current_queue_state = queue_state::blocked;
    return result;
  }

  crit_request req = get_crit_request(cmd, weapon);
  if (req == crit_request::any) {
    pending_force_attack = false;
    pending_force_weapon_ent_index = 0;
    result.attack_allowed = true;
    return result;
  }

  const bool wants_crit = req == crit_request::crit;
  result.crit_requested = wants_crit;
  result.skip_requested = req == crit_request::skip;

  if (!is_crit_command(cmd->command_number, weapon, wants_crit, true, is_melee_weapon)) {
    if (wants_crit) {
      const int queued_command = get_crit_command(
        weapon,
        cmd->command_number,
        safe_queued_command_window(),
        true,
        true,
        is_melee_weapon);
      if (queued_command <= 0) {
        current_queue_state = queue_state::blocked;
        pending_force_attack = true;
        pending_force_weapon_ent_index = current_weapon_ent_index;
        current_hold_attack = true;
        suppress_attack(cmd, weapon);
        result.attack_suppressed = true;
        return result;
      }

      queued_crit_command = queued_command;
      queued_ticks = std::max(0, queued_crit_command - cmd->command_number);
      current_queue_state = queue_state::waiting_for_seed;
      queued_force_active = true;
      pending_force_attack = true;
      pending_force_weapon_ent_index = current_weapon_ent_index;
    } else {
      current_queue_state = queue_state::waiting_for_seed;
    }

    current_hold_attack = true;
    suppress_attack(cmd, weapon);
    result.attack_suppressed = true;
    return result;
  }

  current_queue_state = wants_crit ? queue_state::releasing : queue_state::idle;
  if (wants_crit) {
    pending_force_attack = false;
    pending_force_weapon_ent_index = 0;
  }
  result.attack_allowed = true;
  return result;
}

void on_game_event(GameEvent* event) {
  if (event == nullptr) return;

  const char* event_name_ptr = event->get_name();
  if (event_name_ptr == nullptr) return;
  std::string event_name{ event_name_ptr };

  auto* local = entity_list->get_localplayer();

  if (event_name == "player_hurt") {
    if (local == nullptr) return;

    int victim_id = event->get_int("userid");
    int attacker_id = event->get_int("attacker");
    bool crit = event->get_bool("crit");
    int damage = event->get_int("damageamount");
    int health = event->get_int("health");
    int weapon_id = event->get_int("weaponid");

    auto* victim = entity_list->get_player_from_id(victim_id);
    auto* attacker = entity_list->get_player_from_id(attacker_id);

    if (victim != nullptr) {
      int victim_idx = victim->get_index();
      if (health_history.count(victim_idx)) {
        auto& history = health_history[victim_idx];
        if (!health) {
          damage = std::clamp(damage, 0, history.new_health);
          history.spawn_counter = -1;
        } else {

          if (victim->in_cond(TF_COND_FEIGN_DEATH)) {
            int old_h = (history.history_map.count(health) ? history.history_map[health].old_health : history.new_health) % 32768;
            if (health > old_h) {
              for (const auto& [h, storage] : history.history_map) {
                int old_h2 = storage.old_health % 32768;
                if (old_h2 > health) {
                  old_h = health > old_h ? old_h2 : std::min(old_h, old_h2);
                }
              }
            }
            damage = std::clamp(old_h - health, 0, damage);
          }
        }
      }
      if (health) {
        store_health_history(victim_idx, health, victim);
      }
    }

    if (attacker == nullptr || attacker != local || victim == attacker) {
      return;
    }

    const char* level_name = engine->get_level_name();
    bool is_mvm = level_name != nullptr && std::strstr(level_name, "mvm_") != nullptr;
    const int max_acceptable_damage = is_mvm ? 5000 : 1500;
    if (damage > max_acceptable_damage) {
      return;
    }

    Weapon* weapon = nullptr;
    for (int i = 0; i < 48; i++) {
      auto* weapon2 = local->get_weapon_at(i);
      if (weapon2 && weapon2->get_weapon_id() == weapon_id) {
        weapon = weapon2;
        break;
      }
    }

    if (weapon == nullptr || !weapon->is_melee()) {
      ranged_damage += damage;
      if (crit && !local->is_crit_boosted()) {
        crit_damage += damage;
      }
    } else {
      melee_damage += damage;
    }
  } else if (event_name == "player_spawn") {
    int victim_id = event->get_int("userid");
    auto* victim = entity_list->get_player_from_id(victim_id);
    if (victim != nullptr) {
      int victim_idx = victim->get_index();
      if (health_history.count(victim_idx)) {
        health_history[victim_idx].spawn_counter = -1;
      }
    }
  } else if (event_name == "scorestats_accumulated_update" || event_name == "mvm_reset_stats") {
    ranged_damage = crit_damage = melee_damage = 0;
  } else if (event_name == "client_beginconnect" || event_name == "client_disconnect" || event_name == "game_newmap") {
    reset();
  }
}

void reset() {
  crit_damage = 0;
  ranged_damage = 0;
  melee_damage = 0;
  resource_damage = 0;
  desync_damage = 0;
  crit_banned = false;
  damage_till_flip = 0.0f;
  reset_weapon_info();
  health_history.clear();
}

void store_health_history(int index, int health, Player* player) {
  bool contains = health_history.count(index) > 0;
  auto& history = health_history[index];

  if (contains && player != nullptr) {
    if (player->is_dormant()) {
      history.spawn_counter = -1;
    } else {
      static const int spawn_counter_offset = tf2_netvars::find_offset("DT_BasePlayer", { "m_iSpawnCounter" });
      if (spawn_counter_offset > 0) {
        int sc = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(player) + spawn_counter_offset);
        if (history.spawn_counter == -1)
          history.spawn_counter = sc;
        else if (history.spawn_counter != sc)
          return;
      }
    }
  }

  if (!contains) {
    history.new_health = health;
    history.old_health = health;
    history.spawn_counter = -1;
  } else if (health != history.new_health) {
    history.old_health = std::max(history.new_health, health);
    history.new_health = health;
  }

  history.history_map[health % 32768] = { history.old_health, global_vars->curtime };

  while (history.history_map.size() > 3) {
    int oldest_health = 0;
    float min_time = std::numeric_limits<float>::max();
    for (const auto& [h, storage] : history.history_map) {
      if (storage.time < min_time) {
        min_time = storage.time;
        oldest_health = h;
      }
    }
    history.history_map.erase(oldest_health);
  }
}

bool weapon_can_crit(Weapon* weapon, bool weapon_only) {
  if (weapon == nullptr) return false;
  auto* weapon_entity = weapon->to_entity();
  if (weapon_entity == nullptr) return false;
  if (!weapon_only) {
    auto* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
    if (local == nullptr || !local->is_alive() || local->is_dormant()) return false;
  }
  if (!weapon_only && !weapon->are_random_crits_enabled()) return false;
  if (attribute_manager->attrib_hook_value(1.0f, "mult_crit_chance", weapon_entity) <= 0.0f) return false;

  switch (weapon->get_weapon_id()) {
    case TF_WEAPON_PDA:
    case TF_WEAPON_PDA_ENGINEER_BUILD:
    case TF_WEAPON_PDA_ENGINEER_DESTROY:
    case TF_WEAPON_PDA_SPY:
    case TF_WEAPON_PDA_SPY_BUILD:
    case TF_WEAPON_BUILDER:
    case TF_WEAPON_INVIS:
    case TF_WEAPON_JAR_MILK:
    case TF_WEAPON_LUNCHBOX:
    case TF_WEAPON_BUFF_ITEM:
    case TF_WEAPON_LASER_POINTER:
    case TF_WEAPON_MEDIGUN:
    case TF_WEAPON_SNIPERRIFLE:
    case TF_WEAPON_SNIPERRIFLE_DECAP:
    case TF_WEAPON_SNIPERRIFLE_CLASSIC:
    case TF_WEAPON_COMPOUND_BOW:
    case TF_WEAPON_JAR:
    case TF_WEAPON_KNIFE:
    case TF_WEAPON_PASSTIME_GUN:
      return false;
  }

  return true;
}

int predict_command_number(user_cmd* cmd) {
  return cmd != nullptr ? cmd->command_number : 0;
}

bool should_hold_attack(user_cmd* cmd) {
  return cmd != nullptr && current_hold_attack;
}

bool wants_queued_force(user_cmd* cmd) {
  return cmd != nullptr && queued_force_active;
}

bool has_pending_queued_force() {
  return pending_force_attack || queued_force_active;
}

bool is_command_crit(user_cmd* cmd, int command_number) {
  auto* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (cmd == nullptr || local == nullptr || !local->is_alive() || local->is_dormant()) {
    return false;
  }

  auto* weapon = local->get_weapon();
  if (weapon == nullptr || !weapon_can_crit(weapon)) {
    return false;
  }

  update_info(local, weapon);
  return is_crit_command(command_number, weapon, true, true, weapon->is_melee());
}

int find_queued_crit_command(user_cmd* cmd, int max_commands) {
  auto* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (cmd == nullptr || local == nullptr || !local->is_alive() || local->is_dormant()) {
    current_queue_state = queue_state::blocked;
    return 0;
  }

  auto* weapon = local->get_weapon();
  if (weapon == nullptr || !weapon_can_crit(weapon)) {
    current_queue_state = queue_state::blocked;
    return 0;
  }

  update_info(local, weapon);

  if (available_crits <= 0 || crit_banned || local->is_crit_boosted() || weapon->crit_time() > global_vars->curtime) {
    current_queue_state = queue_state::blocked;
    return 0;
  }

  if (weapon->is_rapid_fire() && global_vars->curtime < active_rapid_fire_crit_check_time(weapon) + 1.0f) {
    current_queue_state = queue_state::blocked;
    return 0;
  }

  const int bounded_commands = std::min(max_commands, safe_queued_command_window());
  const int target_command = get_crit_command(weapon, cmd->command_number, bounded_commands, true, true, weapon->is_melee());
  queued_crit_command = target_command;
  queued_ticks = target_command > 0 ? std::max(0, target_command - cmd->command_number) : 0;
  current_queue_state = target_command > 0
    ? (target_command == cmd->command_number ? queue_state::releasing : queue_state::waiting_for_seed)
    : queue_state::blocked;
  return target_command;
}

void notify_queued_release(int command_number) {
  if (command_number > 0 && queued_crit_command == command_number) {
    current_queue_state = queue_state::releasing;
    current_hold_attack = false;
    queued_force_active = false;
    pending_force_attack = false;
    pending_force_weapon_ent_index = 0;
  }
}

crit_stats_t get_stats() {
  crit_stats_t stats;
  stats.damage = current_damage;
  stats.cost = current_cost;
  stats.bucket = current_bucket;
  stats.bucket_cap = current_bucket_cap;
  stats.observed_chance = current_observed_chance;
  stats.allowed_chance = current_allowed_chance;
  stats.available = available_crits;
  stats.potential = potential_crits;
  stats.next = next_crit;
  stats.damage_till_crit = damage_till_crit;
  stats.queued_command = queued_crit_command;
  stats.queued_ticks = queued_ticks;
  stats.banned = crit_banned;
  stats.damage_till_flip = static_cast<int>(std::ceil(damage_till_flip));
  stats.queue = current_queue_state;
  return stats;
}

}
