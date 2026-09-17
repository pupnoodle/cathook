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
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
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
  void on_menu_tick();
  void on_dispatch_user_message(int message_type, const bf_read* message_data);
  void on_game_event(GameEvent* event);

  [[nodiscard]] bool is_setup_time() const;
  [[nodiscard]] bool is_buybot_busy() const;
  void mvm_fix();
  void run_chat_commands(std::string_view message, std::uint32_t account_id, bool party_chat);

private:
  void reset_buybot();
  void apply_misc_convars();
  void run_auto_class_select();
  void run_anti_afk(user_cmd* user_cmd);
  void run_auto_report();
  void run_auto_vote_map(GameEvent* event);
  void run_autotaunt(GameEvent* event);
  void run_chatspam();
  void run_startup_sound();
  void run_custom_announcer(GameEvent* event);
  void run_killsay(GameEvent* event);
  void process_killsay();
  void reset_custom_announcer();
  void play_custom_announcer_sound(const char* sound_name);
  void run_voice_command_spam();
  void run_noisemaker_spam();
  void run_micspam();
  void stop_micspam();
  bool prepare_micspam_voice_file();
  void run_mvm_actions(user_cmd* user_cmd = nullptr);
  void run_ping_reducer();
  void run_queueing();
  void run_boost_queueing();
  void run_auto_vote_message(int message_type, const bf_read* message_data);
  void run_auto_vote();
  void run_autoparty();
  void refresh_party_hosts();

  struct pending_vote
  {
    int team = 0;
    int caller = 0;
    int target = 0;
    float start_time = 0.0f;
    float vote_time = 0.0f;
    bool voted = false;
  };

  std::unordered_map<int, pending_vote> pending_votes_{};
  bool vote_active_ = false;
  float vote_call_cooldown_expire_ = 0.0f;
  float next_auto_vote_kick_time_ = 0.0f;

  float next_class_action_time_ = 0.0f;
  float next_queue_action_time_ = 0.0f;
  float next_noisemaker_time_ = 0.0f;
  float next_voice_command_time_ = 0.0f;
  float next_micspam_on_time_ = 0.0f;
  float next_micspam_off_time_ = 0.0f;
  float next_chatspam_time_ = 0.0f;
  float next_autotaunt_time_ = 0.0f;
  float next_mvm_command_time_ = 0.0f;
  float next_mvm_class_retry_time_ = 0.0f;
  float next_mvm_buybot_time_ = 0.0f;
  float next_ping_reduce_time_ = 0.0f;
  float queue_loading_start_time_ = 0.0f;
  float boost_match_start_time_ = 0.0f;
  float last_active_input_time_ = 0.0f;
  bool was_in_game_ = false;
  bool startup_sound_played_ = false;
  bool warmup_active_ = false;
  bool cheats_bypass_applied_ = false;
  int original_sv_cheats_value_ = 0;
  bool vac_bypass_applied_ = false;
  bool original_allow_secure_servers_ = false;
  bool ping_reducer_saved_cmd_rate_ = false;
  bool ping_reducer_server_rate_sent_ = false;
  bool micspam_recording_ = false;
  bool micspam_voice_inputfromfile_active_ = false;
  bool boost_leave_requested_ = false;
  bool mvm_auto_abandoned_ = false;
  int original_cmd_rate_ = 0;
  int ping_reducer_last_server_rate_ = 0;
  int mvm_buybot_step_ = 1;
  int mvm_buybot_upgrade_slot_step_ = 0;
  int mvm_buybot_upgrade_index_ = 0;
  int mvm_buybot_priority_step_ = 0;
  bool mvm_buybot_cash_limit_reached_ = false;
  bool mvm_buybot_finished_upgrades_ = false;
  bool mvm_buybot_navigating_ = false;
  float mvm_buybot_stall_time_ = 0.0f;
  float next_mvm_scout_equip_time_ = 0.0f;
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
  std::vector<std::uint32_t> autoparty_hosts_{};
  std::string autoparty_hosts_source_{};
  bool autoparty_from_ipc_ = false;
  float next_autoparty_time_ = 0.0f;
};

automation_controller& controller();
bool reload_casual_criteria();
bool request_casual_queue();
bool cancel_casual_queue();
bool abandon_current_match();
bool promote_party_leader(std::uint32_t account_id);
void mvm_quit();
void shutdown();

}
#endif
