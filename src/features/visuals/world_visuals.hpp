#ifndef CATHOOK_WORLD_VISUALS_HPP
#define CATHOOK_WORLD_VISUALS_HPP

struct funchook;
using funchook_t = struct funchook;

namespace world_visuals
{

void on_render_start();
void on_shutdown();

using particle_create_fn = void* (*)(void*, const char*, int, const char*);
extern particle_create_fn particle_create_original;

void resolve_particle_hook();
bool prepare_particle_hook(funchook_t* hooks);

}

#endif
