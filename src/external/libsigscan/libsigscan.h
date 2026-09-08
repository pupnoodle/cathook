#ifndef LIBSIGSCAN_H_
#define LIBSIGSCAN_H_ 1

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ESigscanPidType {
    SIGSCAN_PID_INVALID = -2,
    SIGSCAN_PID_SELF    = -1,
};

typedef struct SigscanModuleBounds {
    void* start;
    void* end;
    struct SigscanModuleBounds* next;
} SigscanModuleBounds;

SigscanModuleBounds* sigscan_get_module_bounds(int pid, const char* regex);

void sigscan_free_module_bounds(SigscanModuleBounds* bounds);

void* sigscan_pid_module(int pid, const char* regex, const char* ida_pattern);

static inline void* sigscan_pid(int pid, const char* ida_pattern) {
    return sigscan_pid_module(pid, NULL, ida_pattern);
}

static inline void* sigscan_module(const char* regex, const char* ida_pattern) {
    return sigscan_pid_module(SIGSCAN_PID_SELF, regex, ida_pattern);
}

static inline void* sigscan(const char* ida_pattern) {
    return sigscan_pid_module(SIGSCAN_PID_SELF, NULL, ida_pattern);
}

#ifdef __cplusplus
}
#endif

#endif
