#include "killstreak.hpp"

#include "core/player_resource.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/netvars.hpp"

#include <cstring>
#include <unordered_map>

namespace killstreak {

namespace {

constexpr int streak_slot_count = 4;

int current_streak = 0;
std::unordered_map<int, int> weapon_streaks{};

void apply_streaks(int local_index) {
  Entity* resource = cathook::core::player_resource::get_player_resource_entity();
  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;

  if (localplayer != nullptr) {
    static tf2_netvars::lazy_offset streaks_offset{"DT_TFPlayer", {"m_Shared", "m_nStreaks"}};
    if (streaks_offset > 0) {
      auto* streaks = reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(localplayer) + streaks_offset);
      for (int slot = 0; slot < streak_slot_count; ++slot) {
        streaks[slot] = current_streak;
      }
    }
  }

  if (resource != nullptr && local_index > 0) {
    static tf2_netvars::lazy_offset resource_streaks_offset{"DT_TFPlayerResource", {"m_iStreaks"}};
    if (resource_streaks_offset > 0 &&
        cathook::core::player_resource::index_in_range(local_index)) {
      auto* streaks = reinterpret_cast<int*>(
          reinterpret_cast<uintptr_t>(resource) + resource_streaks_offset +
          static_cast<uintptr_t>(local_index) * streak_slot_count * sizeof(int));
      for (int slot = 0; slot < streak_slot_count; ++slot) {
        streaks[slot] = current_streak;
      }
    }
  }
}

}

void reset() {
  current_streak = 0;
  weapon_streaks.clear();
}

void apply() {
  if (!config.misc.automation.killstreak || engine == nullptr || entity_list == nullptr ||
      !engine->is_in_game()) {
    return;
  }
  const int local_index = engine->get_localplayer_index();
  if (local_index <= 0) {
    return;
  }
  apply_streaks(local_index);
}

void on_game_event(GameEvent* event) {
  if (event == nullptr || engine == nullptr || entity_list == nullptr) {
    return;
  }

  const char* name = event->get_name();
  if (name == nullptr) {
    return;
  }

  const int local_index = engine->get_localplayer_index();

  if (std::strcmp(name, "player_spawn") == 0) {
    if (engine->get_player_index_from_id(event->get_int("userid")) == local_index) {
      reset();
      if (config.misc.automation.killstreak) {
        apply_streaks(local_index);
      }
    }
    return;
  }

  if (std::strcmp(name, "player_death") != 0) {
    return;
  }

  const int victim_index = engine->get_player_index_from_id(event->get_int("userid"));
  const int attacker_index = engine->get_player_index_from_id(event->get_int("attacker"));

  if (victim_index == local_index) {
    reset();
    return;
  }

  if (!config.misc.automation.killstreak || attacker_index != local_index ||
      attacker_index == victim_index) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    if (current_streak != 0) {
      reset();
    }
    return;
  }

  ++current_streak;
  const int weapon_id = event->get_int("weaponid");
  ++weapon_streaks[weapon_id];

  event->set_int("kill_streak_total", current_streak);
  event->set_int("kill_streak_wep", weapon_streaks[weapon_id]);

  apply_streaks(local_index);
}

}
