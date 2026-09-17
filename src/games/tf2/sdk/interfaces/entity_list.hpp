#ifndef PLAYER_LIST_HPP
#define PLAYER_LIST_HPP

#include "games/tf2/sdk/base_handle.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"

class Entity;
class Player;

class EntityList {
public:
  Entity* entity_from_index(unsigned int index) {
    if (this == nullptr) return nullptr;
    void** vtable = *(void ***)this;
    if (vtable == nullptr || vtable[3] == nullptr) return nullptr;

    const int max_entities = get_max_entities();
    if (max_entities <= 0 || index >= static_cast<unsigned int>(max_entities)) return nullptr;

    Entity* (*entity_from_index_fn)(void*, unsigned int) = (Entity* (*)(void*, unsigned int))vtable[3];

    return entity_from_index_fn(this, index);
  }

  Player* player_from_index(unsigned int index);

  Entity* entity_from_handle(int handle) {
    const auto raw_handle = static_cast<std::uint32_t>(handle);
    if (handle == 0 || handle == -1 || raw_handle == invalid_ehandle_index) {
      return nullptr;
    }

    const auto entity_index = static_cast<int>(raw_handle & ent_entry_mask);
    if (entity_index <= 0) {
      return nullptr;
    }

    if (this == nullptr) return nullptr;
    void** vtable = *(void ***)this;
    if (vtable == nullptr || vtable[3] == nullptr || vtable[4] == nullptr) return nullptr;

    auto max_entities = get_max_entities();
    if (max_entities <= 0 || entity_index >= max_entities) {
      return nullptr;
    }

    using entity_from_handle_fn = Entity* (*)(void*, const void*);
    const std::uint64_t handle_arg = raw_handle;
    return reinterpret_cast<entity_from_handle_fn>(vtable[4])(this, &handle_arg);
  }

  Entity* entity_from_handle(const CBaseHandle& handle) {
    return entity_from_handle(handle.ToInt());
  }

  Player* get_localplayer(void) {
    return engine == nullptr ? nullptr : this->player_from_index(engine->get_localplayer_index());
  }

  Entity* get_entity_from_id(int user_id) {
    return engine == nullptr ? nullptr : this->entity_from_index(engine->get_player_index_from_id(user_id));
  }

  Player* get_player_from_id(int user_id) {
    return engine == nullptr ? nullptr : this->player_from_index(engine->get_player_index_from_id(user_id));
  }

  Entity* get_game_rules_proxy(void);

  int get_max_entities(void) {
    if (this == nullptr) return 0;
    void** vtable = *(void ***)this;
    if (vtable == nullptr || vtable[8] == nullptr) return 0;

    int (*get_max_entities_fn)(void*) = (int (*)(void*))vtable[8];

    return get_max_entities_fn(this);
  }
};

static inline EntityList* entity_list;

#endif
