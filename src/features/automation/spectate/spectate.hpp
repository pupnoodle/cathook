#ifndef SPECTATE_HPP
#define SPECTATE_HPP

struct user_cmd;

namespace spectate {

void reset();
void on_net_update_start();
void on_net_update_end();
void on_create_move(user_cmd* cmd);
void set_target_userid(int userid);
int target_userid();
int target_index();

}

#endif
