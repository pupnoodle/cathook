#pragma once

#include <SDL2/SDL_events.h>
#include <SDL2/SDL_video.h>
#include <vulkan/vulkan.h>

struct ImGui_ImplVulkan_InitInfo;

bool mono_ui_initialize_opengl(SDL_Window *window);
bool mono_ui_initialize_vulkan(SDL_Window *window, const ImGui_ImplVulkan_InitInfo &init_info);
bool mono_ui_backend_ready();
void mono_ui_shutdown(bool release_graphics_resources);
bool mono_ui_initialized();
void mono_ui_apply_theme();
bool mono_ui_should_block_input();
bool mono_ui_build_frame();
void mono_ui_lock();
void mono_ui_unlock();
void mono_ui_process_event(const SDL_Event *event);

bool mono_ui_vulkan_frame_pending();
bool mono_ui_vulkan_render(VkCommandBuffer command_buffer);
void mono_ui_vulkan_frame_consumed();
bool mono_ui_vulkan_prepare_target(VkRenderPass render_pass, unsigned int min_image_count, unsigned int image_count);
void mono_ui_vulkan_mark_device_lost();
void mono_ui_vulkan_device_idle();
void mono_ui_vulkan_resources_shutdown(bool release_graphics_resources);

bool mono_ui_begin_frame();
void mono_ui_end_frame();
void mono_ui_release_frame();
