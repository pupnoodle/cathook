#ifndef AUTOMATION_PROFILE_STALKER_HPP
#define AUTOMATION_PROFILE_STALKER_HPP
#include <string>

namespace automation::profile_stalker
{

struct presence_card
{
  std::string name{};
  std::string heading{};
  std::string summary{};
  std::string card{};
  bool in_server = false;
};

void tick();
int status_count();
presence_card status(int index);
std::string status_line(int index);

}
#endif
