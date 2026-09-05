#ifndef AUTOMATION_PROFILE_STALKER_API_HPP
#define AUTOMATION_PROFILE_STALKER_API_HPP

#include <string>

namespace automation::profile_stalker
{

#if defined(CATHOOK_WITH_PROFILE_STALKER) && CATHOOK_WITH_PROFILE_STALKER
void tick();
std::string status_line(int index);
int status_count();
#else
inline void tick() {}
inline std::string status_line(int) { return {}; }
inline int status_count() { return 0; }
#endif

}

#endif
