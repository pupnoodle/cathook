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
#include <cstdlib>
#include <cstdint>
#include <cxxabi.h>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
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

struct module_mapping
{
    std::uintptr_t start{};
    std::uintptr_t end{};
    std::uintptr_t offset{};
    char path[512]{};
};

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

void write_byte_hex(const int fd, const std::uint8_t value)
{
    constexpr char k_hex_digits[]{ "0123456789abcdef" };
    char buffer[2]{};
    buffer[0] = k_hex_digits[(value >> 4) & 0xF];
    buffer[1] = k_hex_digits[value & 0xF];
    write_bytes(fd, buffer, sizeof(buffer));
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

void write_symbol_name(const int fd, const char* const name)
{
    if (name == nullptr || name[0] == '\0')
    {
        write_literal(fd, "unknown");
        return;
    }

    int status{};
    char* const demangled_name{ abi::__cxa_demangle(name, nullptr, nullptr, &status) };
    if (status == 0 && demangled_name != nullptr)
    {
        write_literal(fd, demangled_name);
        std::free(demangled_name);
        return;
    }

    if (demangled_name != nullptr)
    {
        std::free(demangled_name);
    }

    write_literal(fd, name);
}

void write_module_offset(const int fd, const module_mapping& mapping, const std::uintptr_t address)
{
    const std::uintptr_t base{ mapping.start - mapping.offset };
    const std::uintptr_t relative_address{ address - base };
    char relative_text[32]{};
    format_hex(relative_text, sizeof(relative_text), relative_address);

    write_literal(fd, mapping.path);
    write_literal(fd, "+");
    write_literal(fd, relative_text);
}

void write_symbolized_address(const int fd, const int index, const std::uintptr_t address)
{
    module_mapping mapping{};
    if (!find_mapping_for_address(address, false, mapping))
    {
        write_literal(fd, "#");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(index));
        write_literal(fd, " ");
        write_hex(fd, address);
        write_literal(fd, " unmapped\n");
        return;
    }

    const std::uintptr_t base{ mapping.start - mapping.offset };
    const std::uintptr_t relative_address{ address - base };
    char absolute_text[32]{};
    char relative_text[32]{};
    format_hex(absolute_text, sizeof(absolute_text), address);
    format_hex(relative_text, sizeof(relative_text), relative_address);
    Dl_info symbol_info{};
    const bool has_symbol_info{ ::dladdr(reinterpret_cast<void*>(address), &symbol_info) != 0 };

    write_literal(fd, "#");
    write_unsigned_decimal(fd, static_cast<std::uint64_t>(index));
    write_literal(fd, " ");
    write_literal(fd, absolute_text);
    write_literal(fd, " ");
    write_module_offset(fd, mapping, address);
    write_literal(fd, " function=");
    if (has_symbol_info && symbol_info.dli_sname != nullptr)
    {
        write_symbol_name(fd, symbol_info.dli_sname);
        if (symbol_info.dli_saddr != nullptr)
        {
            const std::uintptr_t symbol_address{ reinterpret_cast<std::uintptr_t>(symbol_info.dli_saddr) };
            const std::uintptr_t symbol_offset{ address - symbol_address };
            char symbol_offset_text[32]{};
            format_hex(symbol_offset_text, sizeof(symbol_offset_text), symbol_offset);
            write_literal(fd, "+");
            write_literal(fd, symbol_offset_text);
        }
    }
    else
    {
        write_literal(fd, "unknown");
    }
    write_literal(fd, " addr2line=");

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

const char* signal_name(const int signal_number)
{
    switch (signal_number)
    {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGTRAP: return "SIGTRAP";
    case SIGSYS: return "SIGSYS";
    case SIGXCPU: return "SIGXCPU";
    case SIGXFSZ: return "SIGXFSZ";
    case SIGTERM: return "SIGTERM";
    default: return "unknown";
    }
}

const char* signal_code_name(const int signal_number, const int code)
{
    switch (signal_number)
    {
    case SIGSEGV:
        switch (code)
        {
        case SEGV_MAPERR: return "SEGV_MAPERR";
        case SEGV_ACCERR: return "SEGV_ACCERR";
#if defined(SEGV_BNDERR)

        case SEGV_BNDERR: return "SEGV_BNDERR";
#endif
#if defined(SEGV_PKUERR)

        case SEGV_PKUERR: return "SEGV_PKUERR";
#endif

        default: return "unknown";
        }
    case SIGBUS:
        switch (code)
        {
        case BUS_ADRALN: return "BUS_ADRALN";
        case BUS_ADRERR: return "BUS_ADRERR";
        case BUS_OBJERR: return "BUS_OBJERR";
        default: return "unknown";
        }
    case SIGILL:
        switch (code)
        {
        case ILL_ILLOPC: return "ILL_ILLOPC";
        case ILL_ILLOPN: return "ILL_ILLOPN";
        case ILL_ILLADR: return "ILL_ILLADR";
        case ILL_ILLTRP: return "ILL_ILLTRP";
        case ILL_PRVOPC: return "ILL_PRVOPC";
        case ILL_PRVREG: return "ILL_PRVREG";
        case ILL_COPROC: return "ILL_COPROC";
        case ILL_BADSTK: return "ILL_BADSTK";
        default: return "unknown";
        }
    case SIGFPE:
        switch (code)
        {
        case FPE_INTDIV: return "FPE_INTDIV";
        case FPE_INTOVF: return "FPE_INTOVF";
        case FPE_FLTDIV: return "FPE_FLTDIV";
        case FPE_FLTOVF: return "FPE_FLTOVF";
        case FPE_FLTUND: return "FPE_FLTUND";
        case FPE_FLTRES: return "FPE_FLTRES";
        case FPE_FLTINV: return "FPE_FLTINV";
        case FPE_FLTSUB: return "FPE_FLTSUB";
        default: return "unknown";
        }
    default:
        return "unknown";
    }
}
#if defined(__x86_64__)

struct decoded_memory_operand
{
    int base_register{ -1 };
    std::int64_t displacement{};
    std::size_t instruction_length{};
    bool valid{};
    bool rip_relative{};
};

const char* register_name_by_x86_index(const int index)
{
    constexpr const char* k_register_names[]
    {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };

    if (index < 0 || static_cast<std::size_t>(index) >= std::size(k_register_names))
    {
        return "unknown";
    }

    return k_register_names[index];
}

std::uintptr_t register_value_by_x86_index(ucontext_t* const context, const int index)
{
    if (context == nullptr)
    {
        return 0;
    }

    const greg_t* const registers{ context->uc_mcontext.gregs };
    switch (index)
    {
    case 0: return static_cast<std::uintptr_t>(registers[REG_RAX]);
    case 1: return static_cast<std::uintptr_t>(registers[REG_RCX]);
    case 2: return static_cast<std::uintptr_t>(registers[REG_RDX]);
    case 3: return static_cast<std::uintptr_t>(registers[REG_RBX]);
    case 4: return static_cast<std::uintptr_t>(registers[REG_RSP]);
    case 5: return static_cast<std::uintptr_t>(registers[REG_RBP]);
    case 6: return static_cast<std::uintptr_t>(registers[REG_RSI]);
    case 7: return static_cast<std::uintptr_t>(registers[REG_RDI]);
    case 8: return static_cast<std::uintptr_t>(registers[REG_R8]);
    case 9: return static_cast<std::uintptr_t>(registers[REG_R9]);
    case 10: return static_cast<std::uintptr_t>(registers[REG_R10]);
    case 11: return static_cast<std::uintptr_t>(registers[REG_R11]);
    case 12: return static_cast<std::uintptr_t>(registers[REG_R12]);
    case 13: return static_cast<std::uintptr_t>(registers[REG_R13]);
    case 14: return static_cast<std::uintptr_t>(registers[REG_R14]);
    case 15: return static_cast<std::uintptr_t>(registers[REG_R15]);
    default: return 0;
    }
}

std::int32_t read_i32_le(const std::uint8_t* const bytes)
{
    const std::uint32_t value
    {
        static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1]) << 8)
            | (static_cast<std::uint32_t>(bytes[2]) << 16)
            | (static_cast<std::uint32_t>(bytes[3]) << 24)
    };
    return static_cast<std::int32_t>(value);
}

std::size_t read_instruction_bytes(
    const std::uintptr_t address,
    std::uint8_t* const buffer,
    const std::size_t buffer_size)
{
    if (address == 0 || buffer == nullptr || buffer_size == 0)
    {
        return 0;
    }

    module_mapping mapping{};
    if (!find_mapping_for_address(address, true, mapping) || address >= mapping.end)
    {
        return 0;
    }

    const std::size_t remaining{ static_cast<std::size_t>(mapping.end - address) };
    const std::size_t byte_count{ remaining < buffer_size ? remaining : buffer_size };
    std::memcpy(buffer, reinterpret_cast<const void*>(address), byte_count);
    return byte_count;
}

bool opcode_uses_modrm_memory_operand(const std::uint8_t opcode)
{
    switch (opcode)
    {
    case 0x88:
    case 0x89:
    case 0x8A:
    case 0x8B:
    case 0x8D:
    case 0xFF:
        return true;
    default:
        return false;
    }
}

decoded_memory_operand decode_memory_operand_x86_64(const std::uint8_t* const bytes, const std::size_t byte_count)
{
    decoded_memory_operand result{};
    if (bytes == nullptr || byte_count < 2)
    {
        return result;
    }

    std::size_t index{};
    std::uint8_t rex{};
    while (index < byte_count && bytes[index] >= 0x40 && bytes[index] <= 0x4F)
    {
        rex = bytes[index++];
    }

    if (index >= byte_count)
    {
        return result;
    }

    const std::uint8_t opcode{ bytes[index++] };
    if (!opcode_uses_modrm_memory_operand(opcode) || index >= byte_count)
    {
        return result;
    }

    const std::uint8_t modrm{ bytes[index++] };
    const std::uint8_t mod{ static_cast<std::uint8_t>(modrm >> 6) };
    const std::uint8_t rm{ static_cast<std::uint8_t>(modrm & 0x7) };
    if (mod == 3)
    {
        return result;
    }

    const bool rex_b{ (rex & 0x1) != 0 };
    std::uint8_t base_selector{ rm };
    bool has_base{ true };

    if (rm == 4)
    {
        if (index >= byte_count)
        {
            return result;
        }

        const std::uint8_t sib{ bytes[index++] };
        base_selector = static_cast<std::uint8_t>(sib & 0x7);
        if (mod == 0 && base_selector == 5)
        {
            has_base = false;
        }
    }
    else if (mod == 0 && rm == 5)
    {
        has_base = false;
        result.rip_relative = true;
    }

    if (has_base)
    {
        result.base_register = static_cast<int>(base_selector | (rex_b ? 8 : 0));
    }

    if (mod == 1)
    {
        if (index >= byte_count)
        {
            return decoded_memory_operand{};
        }

        result.displacement = static_cast<std::int8_t>(bytes[index++]);
    }
    else if (mod == 2 || !has_base)
    {
        if (index + 4 > byte_count)
        {
            return decoded_memory_operand{};
        }

        result.displacement = read_i32_le(bytes + index);
        index += 4;
    }

    result.instruction_length = index;
    result.valid = true;
    return result;
}

void write_signed_hex_expression_offset(const int fd, const std::int64_t displacement)
{
    char displacement_text[32]{};
    if (displacement < 0)
    {
        const std::uint64_t magnitude{ static_cast<std::uint64_t>(-(displacement + 1)) + 1 };
        format_hex(displacement_text, sizeof(displacement_text), magnitude);
        write_literal(fd, "-");
        write_literal(fd, displacement_text);
        return;
    }

    format_hex(displacement_text, sizeof(displacement_text), static_cast<std::uint64_t>(displacement));
    write_literal(fd, "+");
    write_literal(fd, displacement_text);
}

void write_operand_expression(const int fd, const decoded_memory_operand& operand)
{
    if (!operand.valid)
    {
        return;
    }

    write_literal(fd, "faulting_operand=[");
    if (operand.rip_relative)
    {
        write_literal(fd, "rip");
    }
    else if (operand.base_register >= 0)
    {
        write_literal(fd, register_name_by_x86_index(operand.base_register));
    }

    if (operand.displacement != 0 || (operand.base_register < 0 && !operand.rip_relative))
    {
        write_signed_hex_expression_offset(fd, operand.displacement);
    }

    write_literal(fd, "]\n");
}

void write_operand_address_match(
    const int fd,
    ucontext_t* const context,
    const decoded_memory_operand& operand,
    const std::uintptr_t instruction_pointer,
    const std::uintptr_t fault_address)
{
    if (!operand.valid || context == nullptr)
    {
        return;
    }

    std::uintptr_t base_value{};
    if (operand.rip_relative)
    {
        base_value = instruction_pointer + operand.instruction_length;
    }
    else if (operand.base_register >= 0)
    {
        base_value = register_value_by_x86_index(context, operand.base_register);
    }
    else
    {
        base_value = 0;
    }

    const std::uintptr_t expected_address
    {
        operand.displacement < 0
            ? base_value - (static_cast<std::uintptr_t>(-(operand.displacement + 1)) + 1)
            : base_value + static_cast<std::uintptr_t>(operand.displacement)
    };

    write_literal(fd, "faulting_address_expression=");
    if (operand.rip_relative)
    {
        write_literal(fd, "rip");
    }
    else if (operand.base_register >= 0)
    {
        write_literal(fd, register_name_by_x86_index(operand.base_register));
    }
    else
    {
        write_hex(fd, 0);
    }
    write_signed_hex_expression_offset(fd, operand.displacement);
    write_literal(fd, " -> ");
    write_hex(fd, expected_address);
    write_literal(fd, expected_address == fault_address ? " match=yes\n" : " match=no\n");
}

void write_access_kind(const int fd, const int signal_number, siginfo_t* const info, ucontext_t* const context)
{
    write_literal(fd, "access=");
    if (context == nullptr || info == nullptr || (signal_number != SIGSEGV && signal_number != SIGBUS))
    {
        write_literal(fd, "unknown\n");
        return;
    }

    const greg_t* const registers{ context->uc_mcontext.gregs };
    const std::uintptr_t instruction_pointer{ static_cast<std::uintptr_t>(registers[REG_RIP]) };
    const std::uintptr_t fault_address{ reinterpret_cast<std::uintptr_t>(info->si_addr) };
    const std::uintptr_t error_code{ static_cast<std::uintptr_t>(registers[REG_ERR]) };

    if (fault_address == instruction_pointer || (error_code & 0x10U) != 0)
    {
        write_literal(fd, "execute\n");
    }
    else if ((error_code & 0x2U) != 0)
    {
        write_literal(fd, "write\n");
    }
    else
    {
        write_literal(fd, "read\n");
    }
}

void dump_instruction_context(const int fd, siginfo_t* const info, ucontext_t* const context)
{
    if (context == nullptr)
    {
        return;
    }

    const greg_t* const registers{ context->uc_mcontext.gregs };
    const std::uintptr_t instruction_pointer{ static_cast<std::uintptr_t>(registers[REG_RIP]) };
    const std::uintptr_t fault_address{ info != nullptr ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0 };
    std::uint8_t bytes[16]{};
    const std::size_t byte_count{ read_instruction_bytes(instruction_pointer, bytes, sizeof(bytes)) };
    if (byte_count == 0)
    {
        write_literal(fd, "instruction_bytes=unavailable\n");
        return;
    }

    write_literal(fd, "instruction_bytes=");
    for (std::size_t index{}; index < byte_count; ++index)
    {
        if (index != 0)
        {
            write_literal(fd, " ");
        }
        write_byte_hex(fd, bytes[index]);
    }
    write_literal(fd, "\n");

    const decoded_memory_operand operand{ decode_memory_operand_x86_64(bytes, byte_count) };
    write_operand_expression(fd, operand);
    write_operand_address_match(fd, context, operand, instruction_pointer, fault_address);
}
#endif

void dump_fault_context(const int fd, const int signal_number, siginfo_t* const info, ucontext_t* const context)
{
    write_literal(fd, "\ncause\n");
    write_literal(fd, "signal_name=");
    write_literal(fd, signal_name(signal_number));
    write_literal(fd, "\n");

    if (info != nullptr)
    {
        write_literal(fd, "code_name=");
        write_literal(fd, signal_code_name(signal_number, info->si_code));
        write_literal(fd, "\n");
    }
#if defined(__x86_64__)

    if (context != nullptr)
    {
        const greg_t* const registers{ context->uc_mcontext.gregs };
        const std::uintptr_t instruction_pointer{ static_cast<std::uintptr_t>(registers[REG_RIP]) };
        module_mapping instruction_mapping{};
        write_literal(fd, "instruction=");
        if (find_mapping_for_address(instruction_pointer, false, instruction_mapping))
        {
            write_module_offset(fd, instruction_mapping, instruction_pointer);
            write_literal(fd, "\n");
        }
        else
        {
            write_hex(fd, instruction_pointer);
            write_literal(fd, "\n");
        }
    }

    if (info != nullptr)
    {
        const std::uintptr_t fault_address{ reinterpret_cast<std::uintptr_t>(info->si_addr) };
        module_mapping fault_mapping{};
        write_literal(fd, "fault_address_mapping=");
        if (find_mapping_for_address(fault_address, false, fault_mapping))
        {
            write_module_offset(fd, fault_mapping, fault_address);
            write_literal(fd, "\n");
        }
        else
        {
            write_literal(fd, "unmapped\n");
        }

        write_literal(fd, "likely_null_deref=");
        write_literal(fd, fault_address < 0x1000 ? "yes\n" : "no\n");
    }

    write_access_kind(fd, signal_number, info, context);
    dump_instruction_context(fd, info, context);
#else

    static_cast<void>(context);
    if (info != nullptr)
    {
        const std::uintptr_t fault_address{ reinterpret_cast<std::uintptr_t>(info->si_addr) };
        write_literal(fd, "likely_null_deref=");
        write_literal(fd, fault_address < 0x1000 ? "yes\n" : "no\n");
    }
#endif

}

void dump_fault_frame(const int fd, ucontext_t* const context)
{
    write_literal(fd, "\nfault_frame\n");
    if (context == nullptr)
    {
        write_literal(fd, "unavailable\n");
        return;
    }
#if defined(__x86_64__)

    const greg_t* const registers{ context->uc_mcontext.gregs };
    write_symbolized_address(fd, 0, static_cast<std::uintptr_t>(registers[REG_RIP]));
#elif defined(__i386__)

    const greg_t* const registers{ context->uc_mcontext.gregs };
    write_symbolized_address(fd, 0, static_cast<std::uintptr_t>(registers[REG_EIP]));
#else

    write_literal(fd, "unsupported architecture\n");
#endif

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
    module_mapping stack_mapping{};
    if (!find_mapping_for_address(stack_pointer, false, stack_mapping))
    {
        return;
    }
    write_literal(fd, "\nstack_words\n");
    for (std::size_t index{}; index < k_stack_words; ++index)
    {
        const std::uintptr_t word_address = stack_pointer + index * sizeof(std::uintptr_t);
        if (word_address < stack_mapping.start || word_address + sizeof(std::uintptr_t) > stack_mapping.end)
        {
            break;
        }
        std::uintptr_t word_value{};
        __builtin_memcpy(&word_value, reinterpret_cast<const void*>(word_address), sizeof(word_value));
        write_literal(fd, "rsp+");
        write_unsigned_decimal(fd, static_cast<std::uint64_t>(index * sizeof(std::uintptr_t)));
        write_literal(fd, " ");
        write_hex(fd, static_cast<std::uint64_t>(word_value));
        write_literal(fd, "\n");
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

        [[maybe_unused]] const ssize_t write_result{ ::write(fd, buffer, static_cast<std::size_t>(read_size)) };
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
    write_literal(fd, "\nbacktrace\ndeferred to post-crash tool\n");
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
