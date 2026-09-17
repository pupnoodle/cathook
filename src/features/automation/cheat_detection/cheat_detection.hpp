#ifndef CHEAT_DETECTION_HPP
#define CHEAT_DETECTION_HPP

class GameEvent;

namespace cheat_detection {

void on_net_update_end();
void on_game_event(GameEvent* event);
void reset();

}

#endif
