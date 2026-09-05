/*
/^-----^\   data: 2026-08-22
V  o o  V  file: src/features/automation/mvm_queue/mvm_queue.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef AUTOMATION_MVM_QUEUE_HPP
#define AUTOMATION_MVM_QUEUE_HPP

namespace automation::mvm_queue
{

void tick();
int tour_count();
const char* tour_display_name(int index);
int bootcamp_mission_count();
const char* bootcamp_mission_name(int index);

}
#endif
