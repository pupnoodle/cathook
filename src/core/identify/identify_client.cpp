#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "core/identify/identify_client.hpp"

#include "core/print.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace cathook::core::identify
{

namespace
{

constexpr std::chrono::seconds send_deadline{2};

}

identify_client::~identify_client()
{
  stop();
}

void identify_client::set_peers_handler(peers_callback_t callback)
{
  m_peers_callback = std::move(callback);
}

void identify_client::set_chat_handler(chat_callback_t callback)
{
  m_chat_callback = std::move(callback);
}

void identify_client::connect(std::string host, int port)
{
  m_host = std::move(host);
  m_port = port;
  m_running = true;
  m_worker = std::thread{[this]() {
    worker_loop();
  }};
}

void identify_client::stop()
{
  m_running = false;
  m_cv.notify_all();
  if (m_worker.joinable())
  {
    m_worker.join();
  }
  close_socket();
}

void identify_client::update_identity(std::string_view server_id, std::string_view player_hash, std::string_view signature, int head_scale)
{
  std::lock_guard<std::mutex> lock{m_identity_mutex};
  if (server_id == m_current_server_id && player_hash == m_current_player_hash && signature == m_current_signature && head_scale == m_current_head_scale)
  {
    return;
  }

  m_current_server_id = server_id;
  m_current_player_hash = player_hash;
  m_current_signature = signature;
  m_current_head_scale = head_scale;

  if (server_id.empty() || player_hash.empty() || signature.empty())
  {
    return;
  }

  send_line("{\"type\":\"hello\",\"server_id\":\"" + escape_string(server_id) +
            "\",\"player_hash\":\"" + escape_string(player_hash) +
            "\",\"signature\":\"" + escape_string(signature) +
            "\",\"head_scale\":" + std::to_string(head_scale) + "}");
}

void identify_client::send_chat(std::string_view message)
{
  if (message.empty() || message.size() > 128)
  {
    return;
  }

  send_line("{\"type\":\"chat\",\"msg\":\"" + escape_string(message) + "\"}");
}

void identify_client::close_socket()
{
  std::lock_guard<std::mutex> lock{m_socket_mutex};
  int s = m_socket.exchange(-1);
  if (s >= 0)
  {
    ::shutdown(s, SHUT_RDWR);
    ::close(s);
  }
}

bool identify_client::dial()
{
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  const std::string service = std::to_string(m_port);

  gaicb request{};
  request.ar_name = m_host.c_str();
  request.ar_service = service.c_str();
  request.ar_request = &hints;
  gaicb* requests[] = {&request};

  if (::getaddrinfo_a(GAI_NOWAIT, requests, 1, nullptr) != 0)
  {
    return false;
  }

  while (::gai_error(&request) == EAI_INPROGRESS)
  {
    if (!m_running.load(std::memory_order_acquire))
    {
      ::gai_cancel(&request);
    }

    const timespec timeout{0, 100000000};
    ::gai_suspend(requests, 1, &timeout);
  }

  int resolution_error = ::gai_error(&request);
  if (!m_running.load(std::memory_order_acquire))
  {
    if (resolution_error == EAI_INPROGRESS)
    {
      ::gai_cancel(&request);
      while ((resolution_error = ::gai_error(&request)) == EAI_INPROGRESS)
      {
        const timespec timeout{0, 1000000};
        ::gai_suspend(requests, 1, &timeout);
      }
    }

    if (request.ar_result != nullptr)
    {
      ::freeaddrinfo(request.ar_result);
    }
    return false;
  }

  if (resolution_error != 0)
  {
    if (request.ar_result != nullptr)
    {
      ::freeaddrinfo(request.ar_result);
    }
    return false;
  }

  res = request.ar_result;

  int s = -1;
  for (auto* p = res; p != nullptr; p = p->ai_next)
  {
    s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s < 0)
    {
      continue;
    }

    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0 || ::fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0)
    {
      ::close(s);
      s = -1;
      continue;
    }

    bool connected = ::connect(s, p->ai_addr, p->ai_addrlen) == 0;
    if (!connected && errno == EINPROGRESS)
    {
      while (m_running.load(std::memory_order_acquire))
      {
        pollfd poll_descriptor{s, POLLOUT | POLLERR | POLLHUP | POLLNVAL, 0};
        const int poll_result = ::poll(&poll_descriptor, 1, 100);
        if (poll_result < 0)
        {
          if (errno == EINTR)
          {
            continue;
          }
          break;
        }
        if (poll_result == 0)
        {
          continue;
        }
        if ((poll_descriptor.revents & POLLNVAL) != 0)
        {
          break;
        }

        int socket_error = 0;
        socklen_t socket_error_size = sizeof(socket_error);
        if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) == 0 && socket_error == 0)
        {
          connected = true;
        }
        break;
      }
    }

    if (connected)
    {
      break;
    }

    ::close(s);
    s = -1;
  }

  ::freeaddrinfo(res);

  if (s < 0)
  {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock{m_socket_mutex};
    if (!m_running.load(std::memory_order_acquire))
    {
      ::close(s);
      return false;
    }
    m_socket.store(s, std::memory_order_release);
  }

  return true;
}

void identify_client::worker_loop()
{
  std::string buf;
  std::mt19937 rng{static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
  std::uniform_int_distribution<int> jitter_dial(0, 5000);
  std::uniform_int_distribution<int> jitter_loop(0, 3000);

  while (m_running)
  {
    if (m_socket < 0 && !dial())
    {
      std::unique_lock<std::mutex> lock{m_identity_mutex};
      m_cv.wait_for(lock, std::chrono::milliseconds(5000 + jitter_dial(rng)), [this]() {
        return !m_running;
      });
      continue;
    }

    {
      std::lock_guard<std::mutex> lock{m_identity_mutex};
      if (!m_current_server_id.empty() && !m_current_player_hash.empty() && !m_current_signature.empty())
      {
        send_line("{\"type\":\"hello\",\"server_id\":\"" + escape_string(m_current_server_id) +
                  "\",\"player_hash\":\"" + escape_string(m_current_player_hash) +
                  "\",\"signature\":\"" + escape_string(m_current_signature) +
                  "\",\"head_scale\":" + std::to_string(m_current_head_scale) + "}");
      }
    }

    char chunk[1024];
    while (m_running)
    {
      int s = m_socket;
      if (s < 0)
      {
        break;
      }

      pollfd poll_descriptor{s, POLLIN | POLLERR | POLLHUP | POLLNVAL, 0};
      const int poll_result = ::poll(&poll_descriptor, 1, 100);
      if (poll_result < 0)
      {
        if (errno == EINTR)
        {
          continue;
        }
        break;
      }
      if (poll_result == 0)
      {
        continue;
      }
      if ((poll_descriptor.revents & POLLNVAL) != 0)
      {
        break;
      }

      ssize_t n = ::recv(s, chunk, sizeof(chunk), MSG_DONTWAIT);
      if (n == 0)
      {
        break;
      }
      if (n < 0)
      {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          continue;
        }
        break;
      }

      buf.append(chunk, static_cast<size_t>(n));
      size_t pos = 0;
      while ((pos = buf.find('\n')) != std::string::npos)
      {
        handle_line(buf.substr(0, pos));
        buf.erase(0, pos + 1);
      }
    }

    close_socket();
    buf.clear();

    if (m_running)
    {
      std::unique_lock<std::mutex> lock{m_identity_mutex};
      m_cv.wait_for(lock, std::chrono::milliseconds(2000 + jitter_loop(rng)), [this]() {
        return !m_running;
      });
    }
  }
}

void identify_client::send_line(std::string line)
{
  std::lock_guard<std::mutex> lock{m_send_mutex};
  std::lock_guard<std::mutex> socket_lock{m_socket_mutex};
  int s = m_socket;
  if (s < 0)
  {
    return;
  }

  line += '\n';
  size_t offset = 0;
  const auto deadline = std::chrono::steady_clock::now() + send_deadline;
  while (offset < line.size())
  {
    if (std::chrono::steady_clock::now() >= deadline)
    {
      if (m_socket.load(std::memory_order_acquire) == s)
      {
        m_socket.store(-1, std::memory_order_release);
        ::shutdown(s, SHUT_RDWR);
        ::close(s);
      }
      return;
    }

    ssize_t n = ::send(s, line.data() + offset, line.size() - offset, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n > 0)
    {
      offset += static_cast<size_t>(n);
      continue;
    }

    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
    {
      pollfd poll_descriptor{s, POLLOUT | POLLERR | POLLHUP | POLLNVAL, 0};
      const int poll_result = ::poll(&poll_descriptor, 1, 100);
      if (poll_result == 0 || (poll_result < 0 && errno == EINTR))
      {
        if (m_running.load(std::memory_order_acquire))
        {
          continue;
        }
        return;
      }
      if (m_running.load(std::memory_order_acquire) && poll_result > 0 &&
          (poll_descriptor.revents & POLLOUT) != 0)
      {
        continue;
      }
    }

    if (n <= 0)
    {
      if (m_socket.load(std::memory_order_acquire) == s)
      {
        m_socket.store(-1, std::memory_order_release);
        ::shutdown(s, SHUT_RDWR);
        ::close(s);
      }
      return;
    }
  }
}

std::string identify_client::escape_string(std::string_view value)
{
  std::string out;
  out.reserve(value.size() + 4);
  for (char c : value)
  {
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if (static_cast<unsigned char>(c) >= 0x20)
    {
      out += c;
    }
  }
  return out;
}

std::string identify_client::find_json_value(std::string_view json, std::string_view key)
{
  std::string needle = "\"" + std::string{key} + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string_view::npos)
  {
    return {};
  }

  pos = json.find(':', pos + needle.size());
  if (pos == std::string_view::npos)
  {
    return {};
  }

  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
  {
    ++pos;
  }

  if (pos >= json.size())
  {
    return {};
  }

  if (json[pos] == '"')
  {
    ++pos;
    std::string out;
    while (pos < json.size() && json[pos] != '"')
    {
      if (json[pos] == '\\' && pos + 1 < json.size())
      {
        out += json[pos + 1];
        pos += 2;
      }
      else
      {
        out += json[pos++];
      }
    }
    return out;
  }

  if (json[pos] == '[')
  {
    size_t start = pos;
    int depth = 0;
    for (; pos < json.size(); ++pos)
    {
      if (json[pos] == '[')
      {
        ++depth;
      }
      else if (json[pos] == ']')
      {
        if (--depth == 0)
        {
          return std::string{json.substr(start, pos - start + 1)};
        }
      }
    }
  }

  return {};
}

std::vector<std::string> identify_client::parse_string_array(std::string_view array_str)
{
  std::vector<std::string> out;
  for (size_t i = 0; i < array_str.size();)
  {
    if (array_str[i] == '"')
    {
      ++i;
      std::string s;
      while (i < array_str.size() && array_str[i] != '"')
      {
        if (array_str[i] == '\\' && i + 1 < array_str.size())
        {
          s += array_str[i + 1];
          i += 2;
        }
        else
        {
          s += array_str[i++];
        }
      }
      out.push_back(std::move(s));
      if (i < array_str.size())
      {
        ++i;
      }
    }
    else
    {
      ++i;
    }
  }
  return out;
}

void identify_client::handle_line(std::string_view line)
{
  std::string type = find_json_value(line, "type");
  if (type == "peers")
  {
    auto hashes = parse_string_array(find_json_value(line, "hashes"));
    if (m_peers_callback)
    {
      m_peers_callback(hashes);
    }
  }
  else if (type == "chat")
  {
    std::string from = find_json_value(line, "from");
    std::string msg = find_json_value(line, "msg");
    if (m_chat_callback)
    {
      m_chat_callback(from, msg);
    }
  }
}

}
