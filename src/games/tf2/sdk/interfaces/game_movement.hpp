/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/game_movement.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef GAME_MOVEMENT_HPP
#define GAME_MOVEMENT_HPP

#include <cstddef>
#include <cstdint>

#include "core/types.hpp"
#include "games/tf2/sdk/combat_offsets.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/netvars.hpp"

class Player;

class MoveData {
public:
  bool			m_bFirstRunOfFunctions : 1;
  bool			m_bGameCodeMovedPlayer : 1;

  int	m_nPlayerHandle;

  int				m_nImpulseCommand;
  Vec3			m_vecViewAngles;
  Vec3			m_vecAbsViewAngles;
  int				m_nButtons;
  int				m_nOldButtons;
  float			m_flForwardMove;
  float			m_flOldForwardMove;
  float			m_flSideMove;
  float			m_flUpMove;

  float			m_flMaxSpeed;
  float			m_flClientMaxSpeed;

  Vec3			m_vecVelocity;
  Vec3			m_vecAngles;
  Vec3			m_vecOldAngles;

  float			m_outStepHeight;
  Vec3			m_outWishVel;
  Vec3			m_outJumpVel;

  Vec3			m_vecConstraintCenter;
  float			m_flConstraintRadius;
  float			m_flConstraintWidth;
  float			m_flConstraintSpeedFactor;

  Vec3			m_vecAbsOrigin;

  void			SetAbsOrigin( const Vec3 &vec );
  const Vec3	&GetAbsOrigin() const;
};

static_assert(offsetof(MoveData, m_vecAbsOrigin) == 0x9c);

inline void MoveData::SetAbsOrigin(const Vec3& vec) {
  m_vecAbsOrigin = vec;
}

inline const Vec3& MoveData::GetAbsOrigin() const {
  return m_vecAbsOrigin;
}

struct CViewVectors {
  Vec3 m_vView{};
  Vec3 m_vHullMin{};
  Vec3 m_vHullMax{};
  Vec3 m_vDuckHullMin{};
  Vec3 m_vDuckHullMax{};
  Vec3 m_vDuckView{};
  Vec3 m_vObsHullMin{};
  Vec3 m_vObsHullMax{};
  Vec3 m_vDeadViewHeight{};
};

constexpr float PLAYER_ORIGIN_COMPRESSION = 0.125f;

class GameMovement {
public:
  struct bind_guard {
    GameMovement* movement = nullptr;
    Player* old_player = nullptr;
    MoveData* old_move = nullptr;
    bool have_player = false;
    bool have_move = false;

    explicit bind_guard(GameMovement* owner) : movement(owner) {
      if (movement == nullptr) {
        return;
      }
      auto* bytes = reinterpret_cast<std::uint8_t*>(movement);
      const std::size_t player_off = tf2_combat::game_movement::player();
      const std::size_t move_off = tf2_combat::game_movement::move_data();
      if (player_off > 0 && player_off <= 32) {
        old_player = *reinterpret_cast<Player**>(bytes + player_off);
        have_player = true;
      }
      if (move_off > 0 && move_off <= 32) {
        old_move = *reinterpret_cast<MoveData**>(bytes + move_off);
        have_move = true;
      }
    }

    void bind(Player* player, MoveData* move) {
      if (movement == nullptr) {
        return;
      }
      auto* bytes = reinterpret_cast<std::uint8_t*>(movement);
      const std::size_t player_off = tf2_combat::game_movement::player();
      const std::size_t move_off = tf2_combat::game_movement::move_data();
      if (have_player) {
        *reinterpret_cast<Player**>(bytes + player_off) = player;
      }
      if (have_move) {
        *reinterpret_cast<MoveData**>(bytes + move_off) = move;
      }
    }

    ~bind_guard() {
      if (movement == nullptr) {
        return;
      }
      auto* bytes = reinterpret_cast<std::uint8_t*>(movement);
      const std::size_t player_off = tf2_combat::game_movement::player();
      const std::size_t move_off = tf2_combat::game_movement::move_data();
      if (have_player) {
        *reinterpret_cast<Player**>(bytes + player_off) = old_player;
      }
      if (have_move) {
        *reinterpret_cast<MoveData**>(bytes + move_off) = old_move;
      }
    }

    bind_guard(const bind_guard&) = delete;
    bind_guard& operator=(const bind_guard&) = delete;
  };

  CViewVectors* view_vectors() const {
    void* rules = tf2_netvars::game_rules_object();
    if (rules == nullptr) {
      return nullptr;
    }
    using get_view_vectors_fn = CViewVectors* (*)(void*);
    if (auto* const direct = reinterpret_cast<get_view_vectors_fn>(tf2_combat::get().get_view_vectors_fn)) {
      return direct(rules);
    }
    void** vtable = *reinterpret_cast<void***>(rules);
    const std::size_t slot = tf2_combat::game_movement::get_view_vectors();
    if (vtable == nullptr || slot == 0 || vtable[slot] == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<get_view_vectors_fn>(vtable[slot])(rules);
  }

  struct bounds_guard {
    GameMovement* movement = nullptr;
    Player* player = nullptr;
    CViewVectors saved{};
    bool active = false;

    bounds_guard(GameMovement* owner, Player* target) : movement(owner), player(target) {
      (void)movement;
      (void)player;
    }

    ~bounds_guard() {
      if (!active || movement == nullptr) {
        return;
      }
      CViewVectors* vectors = movement->view_vectors();
      if (vectors != nullptr) {
        *vectors = saved;
      }
    }

    bounds_guard(const bounds_guard&) = delete;
    bounds_guard& operator=(const bounds_guard&) = delete;
  };

  int check_stuck(Player* player, MoveData* move) {
    if (this == nullptr || player == nullptr || move == nullptr) {
      return 0;
    }
    using check_stuck_fn = int (*)(void*);
    check_stuck_fn fn = reinterpret_cast<check_stuck_fn>(tf2_combat::get().check_stuck_fn);
    if (fn == nullptr) {
      void** vtable = *reinterpret_cast<void***>(this);
      const std::size_t slot = tf2_combat::game_movement::check_stuck();
      if (vtable == nullptr || slot == 0 || vtable[slot] == nullptr) {
        return 0;
      }
      fn = reinterpret_cast<check_stuck_fn>(vtable[slot]);
    }
    bind_guard pointers(this);
    pointers.bind(player, move);
    bounds_guard bounds(this, player);
    return fn(this);
  }

  bool process_movement(Player* player, MoveData* move) {
    if (this == nullptr || player == nullptr || move == nullptr) {
      return false;
    }

    using process_movement_fn = void (*)(void*, Player*, MoveData*);
    process_movement_fn fn =
      reinterpret_cast<process_movement_fn>(tf2_combat::get().process_movement_fn);
    if (fn == nullptr) {
      void** vtable = *reinterpret_cast<void***>(this);
      const std::size_t slot = tf2_combat::game_movement::process_movement();
      if (vtable == nullptr || slot == 0 || vtable[slot] == nullptr) {
        return false;
      }
      fn = reinterpret_cast<process_movement_fn>(vtable[slot]);
    }

    bind_guard pointers(this);
    bounds_guard bounds(this, player);
    fn(this, player, move);
    return true;
  }
};

inline static GameMovement* game_movement;

#endif
