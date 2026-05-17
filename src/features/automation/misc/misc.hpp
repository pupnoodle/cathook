/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/automation/misc/misc.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef AUTOMATION_MISC_HPP
#define AUTOMATION_MISC_HPP

#include <string>
#include <utility>
#include <vector>

struct user_cmd;
struct bf_read;
class GameEvent;

namespace automation
{

class automation_controller
{
public:
  void on_create_move(user_cmd* user_cmd);
  void on_frame_stage_notify();
  void on_paint();
  void on_dispatch_user_message(int message_type, const bf_read* message_data);
  void on_game_event(GameEvent* event);

private:
  void apply_misc_convars();
  void run_auto_class_select();
  void run_anti_afk(user_cmd* user_cmd);
  void run_auto_report();
  void run_auto_vote_map(GameEvent* event);
  void run_autotaunt(GameEvent* event);
  void run_chatspam();
  void run_custom_announcer(GameEvent* event);
  void run_killsay(GameEvent* event);
  void process_killsay();
  void reset_custom_announcer();
  void play_custom_announcer_sound(const char* sound_name);
  void run_voice_command_spam();
  void run_noisemaker_spam();
  void run_micspam();
  void stop_micspam();
  void run_mvm_actions();
  void run_ping_reducer();
  void run_ban_message_bypass();
  void run_queueing();

  float next_class_action_time_ = 0.0f;
  float next_auto_report_time_ = 0.0f;
  float next_queue_action_time_ = 0.0f;
  float next_noisemaker_time_ = 0.0f;
  float next_voice_command_time_ = 0.0f;
  float next_micspam_on_time_ = 0.0f;
  float next_micspam_off_time_ = 0.0f;
  float next_chatspam_time_ = 0.0f;
  float next_autotaunt_time_ = 0.0f;
  float next_mvm_command_time_ = 0.0f;
  float next_mvm_buybot_time_ = 0.0f;
  float next_ping_reduce_time_ = 0.0f;
  float queue_loading_start_time_ = 0.0f;
  float last_active_input_time_ = 0.0f;
  bool was_in_game_ = false;
  bool cheats_bypass_applied_ = false;
  int original_sv_cheats_value_ = 0;
  bool vac_bypass_applied_ = false;
  bool original_allow_secure_servers_ = false;
  bool ping_reducer_saved_cmd_rate_ = false;
  bool micspam_recording_ = false;
  int original_cmd_rate_ = 0;

  enum class ban_bypass_phase
  {
    idle = 0,
    main_choke,
    pulse_choke,
    pulse_release,
    verify,
  };

  ban_bypass_phase ban_bypass_phase_ = ban_bypass_phase::idle;
  bool ban_bypass_convars_saved_ = false;
  float ban_bypass_original_host_timescale_ = 1.0f;
  int ban_bypass_original_fps_max_ = 0;
  float ban_bypass_phase_deadline_ = 0.0f;
  float ban_bypass_arm_deadline_ = 0.0f;
  int ban_bypass_pulses_remaining_ = 0;
  int ban_bypass_last_signon_ = 0;
  bool ban_bypass_choke_active_ = false;

  int mvm_buybot_step_ = 1;
  int autotaunt_previous_slot_ = -1;
  bool autotaunt_waiting_for_taunt_ = false;
  int chatspam_index_ = 0;
  int chatspam_last_index_ = -1;
  unsigned int announcer_killstreak_ = 0;
  unsigned int announcer_kill_combo_ = 0;
  unsigned int announcer_headshot_combo_ = 0;
  float announcer_last_kill_time_ = -100000.0f;
  float announcer_last_headshot_time_ = -100000.0f;
  std::vector<std::pair<float, std::string>> pending_killsays_{};
  std::vector<unsigned long> reported_account_ids_{};
};

automation_controller& controller();
bool reload_casual_criteria();
bool request_casual_queue();
bool cancel_casual_queue();
bool abandon_current_match();

} // namespace automation

#endif
