#include "anti_cheat_compat.hpp"

#include "core/math/math.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

#include <cmath>
#include <deque>

namespace anti_cheat_compat {

namespace {

constexpr float math_epsilon = 1.0f / 16.0f;
constexpr float psilent_epsilon = 1.0f - math_epsilon;
constexpr float real_epsilon = 0.1f + math_epsilon;
constexpr float snap_size_epsilon = 10.0f - math_epsilon;
constexpr float snap_noise_epsilon = 0.5f + math_epsilon;
constexpr int max_bhop_chain = 9;

struct command_history {
  Vec3 angles{};
  bool attack1 = false;
  bool attack2 = false;
  bool sending_packet = true;
};

std::deque<command_history> history{};
int jump_chain = 0;
bool last_jump_valid = false;
bool last_command_jump = false;

bool enabled() {
  return config.misc.exploits.anti_cheat_compat;
}

float angle_fov(const Vec3& from, const Vec3& to) {
  Vec3 forward_from, forward_to;
  angle_vectors(from, &forward_from, nullptr, nullptr);
  angle_vectors(to, &forward_to, nullptr, nullptr);
  const float result = std::acos(std::clamp(dot(forward_from, forward_to), -1.0f, 1.0f)) / pideg;
  return std::isfinite(result) ? result : 0.0f;
}

void clamp_angles(Vec3& angles) {
  angles.x = std::clamp(angles.x, -89.0f, 89.0f);
  angles.y = azimuth_to_signed(angles.y);
  angles.z = 0.0f;
}

Vec3 lerp_angle(const Vec3& from, const Vec3& to, float fraction) {
  return Vec3{
    from.x + std::remainder(to.x - from.x, 360.0f) * fraction,
    azimuth_to_signed(from.y + std::remainder(to.y - from.y, 360.0f) * fraction),
    0.0f};
}

void mark_packet_sent(command_history& entry, bool sending) {
  entry.sending_packet = sending;
  if (sending) {
    tickbase::force_send_packet();
  }
}

}

void reset() {
  history.clear();
  jump_chain = 0;
  last_jump_valid = false;
  last_command_jump = false;
}

void enforce_settings() {
  if (!enabled()) {
    return;
  }

  if (config.aimbot.aim_mode == Aim::AimMode::PSILENT) {
    config.aimbot.aim_mode = Aim::AimMode::PLAIN;
  }
  config.crithack.force_crits = false;
  config.crithack.avoid_random = false;
  config.misc.movement.fast_accelerate = false;
  config.misc.exploits.doubletap = false;
  config.misc.exploits.warp = false;
  config.misc.exploits.fakelag = false;

  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer == nullptr || !localplayer->is_taunting()) {
    config.misc.exploits.anti_aim = false;
  }
}

void on_auto_jump(user_cmd* cmd, Player* localplayer) {
  if (!enabled() || cmd == nullptr || localplayer == nullptr) {
    jump_chain = 0;
    last_jump_valid = false;
    last_command_jump = false;
    return;
  }

  const bool jump_valid = localplayer->is_on_ground() && !localplayer->is_ducking();
  if (jump_valid) {
    if (!last_jump_valid && (cmd->buttons & IN_JUMP) != 0 && !last_command_jump) {
      ++jump_chain;
    } else {
      jump_chain = 0;
    }
    if (jump_chain > max_bhop_chain) {
      cmd->buttons &= ~IN_JUMP;
    }
  }
  last_jump_valid = jump_valid;
  last_command_jump = (cmd->buttons & IN_JUMP) != 0;
}

void on_create_move(user_cmd* cmd) {
  if (cmd == nullptr) {
    return;
  }
  if (!enabled()) {
    reset();
    return;
  }

  clamp_angles(cmd->view_angles);

  history.emplace_front(command_history{
    cmd->view_angles,
    (cmd->buttons & IN_ATTACK) != 0,
    (cmd->buttons & IN_ATTACK2) != 0,
    tickbase::should_send_packet()});
  if (history.size() > 5) {
    history.pop_back();
  }
  if (history.size() < 3) {
    return;
  }

  if (!history[0].attack1 && history[1].attack1 && !history[2].attack1) {
    cmd->buttons |= IN_ATTACK;
    history[0].attack1 = true;
  }
  if (!history[0].attack2 && history[1].attack2 && !history[2].attack2) {
    cmd->buttons |= IN_ATTACK2;
    history[0].attack2 = true;
  }

  if (!history[0].attack1 && !history[1].attack1 && !history[2].attack1) {
    return;
  }

  if (angle_fov(history[0].angles, history[1].angles) > psilent_epsilon &&
      angle_fov(history[0].angles, history[2].angles) < real_epsilon) {
    cmd->view_angles = lerp_angle(history[1].angles, history[0].angles, 0.5f);
    if (angle_fov(cmd->view_angles, history[2].angles) < real_epsilon) {
      cmd->view_angles = history[0].angles + Vec3{0.0f, real_epsilon * 2.0f, 0.0f};
    }
    history[0].angles = cmd->view_angles;
    mark_packet_sent(history[0], history[1].sending_packet);
  }

  if (history.size() == 5) {
    const float delta01 = angle_fov(history[0].angles, history[1].angles);
    const float delta12 = angle_fov(history[1].angles, history[2].angles);
    const float delta23 = angle_fov(history[2].angles, history[3].angles);
    const float delta34 = angle_fov(history[3].angles, history[4].angles);

    if ((delta12 > snap_size_epsilon && delta23 < snap_noise_epsilon && history[2].angles != history[3].angles ||
         delta23 > snap_size_epsilon && delta12 < snap_noise_epsilon && history[1].angles != history[2].angles) &&
        delta01 < snap_noise_epsilon && history[0].angles != history[1].angles &&
        delta34 < snap_noise_epsilon && history[3].angles != history[4].angles) {
      cmd->view_angles.y += snap_noise_epsilon * 2.0f;
      history[0].angles = cmd->view_angles;
      mark_packet_sent(history[0], history[1].sending_packet);
    }
  }
}

}
