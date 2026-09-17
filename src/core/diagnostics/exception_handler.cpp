/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/diagnostics/exception_handler.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "exception_handler.hpp"
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <ucontext.h>
#include <unistd.h>

namespace cathook::core
{

namespace
{

volatile sig_atomic_t s_log_fd{ -1 };
volatile sig_atomic_t s_in_handler{ 0 };
alignas(16) static char s_alt_stack[65536]{};
std::string s_log_file_path{};
std::string s_fallback_log_file_path{};

struct signal_state
{
    std::array<struct sigaction, 9> previous_actions{};
    bool installed{};
};

signal_state& state()
{
    static signal_state instance{};
    return instance;
}

void write_bytes(const int fd, const char* data, std::size_t length)
{
    if (fd < 0 || data == nullptr)
    {
        return;
    }

    while (length > 0)
    {
        const ssize_t write_result{ ::write(fd, data, length) };
        if (write_result > 0)
        {
            data += write_result;
            length -= static_cast<std::size_t>(write_result);
        }
        else if (write_result < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            break;
        }
    }
}

void write_literal(const int fd, const char* const text)
{
    if (fd < 0 || text == nullptr)
    {
        return;
    }

    std::size_t length{};
    while (text[length] != '\0')
    {
        ++length;
    }

    write_bytes(fd, text, length);
}

void write_unsigned_decimal(const int fd, std::uint64_t value)
{
    char buffer[32]{};
    std::size_t index{ std::size(buffer) };

    do
    {
        buffer[--index] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (value != 0 && index > 0);

    write_bytes(fd, buffer + index, std::size(buffer) - index);
}

void write_signed_decimal(const int fd, const std::int64_t value)
{
    std::uint64_t magnitude{};
    if (value < 0)
    {
        write_literal(fd, "-");
        magnitude = static_cast<std::uint64_t>(-(value + 1)) + 1;
    }
    else
    {
        magnitude = static_cast<std::uint64_t>(value);
    }

    write_unsigned_decimal(fd, magnitude);
}

void write_hex(const int fd, const std::uint64_t value)
{
    char buffer[18]{ '0', 'x' };
    constexpr char k_hex_digits[]{ "0123456789abcdef" };

    for (int index{}; index < 16; ++index)
    {
        const int shift{ (15 - index) * 4 };
        buffer[2 + index] = k_hex_digits[(value >> shift) & 0xF];
    }

    write_bytes(fd, buffer, sizeof(buffer));
}

void write_register(const int fd, const char* const name, const std::uint64_t value)
{
    write_literal(fd, name);
    write_literal(fd, "=");
    write_hex(fd, value);
    write_literal(fd, "\n");
}

int open_exception_log()
{
    int fd{ static_cast<int>(s_log_fd) };
    if (fd >= 0)
    {
        return fd;
    }

    if (!s_log_file_path.empty())
    {
        fd = ::open(
            s_log_file_path.c_str(),
            O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC,
            0644);
    }

    if (fd < 0 && !s_fallback_log_file_path.empty())
    {
        fd = ::open(
            s_fallback_log_file_path.c_str(),
            O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC,
            0644);
    }

    if (fd < 0)
    {
        return -1;
    }

    if (s_log_fd >= 0)
    {
        ::close(fd);
        return static_cast<int>(s_log_fd);
    }

    s_log_fd = static_cast<sig_atomic_t>(fd);
    return fd;
}

void signal_handler(const int signal_number, siginfo_t* const info, void* const context_ptr)
{
    if (s_in_handler != 0)
    {
        ::_exit(128 + signal_number);
    }
    s_in_handler = 1;

    const int fd{ static_cast<int>(s_log_fd) };
    if (fd >= 0)
    {
        write_literal(fd, "\n================ crash ================\n");
        write_literal(fd, "signal=");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(signal_number));
        write_literal(fd, " pid=");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(::getpid()));
        write_literal(fd, "\n");

        if (info)
        {
            write_literal(fd, "code=");
            write_signed_decimal(fd, static_cast<std::int64_t>(info->si_code));
            write_literal(fd, " fault_address=");
            write_hex(fd, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info->si_addr)));
            write_literal(fd, "\n");
        }

#if defined(__x86_64__)
        if (context_ptr != nullptr)
        {
            const auto& registers{ static_cast<ucontext_t*>(context_ptr)->uc_mcontext.gregs };
            write_register(fd, "rip", static_cast<std::uint64_t>(registers[REG_RIP]));
            write_register(fd, "rsp", static_cast<std::uint64_t>(registers[REG_RSP]));
            write_register(fd, "rbp", static_cast<std::uint64_t>(registers[REG_RBP]));
        }
#elif defined(__i386__)
        if (context_ptr != nullptr)
        {
            const auto& registers{ static_cast<ucontext_t*>(context_ptr)->uc_mcontext.gregs };
            write_register(fd, "eip", static_cast<std::uint64_t>(registers[REG_EIP]));
            write_register(fd, "esp", static_cast<std::uint64_t>(registers[REG_ESP]));
            write_register(fd, "ebp", static_cast<std::uint64_t>(registers[REG_EBP]));
        }
#endif

        write_literal(fd, "\n=======================================\n");
        static_cast<void>(::fsync(fd));
    }

    ::_exit(128 + signal_number);
}

}

void exception_handler::install(const std::filesystem::path& log_file_path)
{
    auto& handler_state{ state() };
    if (handler_state.installed)
    {
        return;
    }

    std::error_code error{};
    std::filesystem::create_directories(log_file_path.parent_path(), error);

    s_log_file_path = log_file_path.string();
    s_fallback_log_file_path = "/tmp/cathook-exception.log";
    s_log_fd = -1;
    s_in_handler = 0;
    open_exception_log();

    stack_t alt_stack{};
    alt_stack.ss_sp = s_alt_stack;
    alt_stack.ss_size = sizeof(s_alt_stack);
    alt_stack.ss_flags = 0;
    ::sigaltstack(&alt_stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = &signal_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    ::sigfillset(&action.sa_mask);

    constexpr int k_signals[]{
        SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE,
        SIGTRAP, SIGSYS, SIGXCPU, SIGXFSZ
    };
    for (std::size_t index{}; index < std::size(k_signals); ++index)
    {
        if (::sigaction(k_signals[index], &action, &handler_state.previous_actions[index]) != 0)
        {
            for (std::size_t restore_index{}; restore_index < index; ++restore_index)
            {
                static_cast<void>(::sigaction(k_signals[restore_index], &handler_state.previous_actions[restore_index], nullptr));
            }

            const int installed_fd{ static_cast<int>(s_log_fd) };
            if (installed_fd >= 0)
            {
                ::close(installed_fd);
                s_log_fd = -1;
            }
            return;
        }
    }

    handler_state.installed = true;

}

void exception_handler::uninstall()
{
    auto& handler_state{ state() };
    if (!handler_state.installed)
    {
        return;
    }

    constexpr int k_signals[]{
        SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE,
        SIGTRAP, SIGSYS, SIGXCPU, SIGXFSZ
    };
    for (std::size_t index{}; index < std::size(k_signals); ++index)
    {
        static_cast<void>(::sigaction(k_signals[index], &handler_state.previous_actions[index], nullptr));
    }

    const int fd{ static_cast<int>(s_log_fd) };
    s_log_fd = -1;
    if (fd >= 0)
    {
        ::close(fd);
    }

    stack_t disable_stack{};
    disable_stack.ss_flags = SS_DISABLE;
    ::sigaltstack(&disable_stack, nullptr);
    s_in_handler = 0;

    handler_state.installed = false;
}

}
