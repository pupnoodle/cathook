/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/sdl.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_syswm.h>
#include <GL/glew.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"

#include "core/ui/mono_ui.hpp"
#include "mono/mono.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "core/print.hpp"
#include "features/menu/menu.hpp"
#include "features/menu/indicators.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/visuals/esp/esp.hpp"
#include "features/visuals/radar/radar.hpp"
#include "features/visuals/hitmarker.hpp"
#include "features/visuals/spectator_list.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/automation/nographics/nographics.hpp"
#undef Status

bool (*poll_event_original)(SDL_Event*) = NULL;
void (*swap_window_original)(SDL_Window*) = NULL;
Uint32 (*get_window_flags_original)(SDL_Window*) = NULL;
SDL_bool (*get_window_WM_info_original)(SDL_Window* window, SDL_SysWMinfo* info) = NULL;
void (*get_window_size_original)(SDL_Window* window, int* w, int* h) = NULL;
void** poll_event_target = nullptr;
void** swap_window_target = nullptr;
void** get_window_flags_target = nullptr;
void** get_window_WM_info_target = nullptr;
void** get_window_size_target = nullptr;
std::atomic_bool sdl_hooks_installed = false;
std::atomic_bool sdl_hooks_uninstalling = false;
std::atomic_int sdl_active_hook_calls = 0;
enum class mono_backend_kind : std::uint8_t {
  none,
  opengl,
  vulkan,
};

static mono::runtime mono_runtime{};
static mono_backend_kind mono_backend = mono_backend_kind::none;
static bool mono_skip_opengl_backend_shutdown = false;
static SDL_GLContext mono_opengl_context = nullptr;
static std::atomic_bool mono_overlay_disabled = false;
static bool mono_frame_ready = false;
static std::atomic_bool mono_vulkan_device_lost = false;
static ImGui_ImplVulkan_InitInfo mono_vulkan_init_info{};
static std::recursive_mutex mono_ui_mutex{};
static thread_local bool mono_ui_frame_lock_held = false;
static std::array<mono::key_state, SDL_NUM_SCANCODES> mono_keyboard_state{};
static std::array<mono::key_state, 8> mono_mouse_state{};
static std::chrono::steady_clock::time_point mono_last_imgui_frame_time{};

struct sdl_hook_call_guard
{
  sdl_hook_call_guard()
  {
    sdl_active_hook_calls.fetch_add(1, std::memory_order_acq_rel);
  }

  ~sdl_hook_call_guard()
  {
    sdl_active_hook_calls.fetch_sub(1, std::memory_order_acq_rel);
  }
};

bool begin_sdl_hook_uninstall()
{
  sdl_hooks_uninstalling.store(true, std::memory_order_release);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (sdl_active_hook_calls.load(std::memory_order_acquire) > 0) {
    if (std::chrono::steady_clock::now() >= deadline) {
      sdl_hooks_uninstalling.store(false, std::memory_order_release);
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

void finish_sdl_hook_uninstall()
{
  sdl_hooks_installed.store(false, std::memory_order_release);
  sdl_hooks_uninstalling.store(false, std::memory_order_release);
}

static void update_imgui_sdl_display_size(SDL_Window* window) {
  if (window == nullptr || engine == nullptr) {
    return;
  }

  const auto engine_size = engine->get_screen_size();
  const auto display_width = engine_size.x;
  const auto display_height = engine_size.y;

  if (display_width <= 0 || display_height <= 0) {
    return;
  }

  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(display_width), static_cast<float>(display_height));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

static void update_imgui_frame_timing() {
  const auto now = std::chrono::steady_clock::now();
  auto& io = ImGui::GetIO();
  if (mono_last_imgui_frame_time.time_since_epoch().count() == 0) {
    io.DeltaTime = 1.0f / 60.0f;
  } else {
    io.DeltaTime = std::clamp(
      std::chrono::duration<float>(now - mono_last_imgui_frame_time).count(),
      1.0f / 1000.0f,
      0.1f);
  }
  mono_last_imgui_frame_time = now;
}

static mono::key_state mono_input_state(const int key) {
  if (key >= 0 && key < static_cast<int>(mono_keyboard_state.size())) {
    return mono_keyboard_state[static_cast<size_t>(key)];
  }
  if (key < 0 && -key < static_cast<int>(mono_mouse_state.size())) {
    return mono_mouse_state[static_cast<size_t>(-key)];
  }
  return {};
}

static std::string mono_input_name(const int key) {
  if (key >= 0 && key < SDL_NUM_SCANCODES) {
    const char *const name = SDL_GetScancodeName(static_cast<SDL_Scancode>(key));
    return name != nullptr && name[0] != '\0' ? name : "Unknown";
  }
  if (key < 0) {
    switch (-key) {
    case SDL_BUTTON_LEFT: return "Mouse Left";
    case SDL_BUTTON_RIGHT: return "Mouse Right";
    case SDL_BUTTON_MIDDLE: return "Mouse Middle";
    case SDL_BUTTON_X1: return "Mouse X1";
    case SDL_BUTTON_X2: return "Mouse X2";
    default: return "Mouse Button " + std::to_string(-key);
    }
  }
  return "Unknown";
}

static void reset_mono_input_edges() {
  for (auto &state : mono_keyboard_state) {
    state.pressed = false;
    state.released = false;
  }
  for (auto &state : mono_mouse_state) {
    state.pressed = false;
    state.released = false;
  }
}

void mono_ui_process_event(const SDL_Event *const event) {
  if (event == nullptr) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);

  const int scancode = static_cast<int>(event->key.keysym.scancode);
  if (event->type == SDL_KEYDOWN && scancode >= SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES) {
    auto &state = mono_keyboard_state[static_cast<size_t>(scancode)];
    state.pressed = event->key.repeat == 0;
    state.held = true;
  } else if (event->type == SDL_KEYUP && scancode >= SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES) {
    auto &state = mono_keyboard_state[static_cast<size_t>(scancode)];
    state.held = false;
    state.released = true;
  } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button < mono_mouse_state.size()) {
    auto &state = mono_mouse_state[event->button.button];
    state.pressed = true;
    state.held = true;
  } else if (event->type == SDL_MOUSEBUTTONUP && event->button.button < mono_mouse_state.size()) {
    auto &state = mono_mouse_state[event->button.button];
    state.held = false;
    state.released = true;
  }

  if (mono_runtime.initialized()) {
    ImGui_ImplSDL2_ProcessEvent(event);
  }
}

static void configure_mono_input() {
  mono::set_input_adapter({
    .state = [](const int key) { return mono_input_state(key); },
    .name = [](const int key) { return mono_input_name(key); },
    .first_key = 0,
    .last_key = SDL_NUM_SCANCODES - 1,
    .first_mouse_key = -SDL_BUTTON_LEFT,
    .last_mouse_key = -SDL_BUTTON_X2,
    .escape_key = SDL_SCANCODE_ESCAPE,
    .opening_mouse_key = -SDL_BUTTON_LEFT
  });
}

static bool initialize_mono_runtime(SDL_Window *const window, mono::backend backend, const mono_backend_kind kind) {
  if (window == nullptr) {
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);

  if (mono_runtime.initialized()) {
    return mono_backend == kind;
  }

  configure_mono_input();
  const bool initialized = mono_runtime.initialize(std::move(backend), [](ImGuiIO &io) {
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    cat_menu::ensure_fonts();
    return io.FontDefault != nullptr;
  });
  if (initialized) {
    mono_backend = kind;
    set_imgui_theme();
  }
  return initialized;
}

bool mono_ui_initialize_opengl(SDL_Window *const window) {
  const SDL_GLContext current_context = SDL_GL_GetCurrentContext();
  if (window == nullptr || current_context == nullptr || mono_overlay_disabled.load(std::memory_order_acquire)) {
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
    if (mono_runtime.initialized()) {
      return mono_backend == mono_backend_kind::opengl && current_context == mono_opengl_context;
    }
  }

  const GLenum err = glewInit();
  if (err != GLEW_OK) {
    print("Failed to initialize GLEW in TF2's GL context: %s\n", glewGetErrorString(err));
    return false;
  }

  mono_opengl_context = current_context;
  if (!initialize_mono_runtime(window, {
    .initialize = [window, current_context]() {
      if (!ImGui_ImplSDL2_InitForOpenGL(window, current_context)) {
        return false;
      }

      if (!ImGui_ImplOpenGL3_Init()) {
        ImGui_ImplSDL2_Shutdown();
        return false;
      }
      return true;
    },
    .shutdown = []() {
      if (!mono_skip_opengl_backend_shutdown) {
        ImGui_ImplOpenGL3_Shutdown();
      }
      if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL2_Shutdown();
      }
    },
    .new_frame = [window]() {
      ImGui_ImplSDL2_NewFrame();
      update_imgui_sdl_display_size(window);
      update_imgui_frame_timing();
    },
    .render = [](ImDrawData *const) {}
  }, mono_backend_kind::opengl)) {
    mono_opengl_context = nullptr;
    return false;
  }

  print("[renderer] single-context OpenGL overlay initialized (context=%p)\n", current_context);
  return true;
}

bool mono_ui_initialize_vulkan(SDL_Window *const window, const ImGui_ImplVulkan_InitInfo &init_info) {
  if (window == nullptr || mono_overlay_disabled.load(std::memory_order_acquire)) {
    return false;
  }

  {
    std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
    if (mono_runtime.initialized()) {
      return mono_backend == mono_backend_kind::vulkan;
    }
  }

  mono_vulkan_init_info = init_info;
  if (!initialize_mono_runtime(window, {
    .initialize = [window]() {
      if (!ImGui_ImplSDL2_InitForVulkan(window)) {
        return false;
      }

      if (!ImGui_ImplVulkan_Init(&mono_vulkan_init_info)) {
        ImGui_ImplSDL2_Shutdown();
        return false;
      }
      return true;
    },
    .shutdown = []() {
      if (!mono_vulkan_device_lost && ImGui::GetCurrentContext() != nullptr &&
          ImGui::GetIO().BackendRendererUserData != nullptr) {
        ImGui_ImplVulkan_Shutdown();
      }
      if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL2_Shutdown();
      }
    },
    .new_frame = [window]() {
      ImGui_ImplSDL2_NewFrame();
      update_imgui_sdl_display_size(window);
      update_imgui_frame_timing();
    },
    .render = [](ImDrawData *const) {}
  }, mono_backend_kind::vulkan)) {
    return false;
  }

  print("[renderer] Vulkan overlay initialized\n");
  return true;
}

bool mono_ui_backend_ready() {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  return mono_runtime.initialized() && mono_backend != mono_backend_kind::none;
}

bool mono_ui_vulkan_frame_pending() {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  return mono_backend == mono_backend_kind::vulkan && mono_runtime.initialized() && mono_frame_ready;
}

bool mono_ui_vulkan_render(const VkCommandBuffer command_buffer) {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  if (mono_backend != mono_backend_kind::vulkan || !mono_runtime.initialized() ||
      !mono_frame_ready || command_buffer == VK_NULL_HANDLE) {
    return false;
  }

  ImDrawData *const draw_data = ImGui::GetDrawData();
  if (draw_data != nullptr && draw_data->Valid) {
    ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer);
  }
  return true;
}

void mono_ui_vulkan_frame_consumed() {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  mono_frame_ready = false;
}

bool mono_ui_vulkan_prepare_target(const VkRenderPass render_pass, const unsigned int min_image_count,
    const unsigned int image_count) {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  if (mono_backend != mono_backend_kind::vulkan || !mono_runtime.initialized()) {
    return false;
  }

  if (mono_vulkan_init_info.RenderPass == render_pass && mono_vulkan_init_info.ImageCount == image_count) {
    return true;
  }
  if (render_pass == VK_NULL_HANDLE || image_count < 2) {
    return false;
  }

  mono_vulkan_init_info.RenderPass = render_pass;
  mono_vulkan_init_info.MinImageCount = std::max(2u, std::min(min_image_count, image_count));
  mono_vulkan_init_info.ImageCount = image_count;
  ImGui_ImplVulkan_Shutdown();
  if (!ImGui_ImplVulkan_Init(&mono_vulkan_init_info)) {
    return false;
  }
  for (ImTextureData *texture : ImGui::GetPlatformIO().Textures) {
    if (texture != nullptr && texture->Status == ImTextureStatus_OK) {
      texture->SetStatus(ImTextureStatus_WantDestroy);
    }
  }
  return true;
}

void mono_ui_vulkan_mark_device_lost() {
  mono_vulkan_device_lost = true;
}

void mono_ui_shutdown(const bool release_graphics_resources) {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);

  if (!mono_runtime.initialized()) {
    reset_mono_input_edges();
    return;
  }

  const bool using_opengl = mono_backend == mono_backend_kind::opengl;
  const bool using_vulkan = mono_backend == mono_backend_kind::vulkan;
  const SDL_GLContext current_context = using_opengl ? SDL_GL_GetCurrentContext() : nullptr;
  mono_skip_opengl_backend_shutdown = using_opengl &&
      (!release_graphics_resources || current_context == nullptr || current_context != mono_opengl_context);
  if (mono_skip_opengl_backend_shutdown) {
    print("[renderer] abandoning OpenGL overlay resources without driver cleanup (context no longer safe)\n");
  }

  if (using_vulkan && release_graphics_resources && !mono_vulkan_device_lost) {
    mono_ui_vulkan_device_idle();
  }

  const bool graphics_context_unavailable = !release_graphics_resources ||
      (using_opengl && mono_skip_opengl_backend_shutdown) ||
      (using_vulkan && mono_vulkan_device_lost);
  if (graphics_context_unavailable) {
    mono_runtime.abandon();
    ImGui::SetCurrentContext(nullptr);
  } else {
    mono_runtime.shutdown();
  }

  if (using_vulkan) {
    mono_ui_vulkan_resources_shutdown(release_graphics_resources);
  }

  mono_skip_opengl_backend_shutdown = false;
  mono_backend = mono_backend_kind::none;
  mono_opengl_context = nullptr;
  mono_overlay_disabled.store(false, std::memory_order_release);
  mono_frame_ready = false;
  mono_vulkan_device_lost = false;
  mono_vulkan_init_info = {};
  mono_last_imgui_frame_time = {};
  reset_mono_input_edges();
}

bool mono_ui_initialized() {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  return mono_runtime.initialized();
}

void mono_ui_apply_theme() {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  if (mono_runtime.initialized()) {
    set_imgui_theme();
  }
}

bool mono_ui_should_block_input() {
  std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
  return mono_runtime.initialized() && ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

void mono_ui_lock() {
  mono_ui_mutex.lock();
}

void mono_ui_unlock() {
  mono_ui_mutex.unlock();
}

bool mono_ui_begin_frame() {
  if (mono_ui_frame_lock_held) {
    return false;
  }

  mono_ui_mutex.lock();
  if (!mono_runtime.initialized()) {
    mono_ui_mutex.unlock();
    return false;
  }

  mono_ui_frame_lock_held = true;
  const ImVec4 accent = cat_menu::menu_accent();
  mono_runtime.begin_frame({accent.x, accent.y, accent.z, accent.w});
  return true;
}

void mono_ui_end_frame() {
  if (!mono_ui_frame_lock_held) {
    return;
  }

  if (mono_runtime.initialized()) {
    mono_runtime.end_frame();
  }
  reset_mono_input_edges();
}

void mono_ui_release_frame() {
  if (!mono_ui_frame_lock_held) {
    return;
  }

  mono_ui_frame_lock_held = false;
  mono_ui_mutex.unlock();
}

int SDLCALL event_filter(void* userdata, SDL_Event* event) {
  sdl_hook_call_guard guard{};
  if (sdl_hooks_uninstalling.load(std::memory_order_acquire)) {
    return 1;
  }

  if (event == nullptr) {
    return 1;
  }

  get_input(event);

  if (menu_focused == false && player_manager_window_open == false) return 1;

  const bool imgui_ready = sdl_window != nullptr && mono_ui_initialized();
  if (imgui_ready) {
    mono_ui_process_event(event);
  }

  if (!imgui_ready) {
    return 1;
  }

  if (event->type == SDL_KEYUP) {
    return 1;
  }

  if (imgui_ready && mono_ui_should_block_input()) return 0;

  if (event->type == SDL_KEYDOWN) {
    SDL_KeyboardEvent* key = &event->key;
    SDL_Keycode sym = key->keysym.sym;
    if (sym == SDLK_w || sym == SDLK_a || sym == SDLK_s || sym == SDLK_d || sym == SDLK_INSERT ||
	sym == SDLK_SPACE || sym == SDLK_LCTRL) {
      return 1;
    }
  }

  return 0;
}

bool poll_event_hook(SDL_Event* event) {
  CATHOOK_HOOK_GUARD();
  if (sdl_hooks_uninstalling.load(std::memory_order_acquire)) {
    return poll_event_original != nullptr ? poll_event_original(event) : false;
  }

  sdl_hook_call_guard guard{};

  if (poll_event_original == nullptr) {
    return false;
  }

  const bool ret = poll_event_original(event);
  if (!ret || event == nullptr) {
    return ret;
  }

  if (sdl_window != nullptr && mono_ui_initialized() && !menu_focused && !player_manager_window_open) {
    mono_ui_process_event(event);
  }

  get_input(event);

  return ret;
}

bool mono_ui_build_frame() {
  if (engine == nullptr || nographics::should_skip_rendering_hooks() ||
      mono_overlay_disabled.load(std::memory_order_acquire) ||
      !mono_ui_backend_ready() || !mono_ui_begin_frame()) {
    return false;
  }

  cat_menu::ensure_fonts();
  warmup_bind_targets();
  if (ImGui::IsKeyPressed(ImGuiKey_Insert, false) || ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
    menu_focused = !menu_focused;
    player_manager_window_open = menu_focused;
    cat_bind::set_menu_open(menu_focused || player_manager_window_open);
    if (surface != nullptr) {
      surface->set_cursor_visible(menu_focused || player_manager_window_open);
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
    player_manager_window_open = !player_manager_window_open;
    cat_bind::set_menu_open(menu_focused || player_manager_window_open);
    if (surface != nullptr) {
      surface->set_cursor_visible(menu_focused || player_manager_window_open);
    }
  }

  ImGui::SetNextWindowPos({0.0f, 0.0f}, ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  constexpr ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin("##cathook_overlay_canvas", nullptr, overlay_flags);
  draw_aimbot_fov_imgui();
  draw_thirdperson_crosshair_imgui();
  draw_players_imgui();
  draw_backtrack_visualizer_imgui();
  hitmarker::draw_imgui();
  navbot::controller().draw_imgui();
  draw_watermark();

  if (menu_focused || player_manager_window_open) {
    draw_menu();
  }

  draw_game_indicators();
  radar::draw_radar();
  ImGui::End();

  mono_ui_end_frame();
  mono_frame_ready = true;
  mono_ui_release_frame();
  return true;
}

void swap_window_hook(SDL_Window* window) {
  CATHOOK_HOOK_GUARD();
  void (*original)(SDL_Window*) = swap_window_original;

  if (original == nullptr) {
    return;
  }

  if (sdl_hooks_uninstalling.load(std::memory_order_acquire)) {
    original(window);
    return;
  }

  sdl_window = window;

  {
    sdl_hook_call_guard guard{};
    bool render_overlay = !nographics::should_skip_rendering_hooks() && engine != nullptr &&
        !mono_overlay_disabled.load(std::memory_order_acquire);
    if (render_overlay && mono_backend == mono_backend_kind::vulkan) {
      render_overlay = false;
    }
    if (render_overlay) {

      const SDL_GLContext current_gl_context = SDL_GL_GetCurrentContext();
      if (current_gl_context == nullptr) {
        render_overlay = false;
      } else if (mono_ui_initialized() && current_gl_context != mono_opengl_context) {
        render_overlay = false;
        if (!mono_overlay_disabled.exchange(true, std::memory_order_acq_rel)) {
          print("[renderer] TF2 replaced its GL context; disabling the overlay to avoid stale driver state "
                "(expected=%p, current=%p)\n", mono_opengl_context, current_gl_context);
        }
      } else if (!mono_ui_initialized() && !mono_ui_initialize_opengl(window)) {
        render_overlay = false;
      }
    }

    if (render_overlay) {
      std::lock_guard<std::recursive_mutex> lock(mono_ui_mutex);
      if (mono_frame_ready && mono_runtime.initialized() &&
          SDL_GL_GetCurrentContext() == mono_opengl_context) {

        ImGui_ImplOpenGL3_NewFrame();
        ImDrawData* const draw_data = ImGui::GetDrawData();
        if (draw_data != nullptr && draw_data->Valid) {
          const GLboolean framebuffer_srgb_enabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
          if (framebuffer_srgb_enabled) {
            glDisable(GL_FRAMEBUFFER_SRGB);
          }
          ImGui_ImplOpenGL3_RenderDrawData(draw_data);
          if (framebuffer_srgb_enabled) {
            glEnable(GL_FRAMEBUFFER_SRGB);
          }
        }
      }
    }

    original(window);
  }

  cathook::core::service_detach_request();
}

Uint32 get_window_flags_hook(SDL_Window* window) {
  CATHOOK_HOOK_GUARD();
  if (sdl_hooks_uninstalling.load(std::memory_order_acquire)) {
    return get_window_flags_original != nullptr ? get_window_flags_original(window) : 0;
  }

  sdl_hook_call_guard guard{};

  if (get_window_flags_original == nullptr) {
    return 0;
  }

  if (window != nullptr) {
    sdl_window = window;
  }

  return get_window_flags_original(window);
}

SDL_bool get_window_WM_info_hook(SDL_Window* window, SDL_SysWMinfo* info) {
  CATHOOK_HOOK_GUARD();
  if (sdl_hooks_uninstalling.load(std::memory_order_acquire)) {
    return get_window_WM_info_original != nullptr ? get_window_WM_info_original(window, info) : SDL_FALSE;
  }

  sdl_hook_call_guard guard{};

  if (get_window_WM_info_original == nullptr) {
    return SDL_FALSE;
  }

  if (window != nullptr) {
    sdl_window = window;
  }

  return get_window_WM_info_original(window, info);
}

void get_window_size_hook(SDL_Window* window, int* w, int* h) {
  CATHOOK_HOOK_GUARD();
  if (sdl_hooks_uninstalling.load(std::memory_order_acquire)) {
    if (get_window_size_original != nullptr) {
      get_window_size_original(window, w, h);
      return;
    }
  }

  sdl_hook_call_guard guard{};

  if (get_window_size_original == nullptr) {
    if (w != nullptr) {
      *w = 0;
    }
    if (h != nullptr) {
      *h = 0;
    }
    return;
  }

  sdl_window = window;

  get_window_size_original(window, w, h);
}
