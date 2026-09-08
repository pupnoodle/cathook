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

    if (process_vm_readv(pid, local, 1, remote, 1, 0) == -1) {
        ERR("Error reading address %p: %s", (void*)src, strerror(errno));
        return NULL;
    }

    return dst;
}

static uint8_t hex2byte(const char* hex) {
    int result = 0;

    while (isspace(*hex))
        hex++;

    for (int i = 0; i < 2 && hex[i] != '\0'; i++) {
        const char c = hex[i];

        if (c == ' ')
            break;

        uint8_t n = 0;
        if (c >= '0' && c <= '9')
            n = c - '0';
        else if (c >= 'a' && c <= 'f')
            n = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F')
            n = 10 + c - 'A';

        result <<= 4;
        result |= n & 0xF;
    }

    return result & 0xFF;
}

static void ida2code(const char* ida, uint8_t** code_ptr, char** mask_ptr) {

    size_t dst_sz = 100;
    *code_ptr     = (uint8_t*)malloc(dst_sz);
    *mask_ptr     = (char*)malloc(dst_sz);
    if (*code_ptr == NULL || *mask_ptr == NULL) {
        ERR("malloc() returned NULL");
        return;
    }

    while (isspace(*ida))
        ida++;

    size_t dst_i;
    for (dst_i = 0; *ida != '\0'; dst_i++) {
        if (dst_i >= dst_sz - 1) {
            dst_sz += 100;
            *code_ptr = (uint8_t*)realloc(*code_ptr, dst_sz);
            *mask_ptr = (char*)realloc(*mask_ptr, dst_sz);
        }

        if (*ida == '?') {
            (*code_ptr)[dst_i] = 0x00;
            (*mask_ptr)[dst_i] = '?';

#ifdef LIBSIGSCAN_MULTIPLE_WILDCARDS

            while (*ida == '?')
#endif
                ida++;
        } else {

            (*code_ptr)[dst_i] = hex2byte(ida);
            (*mask_ptr)[dst_i] = 'x';

            while (!isspace(*ida) && *ida != '\0')
                ida++;
        }

        while (isspace(*ida))
            ida++;
    }

    (*mask_ptr)[dst_i] = '\0';
}

static void* do_scan(int pid, uintptr_t start, uintptr_t end, const char* ida) {
    if (!start || !end) {
        ERR("do_scan() got invalid start or end pointers");
        return NULL;
    }

    uint8_t* pattern;
    char* mask;
    ida2code(ida, &pattern, &mask);

    size_t buf_sz = strlen(mask);
    uint8_t* buf  = (uint8_t*)malloc(buf_sz);
    if (read_mem(pid, buf, start, buf_sz) == NULL)
        return NULL;

    uintptr_t chunk_start = start;

    size_t pat_pos     = 0;
    size_t buf_pos     = 0;
    size_t match_start = 0;

    while ((chunk_start + buf_pos) < end && mask[pat_pos] != '\0') {
        if (buf_pos >= buf_sz) {
            if (match_start == buf_pos) {
                chunk_start += buf_sz;
                buf_pos = 0;
                pat_pos = 0;
            } else {
                chunk_start += match_start;
                buf_pos = pat_pos;
            }

            match_start = 0;

            if (chunk_start + buf_sz > end)
                buf_sz = end - chunk_start;

            if (read_mem(pid, buf, chunk_start, buf_sz) == NULL)
                return NULL;
        }

        if (mask[pat_pos] == '?' || buf[buf_pos] == pattern[pat_pos]) {
            buf_pos++;
            pat_pos++;
        } else {
            match_start++;
            buf_pos = match_start;
            pat_pos = 0;
        }
    }

    void* ret =
      (mask[pat_pos] == '\0') ? (void*)(chunk_start + match_start) : NULL;

    free(buf);
    free(mask);
    free(pattern);

    return ret;
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
            dummy.next = NULL;
            goto done;
        }

        void* start_addr = (void*)start_num;
        void* end_addr   = (void*)end_num;

        const bool is_readable = (rwxp[0] == 'r');

        const bool name_matches =
          fmt_match_num == 5 && pathname[0] != '\0' && pathname[0] != '[' &&
          does_module_match(regex, pathname);

        if (is_readable && name_matches) {
            if (cur != NULL && cur->end == start_addr && cur->end < end_addr) {

                cur->end = end_addr;
            } else {

                cur->next =
                  (SigscanModuleBounds*)malloc(sizeof(SigscanModuleBounds));
                cur = cur->next;

                cur->start = start_addr;
                cur->end   = end_addr;
                cur->next  = NULL;
            }
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
    if (pid == SIGSCAN_PID_INVALID)
        return NULL;

    SigscanModuleBounds* bounds = sigscan_get_module_bounds(pid, regex);

    if (bounds == NULL) {
        ERR("Couldn't get any module bounds matching regex \"%s\" "
            "in /proc/%d/maps",
            regex, pid);
        return NULL;
    }

    void* ret = NULL;
    for (SigscanModuleBounds* cur = bounds; cur != NULL; cur = cur->next) {
        void* cur_result =
          do_scan(pid, (uintptr_t)cur->start, (uintptr_t)cur->end, ida_pattern);

        if (cur_result != NULL) {
            ret = cur_result;
            break;
        }
    }

    sigscan_free_module_bounds(bounds);

    return ret;
}
