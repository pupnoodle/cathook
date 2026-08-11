/*
data: 2026-08-10
file: src/features/automation/telemetry_blocker/telemetry_blocker.hpp
author: HappyKuro
*/

#ifndef TELEMETRY_BLOCKER_HPP
#define TELEMETRY_BLOCKER_HPP

namespace telemetry_blocker
{

// Applies or restores the block to match config.misc.telemetry_blocker. Cheap to call
// every frame - it only touches convars when the toggle has actually changed.
void update();

// Puts every convar back the way it was found. Called on detach so the game is not left
// with the block still applied after cathook is gone.
void restore();

// Resolves the CSteamWorksGameStatsUploader entry points in client.so. Safe to call when
// they are missing - every hook falls through to the original, and the convar half of the
// block still works on its own.
void resolve_hooks();

// Drops the resolved pointers, for the re-attach path.
void clear_hook_pointers();

} // namespace telemetry_blocker

// Hook entry points and their originals. Defined in telemetry_blocker.cpp, which the unity
// build pulls in before cathook.cpp wires them into funchook.
extern void* (*steamworks_gamestats_get_interface_original)(void*);
extern long (*steamworks_gamestats_write_perf_data_original)(void*, void*);
extern long (*steamworks_gamestats_submit_row_original)(void*, void*, char);
extern void (*steamworks_gamestats_drain_rows_original)(void*);
extern long (*steamworks_gamestats_end_session_original)(void*);
extern void (*steamworks_gamestats_reset_session_original)(void*);

void* steamworks_gamestats_get_interface_hook(void* self);
long steamworks_gamestats_write_perf_data_hook(void* self, void* data);
long steamworks_gamestats_submit_row_hook(void* self, void* row, char send_now);
void steamworks_gamestats_drain_rows_hook(void* self);
long steamworks_gamestats_end_session_hook(void* self);

namespace telemetry_blocker
{

} // namespace telemetry_blocker

#endif
