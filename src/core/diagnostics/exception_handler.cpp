/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/diagnostics/exception_handler.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

//TODO
//IMPROVE
// - pupnoodle

#include "exception_handler.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <string>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

namespace cathook::core
{

namespace
{

std::atomic<int> s_log_fd{ -1 };

struct module_mapping
{
    std::uintptr_t start{};
    std::uintptr_t end{};
    std::uintptr_t offset{};
    char path[512]{};
};

struct signal_state
{
    std::array<struct sigaction, 4> previous_actions{};
    bool installed{};
};

signal_state& state()
{
    static signal_state instance{};
    return instance;
}

long current_thread_id()
{
    return static_cast<long>(::syscall(SYS_gettid));
}

void write_literal(const int fd, const char* const text)
{
    if (fd < 0 || !text)
    {
        return;
    }

    const std::size_t length{ std::strlen(text) };
    if (length == 0)
    {
        return;
    }

    static_cast<void>(::write(fd, text, length));
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

    static_cast<void>(::write(fd, buffer + index, std::size(buffer) - index));
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

    static_cast<void>(::write(fd, buffer, sizeof(buffer)));
}

void write_register(const int fd, const char* const name, const std::uint64_t value)
{
    write_literal(fd, name);
    write_literal(fd, "=");
    write_hex(fd, value);
    write_literal(fd, "\n");
}

std::size_t format_hex(char* const buffer, const std::size_t size, const std::uint64_t value)
{
    if (buffer == nullptr || size < 3)
    {
        return 0;
    }

    constexpr char k_hex_digits[]{ "0123456789abcdef" };
    char reversed[16]{};
    std::size_t count{};
    auto current = value;

    do
    {
        reversed[count++] = k_hex_digits[current & 0xF];
        current >>= 4;
    }
    while (current != 0 && count < std::size(reversed));

    std::size_t index{};
    buffer[index++] = '0';
    buffer[index++] = 'x';

    while (count > 0 && index + 1 < size)
    {
        buffer[index++] = reversed[--count];
    }

    buffer[index] = '\0';
    return index;
}

bool parse_hex_value(const char*& cursor, const char* const end, std::uintptr_t& value)
{
    value = 0;
    bool found_digit = false;

    while (cursor < end)
    {
        const char c = *cursor;
        unsigned int digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = static_cast<unsigned int>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = static_cast<unsigned int>(c - 'a' + 10);
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = static_cast<unsigned int>(c - 'A' + 10);
        }
        else
        {
            break;
        }

        value = (value << 4) | digit;
        found_digit = true;
        ++cursor;
    }

    return found_digit;
}

void skip_spaces(const char*& cursor, const char* const end)
{
    while (cursor < end && *cursor == ' ')
    {
        ++cursor;
    }
}

bool parse_mapping_line(
    const char* const line,
    const std::size_t length,
    const std::uintptr_t address,
    const bool require_executable,
    module_mapping& mapping)
{
    const char* cursor = line;
    const char* const end = line + length;
    std::uintptr_t start{};
    std::uintptr_t stop{};
    std::uintptr_t offset{};

    if (!parse_hex_value(cursor, end, start) || cursor >= end || *cursor != '-')
    {
        return false;
    }
    ++cursor;

    if (!parse_hex_value(cursor, end, stop) || address < start || address >= stop)
    {
        return false;
    }

    skip_spaces(cursor, end);
    if (end - cursor < 4)
    {
        return false;
    }

    const bool executable = cursor[2] == 'x';
    if (require_executable && !executable)
    {
        return false;
    }
    cursor += 4;

    skip_spaces(cursor, end);
    if (!parse_hex_value(cursor, end, offset))
    {
        return false;
    }

    const char* path = nullptr;
    for (const char* it = cursor; it < end; ++it)
    {
        if (*it == '/')
        {
            path = it;
            break;
        }
    }

    if (path == nullptr)
    {
        return false;
    }

    mapping.start = start;
    mapping.end = stop;
    mapping.offset = offset;

    std::size_t path_length = static_cast<std::size_t>(end - path);
    while (path_length > 0 && (path[path_length - 1] == '\n' || path[path_length - 1] == '\r'))
    {
        --path_length;
    }
    if (path_length >= std::size(mapping.path))
    {
        path_length = std::size(mapping.path) - 1;
    }

    std::memcpy(mapping.path, path, path_length);
    mapping.path[path_length] = '\0';
    return true;
}

bool find_mapping_for_address(
    const std::uintptr_t address,
    const bool require_executable,
    module_mapping& mapping)
{
    const int maps_fd{ ::open("/proc/self/maps", O_RDONLY) };
    if (maps_fd < 0)
    {
        return false;
    }

    char read_buffer[4096]{};
    char line_buffer[1024]{};
    std::size_t line_length{};
    bool found = false;

    while (!found)
    {
        const ssize_t read_size{ ::read(maps_fd, read_buffer, sizeof(read_buffer)) };
        if (read_size <= 0)
        {
            break;
        }

        for (ssize_t index{}; index < read_size; ++index)
        {
            const char c = read_buffer[index];
            if (line_length + 1 < sizeof(line_buffer))
            {
                line_buffer[line_length++] = c;
            }

            if (c == '\n')
            {
                found = parse_mapping_line(line_buffer, line_length, address, require_executable, mapping);
                line_length = 0;
                if (found)
                {
                    break;
                }
            }
        }
    }

    if (!found && line_length > 0)
    {
        found = parse_mapping_line(line_buffer, line_length, address, require_executable, mapping);
    }

    ::close(maps_fd);
    return found;
}

bool run_addr2line(const int fd, const char* const path, const std::uintptr_t relative_address)
{
    char address_text[32]{};
    format_hex(address_text, sizeof(address_text), relative_address);

    const pid_t child = ::fork();
    if (child < 0)
    {
        return false;
    }

    if (child == 0)
    {
        static_cast<void>(::dup2(fd, STDOUT_FILENO));
        static_cast<void>(::dup2(fd, STDERR_FILENO));
        ::execlp("addr2line", "addr2line", "-e", path, "-f", "-C", "-p", address_text, static_cast<char*>(nullptr));
        ::_exit(127);
    }

    int status{};
    while (::waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return false;
        }
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void write_symbolized_address(const int fd, const int index, const std::uintptr_t address)
{
    module_mapping mapping{};
    if (!find_mapping_for_address(address, false, mapping))
    {
        return;
    }

    const auto base = mapping.start - mapping.offset;
    const auto relative_address = address - base;
    char absolute_text[32]{};
    char relative_text[32]{};
    format_hex(absolute_text, sizeof(absolute_text), address);
    format_hex(relative_text, sizeof(relative_text), relative_address);

    write_literal(fd, "#");
    write_unsigned_decimal(fd, static_cast<std::uint64_t>(index));
    write_literal(fd, " ");
    write_literal(fd, absolute_text);
    write_literal(fd, " ");
    write_literal(fd, mapping.path);
    write_literal(fd, "+");
    write_literal(fd, relative_text);
    write_literal(fd, ": ");

    if (!run_addr2line(fd, mapping.path, relative_address))
    {
        write_literal(fd, "addr2line unavailable\n");
    }
}

void dump_symbolized_frames(const int fd, void* const* const frames, const int frame_count)
{
    write_literal(fd, "\nsymbolized_backtrace\n");
    if (frames == nullptr || frame_count <= 0)
    {
        write_literal(fd, "unavailable\n");
        return;
    }

    for (int index{}; index < frame_count; ++index)
    {
        write_symbolized_address(fd, index, reinterpret_cast<std::uintptr_t>(frames[index]));
    }
}

void dump_stack_symbol_candidates(const int fd, ucontext_t* const context)
{
#if defined(__x86_64__)
    if (context == nullptr)
    {
        return;
    }

    const auto& registers{ context->uc_mcontext.gregs };
    const auto stack_pointer = static_cast<std::uintptr_t>(registers[REG_RSP]);
    if (stack_pointer == 0)
    {
        return;
    }

    write_literal(fd, "\nstack_executable_candidates\n");
    constexpr std::size_t k_stack_words = 48;
    const auto* const stack_words = reinterpret_cast<const std::uintptr_t*>(stack_pointer);
    int candidate_index{};

    for (std::size_t index{}; index < k_stack_words; ++index)
    {
        const std::uintptr_t candidate = stack_words[index];
        module_mapping mapping{};
        if (!find_mapping_for_address(candidate, true, mapping))
        {
            continue;
        }

        write_literal(fd, "rsp+");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(index * sizeof(std::uintptr_t)));
        write_literal(fd, " ");
        write_symbolized_address(fd, candidate_index++, candidate);
    }

    if (candidate_index == 0)
    {
        write_literal(fd, "none\n");
    }
#else
    static_cast<void>(fd);
    static_cast<void>(context);
#endif
}

void dump_memory_map(const int fd)
{
    const int maps_fd{ ::open("/proc/self/maps", O_RDONLY) };
    if (maps_fd < 0)
    {
        write_literal(fd, "maps: unavailable\n");
        return;
    }

    write_literal(fd, "\n/proc/self/maps\n");

    char buffer[4096]{};
    while (true)
    {
        const ssize_t read_size{ ::read(maps_fd, buffer, sizeof(buffer)) };
        if (read_size <= 0)
        {
            break;
        }

        static_cast<void>(::write(fd, buffer, static_cast<std::size_t>(read_size)));
    }

    ::close(maps_fd);
}

void dump_registers(const int fd, ucontext_t* const context)
{
    if (!context)
    {
        write_literal(fd, "registers: unavailable\n");
        return;
    }

#if defined(__x86_64__)
    const auto& registers{ context->uc_mcontext.gregs };
    write_literal(fd, "\nregisters\n");
    write_register(fd, "rip", static_cast<std::uint64_t>(registers[REG_RIP]));
    write_register(fd, "rsp", static_cast<std::uint64_t>(registers[REG_RSP]));
    write_register(fd, "rbp", static_cast<std::uint64_t>(registers[REG_RBP]));
    write_register(fd, "rax", static_cast<std::uint64_t>(registers[REG_RAX]));
    write_register(fd, "rbx", static_cast<std::uint64_t>(registers[REG_RBX]));
    write_register(fd, "rcx", static_cast<std::uint64_t>(registers[REG_RCX]));
    write_register(fd, "rdx", static_cast<std::uint64_t>(registers[REG_RDX]));
    write_register(fd, "rsi", static_cast<std::uint64_t>(registers[REG_RSI]));
    write_register(fd, "rdi", static_cast<std::uint64_t>(registers[REG_RDI]));
    write_register(fd, "r8", static_cast<std::uint64_t>(registers[REG_R8]));
    write_register(fd, "r9", static_cast<std::uint64_t>(registers[REG_R9]));
    write_register(fd, "r10", static_cast<std::uint64_t>(registers[REG_R10]));
    write_register(fd, "r11", static_cast<std::uint64_t>(registers[REG_R11]));
    write_register(fd, "r12", static_cast<std::uint64_t>(registers[REG_R12]));
    write_register(fd, "r13", static_cast<std::uint64_t>(registers[REG_R13]));
    write_register(fd, "r14", static_cast<std::uint64_t>(registers[REG_R14]));
    write_register(fd, "r15", static_cast<std::uint64_t>(registers[REG_R15]));
#elif defined(__i386__)
    const auto& registers{ context->uc_mcontext.gregs };
    write_literal(fd, "\nregisters\n");
    write_register(fd, "eip", static_cast<std::uint64_t>(registers[REG_EIP]));
    write_register(fd, "esp", static_cast<std::uint64_t>(registers[REG_ESP]));
    write_register(fd, "ebp", static_cast<std::uint64_t>(registers[REG_EBP]));
    write_register(fd, "eax", static_cast<std::uint64_t>(registers[REG_EAX]));
    write_register(fd, "ebx", static_cast<std::uint64_t>(registers[REG_EBX]));
    write_register(fd, "ecx", static_cast<std::uint64_t>(registers[REG_ECX]));
    write_register(fd, "edx", static_cast<std::uint64_t>(registers[REG_EDX]));
    write_register(fd, "esi", static_cast<std::uint64_t>(registers[REG_ESI]));
    write_register(fd, "edi", static_cast<std::uint64_t>(registers[REG_EDI]));
#else
    write_literal(fd, "\nregisters: unsupported architecture\n");
#endif
}

void dump_backtrace(const int fd)
{
    write_literal(fd, "\nbacktrace\n");

    void* frames[64]{};
    const int frame_count{ ::backtrace(frames, static_cast<int>(std::size(frames))) };
    if (frame_count <= 0)
    {
        write_literal(fd, "unavailable\n");
        return;
    }

    ::backtrace_symbols_fd(frames, frame_count, fd);
    dump_symbolized_frames(fd, frames, frame_count);
}

void signal_handler(const int signal_number, siginfo_t* const info, void* const context_ptr)
{
    const int fd{ s_log_fd.load() };
    if (fd >= 0)
    {
        write_literal(fd, "\n================ crash ================\n");
        write_literal(fd, "signal=");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(signal_number));
        write_literal(fd, " pid=");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(::getpid()));
        write_literal(fd, " tid=");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(current_thread_id()));
        write_literal(fd, "\n");

        if (info)
        {
            write_literal(fd, "code=");
            write_unsigned_decimal(fd, static_cast<std::uint64_t>(info->si_code));
            write_literal(fd, " fault_address=");
            write_hex(fd, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info->si_addr)));
            write_literal(fd, "\n");
        }

        dump_registers(fd, static_cast<ucontext_t*>(context_ptr));
        dump_stack_symbol_candidates(fd, static_cast<ucontext_t*>(context_ptr));
        dump_backtrace(fd);
        dump_memory_map(fd);
        write_literal(fd, "\n=======================================\n");
        static_cast<void>(::fsync(fd));
    }

    ::_exit(128 + signal_number);
}

} // namespace

void exception_handler::install(const std::filesystem::path& log_file_path)
{
    auto& handler_state{ state() };
    if (handler_state.installed)
    {
        return;
    }

    std::error_code error{};
    std::filesystem::create_directories(log_file_path.parent_path(), error);

    const int fd
    {
        ::open(
            log_file_path.string().c_str(),
            O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC,
            0644)
    };
    if (fd < 0)
    {
        return;
    }

    struct sigaction action{};
    action.sa_sigaction = &signal_handler;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    ::sigemptyset(&action.sa_mask);

    constexpr int k_signals[]{ SIGSEGV, SIGABRT, SIGBUS, SIGILL };
    for (std::size_t index{}; index < std::size(k_signals); ++index)
    {
        if (::sigaction(k_signals[index], &action, &handler_state.previous_actions[index]) != 0)
        {
            for (std::size_t restore_index{}; restore_index < index; ++restore_index)
            {
                static_cast<void>(::sigaction(k_signals[restore_index], &handler_state.previous_actions[restore_index], nullptr));
            }

            ::close(fd);
            return;
        }
    }

    s_log_fd.store(fd);
    handler_state.installed = true;
}

void exception_handler::uninstall()
{
    auto& handler_state{ state() };
    if (!handler_state.installed)
    {
        return;
    }

    constexpr int k_signals[]{ SIGSEGV, SIGABRT, SIGBUS, SIGILL };
    for (std::size_t index{}; index < std::size(k_signals); ++index)
    {
        static_cast<void>(::sigaction(k_signals[index], &handler_state.previous_actions[index], nullptr));
    }

    const int fd{ s_log_fd.exchange(-1) };
    if (fd >= 0)
    {
        ::close(fd);
    }

    handler_state.installed = false;
}

} // namespace cathook::core
