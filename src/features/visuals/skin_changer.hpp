#ifndef SKIN_CHANGER_HPP
#define SKIN_CHANGER_HPP

#include "features/menu/config.hpp"

#include <cstdint>
#include <vector>

namespace skin_changer {

using Skin = Visuals::SkinChanger::Skin;

using attribute_definition_lookup_fn = void* (*)(std::uintptr_t, int);
using attribute_list_set_runtime_value_fn = void (*)(void*, void*, float);

inline attribute_definition_lookup_fn attribute_definition_lookup = nullptr;
inline attribute_list_set_runtime_value_fn attribute_list_set_runtime_value = nullptr;

int key(int definition);
Skin get(int skin_key);
void set(int skin_key, const Skin& skin);
void get_kits(int skin_key, std::vector<const char*>& names, std::vector<int>& ids);
const char* weapon_label(int skin_key);

void apply();
void invalidate();

}

#endif
