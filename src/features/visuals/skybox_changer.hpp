#ifndef PUPHOOK_SKYBOX_CHANGER_HPP
#define PUPHOOK_SKYBOX_CHANGER_HPP

namespace skybox_changer
{

[[nodiscard]] int option_count();
[[nodiscard]] const char* const* option_names();

void resolve_load_named_skys();
void update();
void invalidate();

}

#endif
