#ifndef ANTI_CHEAT_COMPAT_HPP
#define ANTI_CHEAT_COMPAT_HPP

struct user_cmd;
class Player;

namespace anti_cheat_compat {

void reset();
void enforce_settings();
void on_auto_jump(user_cmd* cmd, Player* localplayer);
void on_create_move(user_cmd* cmd);

}

#endif
