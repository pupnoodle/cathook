#include <array>
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/net_messages.hpp"

using cl_process_packet_entities_fn = bool (*)(void*, svc_packet_entities_message*);

cl_process_packet_entities_fn cl_process_packet_entities_original = nullptr;

namespace
{

struct packet_weapon_state
{
  bool valid = false;
  float crit_token_bucket = 0.0f;
  int crit_checks = 0;
  int crit_seed_requests = 0;
  int last_crit_check_frame = 0;
  float last_rapid_fire_crit_check_time = 0.0f;
  float crit_time = 0.0f;
};

using packet_weapon_states = std::array<packet_weapon_state, Player::max_weapon_count>;

packet_weapon_state read_packet_weapon_state(Player* localplayer, int slot)
{
  packet_weapon_state state{};
  Weapon* weapon = localplayer->get_weapon_at(slot);
  if (weapon == nullptr) {
    return state;
  }

  state.valid = true;
  state.crit_token_bucket = weapon->crit_token_bucket();
  state.crit_checks = weapon->crit_checks();
  state.crit_seed_requests = weapon->crit_seed_requests();
  state.last_crit_check_frame = weapon->last_crit_check_frame();
  state.last_rapid_fire_crit_check_time = weapon->last_rapid_fire_crit_check_time();
  state.crit_time = weapon->crit_time();
  return state;
}

bool read_packet_weapon_states(Player* localplayer, packet_weapon_states& states)
{
  bool has_weapon = false;
  for (int slot = 0; slot < Player::max_weapon_count; ++slot) {
    states[slot] = read_packet_weapon_state(localplayer, slot);
    has_weapon = has_weapon || states[slot].valid;
  }

  return has_weapon;
}

void restore_packet_weapon_state(Player* localplayer, int slot, const packet_weapon_state& state)
{
  if (!state.valid) {
    return;
  }

  Weapon* weapon = localplayer->get_weapon_at(slot);
  if (weapon == nullptr) {
    return;
  }

  if (tf2_combat::weapon::crit_token_bucket() != 0) {
    weapon->crit_token_bucket() = state.crit_token_bucket;
  }
  if (tf2_combat::weapon::crit_checks() != 0) {
    weapon->crit_checks() = state.crit_checks;
  }
  if (tf2_combat::weapon::crit_seed_requests() != 0) {
    weapon->crit_seed_requests() = state.crit_seed_requests;
  }
  if (tf2_combat::weapon::last_crit_check_frame() != 0) {
    weapon->last_crit_check_frame() = state.last_crit_check_frame;
  }
  if (tf2_combat::weapon::last_rapid_fire_crit_check_time() != 0) {
    weapon->last_rapid_fire_crit_check_time() = state.last_rapid_fire_crit_check_time;
  }
  if (tf2_combat::weapon::crit_time() != 0) {
    weapon->crit_time() = state.crit_time;
  }
}

void restore_packet_weapon_states(Player* localplayer, const packet_weapon_states& states)
{
  for (int slot = 0; slot < Player::max_weapon_count; ++slot) {
    restore_packet_weapon_state(localplayer, slot, states[slot]);
  }
}

}

bool cl_process_packet_entities_hook(void* client_state, svc_packet_entities_message* message)
{
  PUPHOOK_HOOK_GUARD();
  if (cl_process_packet_entities_original == nullptr) {
    return false;
  }

  if (message == nullptr || message->is_delta || entity_list == nullptr) {
    return cl_process_packet_entities_original(client_state, message);
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    return cl_process_packet_entities_original(client_state, message);
  }

  packet_weapon_states states{};
  if (!read_packet_weapon_states(localplayer, states)) {
    return cl_process_packet_entities_original(client_state, message);
  }

  const bool result = cl_process_packet_entities_original(client_state, message);
  localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer != nullptr) {
    restore_packet_weapon_states(localplayer, states);
  }

  return result;
}
