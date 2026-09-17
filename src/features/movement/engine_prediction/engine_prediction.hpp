/*
/^-----^\   data: 2026-08-03
V  o o  V  file: src/features/movement/engine_prediction/engine_prediction.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef ENGINE_PREDICTION_HPP
#define ENGINE_PREDICTION_HPP

struct user_cmd;

void start_engine_prediction(user_cmd* user_cmd);
void end_engine_prediction();
void reset_engine_prediction();
[[nodiscard]] bool engine_prediction_needed();
#endif
