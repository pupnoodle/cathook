#ifndef BHOP_HPP
#define BHOP_HPP

struct user_cmd;

void bhop(user_cmd* user_cmd);
bool movement_post_prediction(user_cmd* user_cmd);
bool moonwalk_create_move(user_cmd* user_cmd);
bool moonwalk_applied_to_command(int command_number);
bool auto_edgebug_create_move(user_cmd* user_cmd);
void reset_movement_session_state();
#endif
