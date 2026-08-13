#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

volatile std::sig_atomic_t stop_requested = 0;

void request_stop(int)
{
  stop_requested = 1;
}

std::string trim(std::string value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos)
  {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool parse_bool(const std::string& value, bool fallback)
{
  const std::string normalized = trim(value);
  if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
  {
    return true;
  }
  if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
  {
    return false;
  }
  return fallback;
}

unsigned int parse_uint(const std::string& value, unsigned int fallback, unsigned int maximum)
{
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0' || parsed > maximum)
  {
    return fallback;
  }
  return static_cast<unsigned int>(parsed);
}

std::vector<std::string> split_command(const std::string& command)
{
  std::vector<std::string> result;
  std::string current;
  char quote = 0;
  bool escaped = false;
  for (const char character : command)
  {
    if (escaped)
    {
      current += character;
      escaped = false;
      continue;
    }
    if (character == '\\' && quote != '\'')
    {
      escaped = true;
      continue;
    }
    if (quote != 0)
    {
      if (character == quote)
      {
        quote = 0;
      }
      else
      {
        current += character;
      }
      continue;
    }
    if (character == '\'' || character == '"')
    {
      quote = character;
    }
    else if (character == ' ' || character == '\t')
    {
      if (!current.empty())
      {
        result.push_back(std::move(current));
      }
    }
    else
    {
      current += character;
    }
  }
  if (escaped)
  {
    current += '\\';
  }
  if (quote != 0)
  {
    throw std::runtime_error("unterminated quote in command");
  }
  if (!current.empty())
  {
    result.push_back(std::move(current));
  }
  return result;
}

std::vector<std::string> split_names(const std::string& value)
{
  std::vector<std::string> result;
  std::string item;
  std::istringstream stream(value);
  while (std::getline(stream, item, ','))
  {
    item = trim(item);
    if (!item.empty())
    {
      result.push_back(std::move(item));
    }
  }
  return result;
}

struct config
{
  unsigned int poll_interval_ms = 1000;
  bool launch_missing = false;
  bool stop_children = false;
  bool log_processes = true;
  bool textmode = true;
  bool synthetic_hardware = true;
  bool hide_windows = true;
  bool drop_draws = true;
  bool trim_webhelper = false;
  bool randomize_hardware = true;
  unsigned int frame_interval_us = 100000;
  std::string hardware_seed;
  std::string steam_preload;
  std::string tf2_preload;
  std::vector<std::string> steam_command;
  std::vector<std::string> tf2_command;
  std::vector<std::string> steam_names{
    "steam", "steamwebhelper", "gameoverlayui"
  };
  std::vector<std::string> tf2_names{
    "tf_linux64", "tf_linux", "hl2_linux"
  };
};

void set_config_value(config& settings, const std::string& key, const std::string& value)
{
  if (key == "poll_interval_ms") settings.poll_interval_ms = parse_uint(value, 1000, 600000);
  else if (key == "launch_missing") settings.launch_missing = parse_bool(value, settings.launch_missing);
  else if (key == "stop_children") settings.stop_children = parse_bool(value, settings.stop_children);
  else if (key == "log_processes") settings.log_processes = parse_bool(value, settings.log_processes);
  else if (key == "textmode") settings.textmode = parse_bool(value, settings.textmode);
  else if (key == "synthetic_hardware") settings.synthetic_hardware = parse_bool(value, settings.synthetic_hardware);
  else if (key == "hide_windows") settings.hide_windows = parse_bool(value, settings.hide_windows);
  else if (key == "drop_draws") settings.drop_draws = parse_bool(value, settings.drop_draws);
  else if (key == "trim_webhelper") settings.trim_webhelper = parse_bool(value, settings.trim_webhelper);
  else if (key == "randomize_hardware") settings.randomize_hardware = parse_bool(value, settings.randomize_hardware);
  else if (key == "frame_interval_us") settings.frame_interval_us = parse_uint(value, 100000, 10000000);
  else if (key == "hardware_seed") settings.hardware_seed = value;
  else if (key == "steam_preload") settings.steam_preload = value;
  else if (key == "tf2_preload") settings.tf2_preload = value;
  else if (key == "steam_command") settings.steam_command = split_command(value);
  else if (key == "tf2_command") settings.tf2_command = split_command(value);
  else if (key == "steam_names") settings.steam_names = split_names(value);
  else if (key == "tf2_names") settings.tf2_names = split_names(value);
  else std::cerr << "catsteamtxtmode: ignoring unknown config key '" << key << "'\n";
}

bool load_config(const std::string& path, config& settings)
{
  std::ifstream input(path);
  if (!input)
  {
    return false;
  }
  std::string line;
  unsigned int line_number = 0;
  while (std::getline(input, line))
  {
    ++line_number;
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';')
    {
      continue;
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos)
    {
      std::cerr << path << ':' << line_number << ": expected key=value\n";
      return false;
    }
    try
    {
      set_config_value(settings, trim(line.substr(0, separator)), trim(line.substr(separator + 1)));
    }
    catch (const std::exception& error)
    {
      std::cerr << path << ':' << line_number << ": " << error.what() << '\n';
      return false;
    }
  }
  return true;
}

std::string executable_directory(const char* argv0)
{
  char path[4096]{};
  const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (length > 0)
  {
    path[length] = '\0';
    const char* slash = std::strrchr(path, '/');
    if (slash != nullptr)
    {
      return std::string(path, static_cast<std::size_t>(slash - path));
    }
  }
  const std::string fallback = argv0 == nullptr ? "catsteamtxtmode" : argv0;
  const auto slash = fallback.find_last_of('/');
  return slash == std::string::npos ? "." : fallback.substr(0, slash);
}

std::string default_config_path(const char* argv0)
{
  return executable_directory(argv0) + "/catsteamtxtmode.cfg";
}

std::string basename_of(const std::string& path)
{
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::set<std::string> target_names(const config& settings)
{
  std::set<std::string> result(settings.steam_names.begin(), settings.steam_names.end());
  result.insert(settings.tf2_names.begin(), settings.tf2_names.end());
  return result;
}

std::map<pid_t, std::string> find_targets(const std::set<std::string>& names)
{
  std::map<pid_t, std::string> result;
  DIR* directory = opendir("/proc");
  if (directory == nullptr)
  {
    return result;
  }
  while (dirent* entry = readdir(directory))
  {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(entry->d_name, &end, 10);
    if (errno != 0 || end == entry->d_name || *end != '\0' || value <= 0)
    {
      continue;
    }
    std::ifstream comm(std::string("/proc/") + entry->d_name + "/comm");
    std::string name;
    std::getline(comm, name);
    name = trim(name);
    if (names.find(name) != names.end())
    {
      result.emplace(static_cast<pid_t>(value), std::move(name));
    }
  }
  closedir(directory);
  return result;
}

bool contains_name(const std::vector<std::string>& names, const std::string& value)
{
  return std::find(names.begin(), names.end(), value) != names.end();
}

std::string select_preload(const config& settings, const std::vector<std::string>& command)
{
  if (!command.empty() && contains_name(settings.tf2_names, basename_of(command.front())))
  {
    return settings.tf2_preload.empty() ? settings.steam_preload : settings.tf2_preload;
  }
  return settings.steam_preload;
}

void set_child_environment(const config& settings, const std::string& preload)
{
  setenv("CAT_STEAM_TXTMODE", settings.textmode ? "1" : "0", 1);
  setenv("CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE", settings.synthetic_hardware ? "1" : "0", 1);
  setenv("CAT_STEAM_TXTMODE_HIDE_WINDOWS", settings.hide_windows ? "1" : "0", 1);
  setenv("CAT_STEAM_TXTMODE_DROP_DRAWS", settings.drop_draws ? "1" : "0", 1);
  setenv("CAT_STEAM_TXTMODE_TRIM_WEBHELPER", settings.trim_webhelper ? "1" : "0", 1);
  setenv("CAT_STEAM_TXTMODE_FRAME_INTERVAL_US", std::to_string(settings.frame_interval_us).c_str(), 1);
  if (!settings.hardware_seed.empty()) setenv("CAT_STEAM_TXTMODE_HARDWARE_SEED", settings.hardware_seed.c_str(), 1);
  else unsetenv("CAT_STEAM_TXTMODE_HARDWARE_SEED");
  setenv("CAT_STEAM_TXTMODE_HARDWARE_RANDOMIZE", settings.randomize_hardware ? "1" : "0", 1);
  if (!preload.empty()) setenv("LD_PRELOAD", preload.c_str(), 1);
}

pid_t launch(const config& settings, const std::vector<std::string>& command)
{
  if (command.empty())
  {
    return -1;
  }
  const pid_t child = fork();
  if (child < 0)
  {
    std::cerr << "catsteamtxtmode: fork failed: " << std::strerror(errno) << '\n';
    return -1;
  }
  if (child == 0)
  {
    set_child_environment(settings, select_preload(settings, command));
    std::vector<char*> arguments;
    arguments.reserve(command.size() + 1);
    for (const std::string& argument : command)
    {
      arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);
    execvp(arguments[0], arguments.data());
    std::cerr << "catsteamtxtmode: exec " << command[0] << " failed: " << std::strerror(errno) << '\n';
    _exit(127);
  }
  std::cout << "catsteamtxtmode: launched " << command.front() << " pid=" << child << '\n';
  return child;
}

void stop_child(pid_t child)
{
  if (child <= 0)
  {
    return;
  }
  kill(child, SIGTERM);
  for (unsigned int attempt = 0; attempt < 20; ++attempt)
  {
    if (waitpid(child, nullptr, WNOHANG) == child)
    {
      return;
    }
    usleep(50000);
  }
  kill(child, SIGKILL);
  waitpid(child, nullptr, 0);
}

void log_process_changes(const std::map<pid_t, std::string>& previous,
  const std::map<pid_t, std::string>& current)
{
  for (const auto& [pid, name] : current)
  {
    if (previous.find(pid) == previous.end())
    {
      std::cout << "catsteamtxtmode: found " << name << " pid=" << pid
        << " (must have been preloaded at process launch)\n";
    }
  }
  for (const auto& [pid, name] : previous)
  {
    if (current.find(pid) == current.end())
    {
      std::cout << "catsteamtxtmode: lost " << name << " pid=" << pid << '\n';
    }
  }
}

void usage(const char* program)
{
  std::cout << "Usage: " << program << " [--config PATH] [--once] [--help]\n"
    << "  Watches Steam/TF2 processes and optionally launches configured commands\n"
    << "  with libcatsteamtxtmode.so in LD_PRELOAD.\n";
}

}

int main(int argc, char** argv)
{
  std::string config_path;
  bool once = false;
  for (int index = 1; index < argc; ++index)
  {
    if (std::strcmp(argv[index], "--help") == 0 || std::strcmp(argv[index], "-h") == 0)
    {
      usage(argv[0]);
      return 0;
    }
    if (std::strcmp(argv[index], "--once") == 0)
    {
      once = true;
      continue;
    }
    if (std::strcmp(argv[index], "--config") == 0 && index + 1 < argc)
    {
      config_path = argv[++index];
      continue;
    }
    std::cerr << "Unknown argument: " << argv[index] << '\n';
    usage(argv[0]);
    return 2;
  }

  if (config_path.empty())
  {
    config_path = default_config_path(argv[0]);
  }
  config settings;
  if (access(config_path.c_str(), R_OK) == 0 && !load_config(config_path, settings))
  {
    return 2;
  }
  if (access(config_path.c_str(), R_OK) != 0)
  {
    std::cout << "catsteamtxtmode: config not found, using built-in defaults: " << config_path << '\n';
  }
  const std::string default_preload = executable_directory(argv[0]) + "/libcatsteamtxtmode.so";
  if (settings.steam_preload.empty()) settings.steam_preload = default_preload;
  if (settings.tf2_preload.empty()) settings.tf2_preload = default_preload;

  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);
  const auto names = target_names(settings);
  std::map<pid_t, std::string> previous;
  pid_t steam_child = -1;
  pid_t tf2_child = -1;
  std::cout << "catsteamtxtmode: watching " << names.size() << " process names\n";

  while (!stop_requested)
  {
    const auto current = find_targets(names);
    if (settings.log_processes)
    {
      log_process_changes(previous, current);
    }

    if (settings.launch_missing)
    {
      const bool steam_running = std::any_of(current.begin(), current.end(), [&](const auto& item)
      {
        return contains_name(settings.steam_names, item.second);
      });
      const bool tf2_running = std::any_of(current.begin(), current.end(), [&](const auto& item)
      {
        return contains_name(settings.tf2_names, item.second);
      });
      if (!steam_running && steam_child <= 0 && !settings.steam_command.empty())
      {
        steam_child = launch(settings, settings.steam_command);
      }
      if (!tf2_running && tf2_child <= 0 && !settings.tf2_command.empty())
      {
        tf2_child = launch(settings, settings.tf2_command);
      }
    }

    if (steam_child > 0 && waitpid(steam_child, nullptr, WNOHANG) == steam_child) steam_child = -1;
    if (tf2_child > 0 && waitpid(tf2_child, nullptr, WNOHANG) == tf2_child) tf2_child = -1;
    previous = current;
    if (once)
    {
      break;
    }
    usleep(settings.poll_interval_ms * 1000U);
  }

  if (settings.stop_children)
  {
    stop_child(steam_child);
    stop_child(tf2_child);
  }
  std::cout << "catsteamtxtmode: stopped\n";
  return 0;
}
