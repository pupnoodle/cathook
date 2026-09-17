/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/team_objective_resource.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef TEAM_OBJECTIVE_RESOURCE_HPP
#define TEAM_OBJECTIVE_RESOURCE_HPP
#include "entity.hpp"
#include "core/print.hpp"
#define MAX_CONTROL_POINTS 8

class TeamObjectiveResource {
public:

  int get_mvm_wave_count(void) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_nMannVsMachineWaveCount"}};
    return offset > 0
      ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset)
      : 0;
  }

  int get_mvm_max_wave_count(void) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_nMannVsMachineMaxWaveCount"}};
    return offset > 0
      ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset)
      : 0;
  }

  bool is_mvm_between_waves(void) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_bMannVsMachineBetweenWaves"}};
    return offset > 0
      && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  int get_num_control_points(void) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_iNumControlPoints"}};
    return offset > 0
      ? *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset)
      : 0;
  }

  bool is_playing_mini_rounds(void) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_bPlayingMiniRounds"}};
    return offset > 0
      && *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset);
  }

  int get_owning_team(int index) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_iOwner"}};
    if (offset <= 0 || index < 0 || index >= MAX_CONTROL_POINTS) {
      return 0;
    }
    return reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + offset)[index];
  }

  bool is_in_mini_round(int index) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_bInMiniRound"}};
    if (offset <= 0 || index < 0 || index >= MAX_CONTROL_POINTS) {
      return false;
    }
    return reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset)[index];
  }

  bool is_locked(int index) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_bCPLocked"}};
    if (offset <= 0 || index < 0 || index >= MAX_CONTROL_POINTS) {
      return false;
    }
    return reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset)[index];
  }

  bool can_team_capture(int index, enum tf_team team) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_bTeamCanCap"}};
    int array_index = index + ((int)(team) * MAX_CONTROL_POINTS);
    if (offset <= 0 || array_index < 0 || array_index >= MAX_CONTROL_POINTS * 4) {
      return false;
    }
    return reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + offset)[array_index];
  }

  Vec3 get_origin(int index) {
    static tf2_netvars::lazy_offset offset{
      "DT_TFObjectiveResource", {"m_vCPPositions"}};
    if (offset <= 0 || index < 0 || index >= MAX_CONTROL_POINTS) {
      return Vec3{};
    }
    return reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(this) + offset)[index];
  }
};
#endif
