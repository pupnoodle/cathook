#ifndef KILLSTREAK_HPP
#define KILLSTREAK_HPP

class GameEvent;

namespace killstreak {

void on_game_event(GameEvent* event);
void apply();
void reset();

}

#endif
