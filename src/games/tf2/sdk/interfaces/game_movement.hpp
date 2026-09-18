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
constexpr std::size_t k_game_movement_player = 0x8;
constexpr std::size_t k_game_movement_mv = 0x10;
constexpr std::size_t k_game_movement_tf_player = 0x16c8;

class GameMovement {
public:
  void bind(Player* player, MoveData* move) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(this);
    *reinterpret_cast<Player**>(bytes + k_game_movement_player) = player;
    *reinterpret_cast<MoveData**>(bytes + k_game_movement_mv) = move;
    *reinterpret_cast<Player**>(bytes + k_game_movement_tf_player) = player;
  }

  CViewVectors* view_vectors() const {
    void* rules = tf2_netvars::game_rules_object();
    if (rules == nullptr) {
      return nullptr;
    }
    void** vtable = *reinterpret_cast<void***>(rules);
    auto fn = reinterpret_cast<CViewVectors* (*)(void*)>(vtable[32]);
    return fn(rules);
  }

  bool set_bounds(Player* player) {
    if (player == nullptr || engine == nullptr ||
        player->get_index() == engine->get_localplayer_index()) {
      return false;
    }
    CViewVectors* vectors = view_vectors();
    if (vectors == nullptr) {
      return false;
    }
    vectors->m_vHullMin = Vec3{-24.0f + PLAYER_ORIGIN_COMPRESSION, -24.0f + PLAYER_ORIGIN_COMPRESSION,
                               PLAYER_ORIGIN_COMPRESSION};
    vectors->m_vHullMax = Vec3{24.0f - PLAYER_ORIGIN_COMPRESSION, 24.0f - PLAYER_ORIGIN_COMPRESSION,
                               82.0f - PLAYER_ORIGIN_COMPRESSION};
    vectors->m_vDuckHullMin = Vec3{-24.0f + PLAYER_ORIGIN_COMPRESSION, -24.0f + PLAYER_ORIGIN_COMPRESSION,
                                   PLAYER_ORIGIN_COMPRESSION};
    vectors->m_vDuckHullMax = Vec3{24.0f - PLAYER_ORIGIN_COMPRESSION, 24.0f - PLAYER_ORIGIN_COMPRESSION,
                                   62.0f - PLAYER_ORIGIN_COMPRESSION};
    return true;
  }

  void restore_bounds(Player* player) {
    if (player == nullptr || engine == nullptr ||
        player->get_index() == engine->get_localplayer_index()) {
      return;
    }
    CViewVectors* vectors = view_vectors();
    if (vectors == nullptr) {
      return;
    }
    vectors->m_vHullMin = Vec3{-24.0f, -24.0f, 0.0f};
    vectors->m_vHullMax = Vec3{24.0f, 24.0f, 82.0f};
    vectors->m_vDuckHullMin = Vec3{-24.0f, -24.0f, 0.0f};
    vectors->m_vDuckHullMax = Vec3{24.0f, 24.0f, 62.0f};
  }

  int check_stuck(Player* player, MoveData* move) {
    if (this == nullptr || player == nullptr || move == nullptr) {
      return 0;
    }
    void** vtable = *reinterpret_cast<void***>(this);
    if (vtable == nullptr || vtable[40] == nullptr) {
      return 0;
    }
    bind(player, move);
    const bool changed = set_bounds(player);
    auto fn = reinterpret_cast<int (*)(void*)>(vtable[40]);
    const int stuck = fn(this);
    if (changed) {
      restore_bounds(player);
    }
    return stuck;
  }

  bool process_movement(Player* player, MoveData* move) {
    if (this == nullptr || player == nullptr || move == nullptr) {
      return false;
    }

    void** vtable = *(void***)this;
    if (vtable == nullptr) {
      return false;
    }

    void (*process_movement_fn)(void*, Player*, MoveData*) = (void (*)(void*, Player*, MoveData*))vtable[2];
    if (process_movement_fn == nullptr) {
      return false;
    }

    bind(player, move);
    const bool changed = set_bounds(player);
    process_movement_fn(this, player, move);
    if (changed) {
      restore_bounds(player);
    }
    return true;
  }
};

inline static GameMovement* game_movement;

#endif
