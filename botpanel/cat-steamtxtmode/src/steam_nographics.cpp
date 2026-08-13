#include "steam_nographics.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <vector>

namespace steam_nographics
{

void install_module_hooks(const char* library_path, void* module_handle);

namespace
{

struct runtime_config
{
  bool enabled = false;
  bool hide_windows = true;
  bool skip_draw_calls = true;
  bool trim_webhelper = true;
  unsigned int present_interval_us = 100000;
};

std::atomic_bool steam_ui_loaded{ false };
std::atomic_bool chrome_html_loaded{ false };

bool env_flag(const char* name, bool fallback)
{
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0')
  {
    return fallback;
  }

  return std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

unsigned int env_uint(const char* name, unsigned int fallback)
{
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0')
  {
    return fallback;
  }

  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed > 10000000UL)
  {
    return fallback;
  }

  return static_cast<unsigned int>(parsed);
}

const runtime_config& config()
{
  static const runtime_config value = []
  {
    runtime_config configured{};
    configured.enabled = env_flag("CAT_STEAM_TXTMODE", true);
    configured.hide_windows = env_flag("CAT_STEAM_TXTMODE_HIDE_WINDOWS", true);
    configured.skip_draw_calls = env_flag("CAT_STEAM_TXTMODE_DROP_DRAWS", true);
    configured.trim_webhelper = env_flag("CAT_STEAM_TXTMODE_TRIM_WEBHELPER", false);
    configured.present_interval_us = env_uint("CAT_STEAM_TXTMODE_FRAME_INTERVAL_US", 100000);
    return configured;
  }();
  return value;
}

const char* basename(const char* path)
{
  if (path == nullptr)
  {
    return "";
  }

  const char* slash = std::strrchr(path, '/');
  return slash != nullptr ? slash + 1 : path;
}

bool process_is_steam_ui_host()
{
  char process_path[4096]{};
  const ssize_t length = readlink("/proc/self/exe", process_path, sizeof(process_path) - 1);
  if (length <= 0)
  {
    return false;
  }

  const char* const process_name = basename(process_path);
  return std::strcmp(process_name, "steam") == 0 || std::strcmp(process_name, "steamwebhelper") == 0;
}

bool library_has_name(const char* path, const char* name)
{
  return std::strcmp(basename(path), name) == 0;
}

std::uint64_t monotonic_microseconds()
{
  timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
  {
    return 0;
  }

  return static_cast<std::uint64_t>(now.tv_sec) * 1000000ULL
    + static_cast<std::uint64_t>(now.tv_nsec) / 1000ULL;
}

void sleep_microseconds(std::uint64_t remaining)
{
  while (remaining != 0)
  {
    timespec request{
      static_cast<time_t>(remaining / 1000000ULL),
      static_cast<long>((remaining % 1000000ULL) * 1000ULL)
    };
    timespec interrupted{};
    if (nanosleep(&request, &interrupted) == 0 || errno != EINTR)
    {
      return;
    }
    remaining = static_cast<std::uint64_t>(interrupted.tv_sec) * 1000000ULL
      + static_cast<std::uint64_t>(interrupted.tv_nsec) / 1000ULL;
  }
}

bool argument_has_prefix(char* const argv[], const char* prefix)
{
  const size_t length = std::strlen(prefix);
  for (char* const* argument = argv; argument != nullptr && *argument != nullptr; ++argument)
  {
    if (std::strncmp(*argument, prefix, length) == 0)
    {
      return true;
    }
  }
  return false;
}

bool is_webhelper_child(char* const argv[])
{
  return argument_has_prefix(argv, "--type=");
}

}

void initialize()
{
  if (!is_enabled() || (!process_is_steam_ui_host() && !should_synthesize_hardware()))
  {
    return;
  }

  if (std::getenv("LP_NUM_THREADS") == nullptr)
  {
    setenv("LP_NUM_THREADS", "1", 0);
  }
  if (std::getenv("GALLIUM_NUM_THREADS") == nullptr)
  {
    setenv("GALLIUM_NUM_THREADS", "1", 0);
  }
  if (std::getenv("vblank_mode") == nullptr)
  {
    setenv("vblank_mode", "0", 0);
  }

  char process_path[4096]{};
  const ssize_t length = readlink("/proc/self/exe", process_path, sizeof(process_path) - 1);
  if (length > 0 && std::strcmp(basename(process_path), "steam") == 0)
  {
    unsetenv("LD_PRELOAD");
  }
}

void on_library_loaded(const char* library_path, void* module_handle)
{
  if (library_has_name(library_path, "steamui.so"))
  {
    steam_ui_loaded.store(true, std::memory_order_release);
  }
  else if (library_has_name(library_path, "chromehtml.so"))
  {
    chrome_html_loaded.store(true, std::memory_order_release);
  }

  install_module_hooks(library_path, module_handle);
}

bool is_enabled()
{
  return config().enabled;
}

bool should_hide_windows()
{
  return is_enabled() && config().hide_windows && process_is_steam_ui_host();
}

bool should_skip_presentation()
{
  return is_enabled() && process_is_steam_ui_host()
    && (steam_ui_loaded.load(std::memory_order_acquire) || chrome_html_loaded.load(std::memory_order_acquire));
}

bool should_skip_draw_calls()
{
  return should_skip_presentation() && config().skip_draw_calls;
}

int offscreen_coordinate()
{
  return -32000;
}

unsigned int offscreen_extent()
{
  return 1;
}

void limit_present_rate()
{
  if (!should_skip_presentation() || config().present_interval_us == 0)
  {
    return;
  }

  thread_local std::uint64_t last_present = 0;
  const std::uint64_t now = monotonic_microseconds();
  if (last_present != 0 && now > last_present)
  {
    const std::uint64_t elapsed = now - last_present;
    if (elapsed < config().present_interval_us)
    {
      sleep_microseconds(config().present_interval_us - elapsed);
    }
  }
  last_present = monotonic_microseconds();
}

char** rewrite_webhelper_argv(const char* executable_path, char* const argv[])
{
  if (!is_enabled() || !config().trim_webhelper || !library_has_name(executable_path, "steamwebhelper")
    || is_webhelper_child(argv))
  {
    return nullptr;
  }

  static constexpr const char* switches[] = {
    "--disable-gpu",
    "--disable-gpu-compositing",
    "--disable-extensions",
    "--disable-features=CalculateNativeWinOcclusion,MediaRouter,OptimizationHints,TranslateUI,VizDisplayCompositor",
    "--mute-audio",
  };

  size_t count = 0;
  while (argv != nullptr && argv[count] != nullptr)
  {
    ++count;
  }

  std::vector<const char*> additions{};
  additions.reserve(sizeof(switches) / sizeof(switches[0]));
  for (const char* option : switches)
  {
    const char* equal = std::strchr(option, '=');
    const size_t key_size = equal == nullptr ? std::strlen(option) : static_cast<size_t>(equal - option);
    bool present = false;
    for (size_t index = 0; index < count; ++index)
    {
      if (std::strncmp(argv[index], option, key_size) == 0
        && (argv[index][key_size] == '\0' || argv[index][key_size] == '='))
      {
        present = true;
        break;
      }
    }
    if (!present)
    {
      additions.emplace_back(option);
    }
  }

  if (additions.empty())
  {
    return nullptr;
  }

  char** rewritten = static_cast<char**>(std::malloc((count + additions.size() + 1) * sizeof(char*)));
  if (rewritten == nullptr)
  {
    return nullptr;
  }

  for (size_t index = 0; index < count; ++index)
  {
    rewritten[index] = argv[index];
  }
  for (size_t index = 0; index < additions.size(); ++index)
  {
    rewritten[count + index] = const_cast<char*>(additions[index]);
  }
  rewritten[count + additions.size()] = nullptr;
  return rewritten;
}

}
