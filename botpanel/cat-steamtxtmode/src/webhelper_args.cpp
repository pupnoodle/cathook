#include "config.hpp"

#include <cstdlib>
#include <cstring>
#include <spawn.h>
#include <unistd.h>
#include <vector>

#include <dlfcn.h>

namespace
{

using namespace cat_stm;

const char* base_name(const char* path)
{
  if (path == nullptr)
  {
    return "";
  }
  const char* slash = std::strrchr(path, '/');
  return slash ? slash + 1 : path;
}

bool argv_has_type(char* const argv[])
{
  for (char* const* a = argv; a && *a; ++a)
  {
    if (std::strncmp(*a, "--type=", 7) == 0)
    {
      return true;
    }
  }
  return false;
}

bool argv_has_key(char* const argv[], const char* key)
{
  const std::size_t key_len = std::strlen(key);
  for (char* const* a = argv; a && *a; ++a)
  {
    if (std::strncmp(*a, key, key_len) == 0)
    {
      return true;
    }
  }
  return false;
}

const char* const trim_switches[] = {
  "--renderer-process-limit=1",
  "--disable-gpu",
  "--disable-gpu-compositing",
  "--disable-extensions",
  "--disable-notifications",
  "--mute-audio",
  "--enable-low-end-device-mode",
};

const char* const strip_switches[] = {
  "--disable-background-timer-throttling",
  "--disable-backgrounding-occluded-windows",
  "--disable-renderer-backgrounding",
};

bool is_stripped(const char* arg)
{
  for (const char* s : strip_switches)
  {
    if (std::strcmp(arg, s) == 0)
    {
      return true;
    }
  }
  return false;
}

char** rewrite_webhelper_argv(const char* path, char* const argv[])
{
  const config& cfg = settings();
  if (cfg.disabled || !cfg.webhelper_trim)
  {
    return nullptr;
  }
  if (std::strcmp(base_name(path), "steamwebhelper") != 0 || argv_has_type(argv))
  {
    return nullptr;
  }

  std::size_t argc = 0;
  while (argv[argc] != nullptr)
  {
    ++argc;
  }

  std::vector<const char*> extra;
  for (const char* sw : trim_switches)
  {
    char key[64];
    const char* eq = std::strchr(sw, '=');
    const std::size_t key_len = eq ? static_cast<std::size_t>(eq - sw) : std::strlen(sw);
    if (key_len < sizeof(key))
    {
      std::memcpy(key, sw, key_len);
      key[key_len] = '\0';
      if (!argv_has_key(argv, key))
      {
        extra.push_back(sw);
      }
    }
  }
  if (cfg.webhelper_single && !argv_has_key(argv, "--single-process"))
  {
    extra.push_back("--single-process");
  }

  std::size_t stripped = 0;
  for (std::size_t i = 0; i < argc; ++i)
  {
    if (is_stripped(argv[i]))
    {
      ++stripped;
    }
  }
  if (extra.empty() && stripped == 0)
  {
    return nullptr;
  }

  char** out = static_cast<char**>(std::malloc((argc + extra.size() + 1) * sizeof(char*)));
  if (out == nullptr)
  {
    return nullptr;
  }
  std::size_t n = 0;
  for (std::size_t i = 0; i < argc; ++i)
  {
    if (!is_stripped(argv[i]))
    {
      out[n++] = argv[i];
    }
  }
  for (const char* sw : extra)
  {
    out[n++] = const_cast<char*>(sw);
  }
  out[n] = nullptr;

  log_line("steamwebhelper: +%zu switch(es), -%zu stripped", extra.size(), stripped);
  return out;
}

}

#define CAT_STM_EXPORT extern "C" __attribute__((visibility("default")))

CAT_STM_EXPORT int execv(const char* path, char* const argv[])
{
  static int (*real)(const char*, char* const[]) = nullptr;
  if (real == nullptr)
  {
    real = reinterpret_cast<decltype(real)>(::dlsym(RTLD_NEXT, "execv"));
  }
  char** rewritten = rewrite_webhelper_argv(path, argv);
  return real(path, rewritten ? rewritten : argv);
}

CAT_STM_EXPORT int execvp(const char* file, char* const argv[])
{
  static int (*real)(const char*, char* const[]) = nullptr;
  if (real == nullptr)
  {
    real = reinterpret_cast<decltype(real)>(::dlsym(RTLD_NEXT, "execvp"));
  }
  char** rewritten = rewrite_webhelper_argv(file, argv);
  return real(file, rewritten ? rewritten : argv);
}

CAT_STM_EXPORT int execve(const char* path, char* const argv[], char* const envp[])
{
  static int (*real)(const char*, char* const[], char* const[]) = nullptr;
  if (real == nullptr)
  {
    real = reinterpret_cast<decltype(real)>(::dlsym(RTLD_NEXT, "execve"));
  }
  char** rewritten = rewrite_webhelper_argv(path, argv);
  return real(path, rewritten ? rewritten : argv, envp);
}

CAT_STM_EXPORT int posix_spawn(pid_t* pid, const char* path,
                               const posix_spawn_file_actions_t* file_actions,
                               const posix_spawnattr_t* attrp,
                               char* const argv[], char* const envp[])
{
  static int (*real)(pid_t*, const char*, const posix_spawn_file_actions_t*,
                     const posix_spawnattr_t*, char* const[], char* const[]) = nullptr;
  if (real == nullptr)
  {
    real = reinterpret_cast<decltype(real)>(::dlsym(RTLD_NEXT, "posix_spawn"));
  }
  char** rewritten = rewrite_webhelper_argv(path, argv);
  return real(pid, path, file_actions, attrp, rewritten ? rewritten : argv, envp);
}
