/*
data: 2026-08-10
file: src/features/visuals/radar/radar.hpp
author: HappyKuro
*/
#ifndef RADAR_HPP
#define RADAR_HPP

namespace radar
{

// Draws the radar overlay in its own mono-framed window. Call from inside the
// overlay canvas Begin/End pair, alongside draw_game_indicators().
void draw_radar();

}

#endif
