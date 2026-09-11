#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/uio.h>

#include "libsigscan.h"

#ifdef LIBSIGSCAN_DEBUG
#define ERR(...)                         \
    do {                                 \
        fprintf(stderr, "libsigscan: "); \
        fprintf(stderr, __VA_ARGS__);    \
        fputc('\n', stderr);             \
    } while (0)
#else
#define ERR(...) \
    do {         \
    } while (0)
#endif

static bool does_module_match(const char* module_name, const char* path) {
    if (module_name == NULL)
        return true;

    if (path == NULL || path[0] == '\0')
        return false;

    const char* file_name = strrchr(path, '/');
    file_name = (file_name != NULL) ? file_name + 1 : path;

    if (strchr(module_name, '/') != NULL)
        return strstr(path, module_name) != NULL;

    return strcmp(file_name, module_name) == 0;
}

static void* read_mem(pid_t pid, void* dst, uintptr_t src, size_t sz) {
    if (pid == SIGSCAN_PID_INVALID) {
        ERR("read_mem: Got an invalid PID");
        return NULL;
    }

    if (pid == SIGSCAN_PID_SELF)
        return memcpy(dst, (void*)src, sz);

    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base  = dst;
    local[0].iov_len   = sz;
    remote[0].iov_base = (void*)src;
    remote[0].iov_len  = sz;

    const ssize_t read_size = process_vm_readv(pid, local, 1, remote, 1, 0);
    if (read_size != (ssize_t)sz) {
        ERR("Error reading address %p: %s", (void*)src, strerror(errno));
        return NULL;
    }

    return dst;
}

static int hex_digit(const char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool ida2code(
  const char* ida,
  uint8_t** code_ptr,
  char** mask_ptr,
  size_t* pattern_size) {
    if (ida == NULL || code_ptr == NULL || mask_ptr == NULL || pattern_size == NULL) {
        return false;
    }

    *code_ptr = NULL;
    *mask_ptr = NULL;
    *pattern_size = 0;

    size_t capacity = 16;
    uint8_t* code = (uint8_t*)malloc(capacity);
    char* mask = (char*)malloc(capacity + 1);
    if (code == NULL || mask == NULL) {
        free(code);
        free(mask);
        ERR("malloc() returned NULL");
        return false;
    }

    size_t fixed_bytes = 0;
    const char* cursor = ida;
    while (*cursor != '\0') {
        while (*cursor != '\0' && isspace((unsigned char)*cursor))
            cursor++;

        if (*cursor == '\0')
            break;

        if (*pattern_size == capacity) {
            if (capacity > (size_t)-1 / 2) {
                free(code);
                free(mask);
                return false;
            }

            const size_t new_capacity = capacity * 2;
            uint8_t* new_code = (uint8_t*)realloc(code, new_capacity);
            char* new_mask = (char*)realloc(mask, new_capacity + 1);
            if (new_code == NULL || new_mask == NULL) {
                free(new_code != NULL ? new_code : code);
                free(new_mask != NULL ? new_mask : mask);
                ERR("realloc() returned NULL");
                return false;
            }
            code = new_code;
            mask = new_mask;
            capacity = new_capacity;
        }

        if (*cursor == '?') {
            cursor++;
            if (*cursor == '?')
                cursor++;
            code[*pattern_size] = 0;
            mask[*pattern_size] = '?';
        } else {
            const int high = hex_digit(cursor[0]);
            const int low = hex_digit(cursor[1]);
            if (high < 0 || low < 0) {
                free(code);
                free(mask);
                ERR("invalid signature byte");
                return false;
            }
            code[*pattern_size] = (uint8_t)((high << 4) | low);
            mask[*pattern_size] = 'x';
            fixed_bytes++;
            cursor += 2;
        }

        (*pattern_size)++;
        while (*cursor != '\0' && isspace((unsigned char)*cursor))
            cursor++;
    }

    mask[*pattern_size] = '\0';
    if (*pattern_size == 0 || fixed_bytes == 0) {
        free(code);
        free(mask);
        ERR("signature must contain a fixed byte");
        return false;
    }

    *code_ptr = code;
    *mask_ptr = mask;
    return true;
}

static int do_scan(
  int pid,
  uintptr_t start,
  uintptr_t end,
  const uint8_t* pattern,
  const char* mask,
  size_t pattern_size,
  void** match) {
    if (!start || end <= start || pattern == NULL || mask == NULL || pattern_size == 0 || match == NULL) {
        ERR("do_scan() got invalid start or end pointers");
        return -1;
    }

    const uintptr_t region_size = end - start;
    if (pattern_size > region_size)
        return 0;

    const size_t chunk_size = 64 * 1024;
    const size_t overlap = pattern_size - 1;
    if (overlap > (size_t)-1 - chunk_size)
        return -1;

    uint8_t* buffer = (uint8_t*)malloc(chunk_size + overlap);
    if (buffer == NULL) {
        ERR("malloc() returned NULL");
        return -1;
    }

    *match = NULL;
    size_t match_count = 0;
    uintptr_t chunk_start = start;
    while (chunk_start < end) {
        const uintptr_t remaining = end - chunk_start;
        const size_t chunk_length = remaining > chunk_size ? chunk_size : (size_t)remaining;
        size_t read_size = chunk_length;
        if (remaining > chunk_length) {
            const uintptr_t after_chunk = remaining - chunk_length;
            const size_t extra = after_chunk > overlap ? overlap : (size_t)after_chunk;
            read_size += extra;
        }

        if (read_mem(pid, buffer, chunk_start, read_size) == NULL) {
            free(buffer);
            return -1;
        }

        const size_t scan_size = read_size >= pattern_size ? read_size - pattern_size + 1 : 0;
        for (size_t position = 0; position < scan_size; ++position) {
            bool matches = true;
            for (size_t pattern_index = 0; pattern_index < pattern_size; ++pattern_index) {
                if (mask[pattern_index] != '?' && buffer[position + pattern_index] != pattern[pattern_index]) {
                    matches = false;
                    break;
                }
            }

            if (matches) {
                if (match_count == 0)
                    *match = (void*)(chunk_start + position);
                if (++match_count > 1) {
                    free(buffer);
                    return 2;
                }
            }
        }

        if (remaining <= chunk_length)
            break;
        chunk_start += chunk_length;
    }

    free(buffer);
    return (int)match_count;
}

SigscanModuleBounds* sigscan_get_module_bounds(int pid, const char* regex) {

    char maps_path[50] = "/proc/self/maps";
    if (pid != SIGSCAN_PID_SELF)
        sprintf(maps_path, "/proc/%d/maps", pid);

    FILE* fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("Couldn't open /proc/%d/maps", pid);
        return NULL;
    }

    SigscanModuleBounds dummy;
    dummy.next               = NULL;
    SigscanModuleBounds* cur = &dummy;

    char line_buf[512];
    char rwxp[5];
    char pathname[512];

    while (fgets(line_buf, sizeof(line_buf), fd)) {
        pathname[0] = '\0';

        long unsigned start_num = 0, end_num = 0, offset = 0;
        const int fmt_match_num =
          sscanf(line_buf, "%lx-%lx %4s %lx %*x:%*x %*d %511[^\n]\n",
                 &start_num, &end_num, rwxp, &offset, pathname);

        if (fmt_match_num < 4) {
            ERR("sscanf() didn't match the minimum fields (4) for "
                "line:\n%s",
                line_buf);
            sigscan_free_module_bounds(dummy.next);
            dummy.next = NULL;
            goto done;
        }

        void* start_addr = (void*)start_num;
        void* end_addr   = (void*)end_num;

        const bool is_executable = rwxp[0] == 'r' && rwxp[2] == 'x';

        const bool name_matches =
          fmt_match_num == 5 && pathname[0] != '\0' && pathname[0] != '[' &&
          does_module_match(regex, pathname);

        if (is_executable && name_matches) {
            SigscanModuleBounds* next =
              (SigscanModuleBounds*)malloc(sizeof(SigscanModuleBounds));
            if (next == NULL) {
                ERR("malloc() returned NULL");
                sigscan_free_module_bounds(dummy.next);
                dummy.next = NULL;
                goto done;
            }
            cur->next = next;
            cur = cur->next;

            cur->start = start_addr;
            cur->end   = end_addr;
            cur->next  = NULL;
        }
    }

done:
    fclose(fd);
    return dummy.next;
}

void sigscan_free_module_bounds(SigscanModuleBounds* bounds) {
    SigscanModuleBounds* cur = bounds;
    while (cur != NULL) {
        SigscanModuleBounds* next = cur->next;
        free(cur);
        cur = next;
    }
}

void* sigscan_pid_module(int pid, const char* regex, const char* ida_pattern) {
    if (pid == SIGSCAN_PID_INVALID || ida_pattern == NULL)
        return NULL;

    uint8_t* pattern = NULL;
    char* mask = NULL;
    size_t pattern_size = 0;
    if (!ida2code(ida_pattern, &pattern, &mask, &pattern_size))
        return NULL;

    SigscanModuleBounds* bounds = sigscan_get_module_bounds(pid, regex);

    if (bounds == NULL) {
        ERR("Couldn't get any module bounds matching regex \"%s\" "
            "in /proc/%d/maps",
            regex != NULL ? regex : "<all>", pid);
        free(mask);
        free(pattern);
        return NULL;
    }

    void* ret = NULL;
    size_t match_count = 0;
    bool scan_failed = false;
    for (SigscanModuleBounds* cur = bounds; cur != NULL; cur = cur->next) {
        void* cur_result = NULL;
        const int cur_match_count = do_scan(
          pid,
          (uintptr_t)cur->start,
          (uintptr_t)cur->end,
          pattern,
          mask,
          pattern_size,
          &cur_result);
        if (cur_match_count < 0) {
            scan_failed = true;
            break;
        }
        if (cur_match_count > 0 && match_count == 0)
            ret = cur_result;
        match_count += (size_t)cur_match_count;
        if (match_count > 1)
            break;
    }

    sigscan_free_module_bounds(bounds);
    free(mask);
    free(pattern);

    if (scan_failed || match_count == 0)
        return NULL;
    if (match_count != 1) {
        ERR("signature is ambiguous; found %zu matches", match_count);
        return NULL;
    }

    return ret;
}
