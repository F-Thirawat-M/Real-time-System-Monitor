#ifndef MINI_HTOP_MONITOR_H
#define MINI_HTOP_MONITOR_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define MINI_MAX_CORES 256
#define MINI_NAME_LEN 128
#define MINI_USER_LEN 32

typedef struct {
    uint64_t total;
    uint64_t idle;
} CpuTicks;

typedef struct {
    pid_t pid;
    uint64_t start_time;
    uint64_t cpu_ticks;
    long rss_pages;
    char name[MINI_NAME_LEN];
    char user[MINI_USER_LEN];
    char state;
    double cpu_percent;
    double memory_percent;
} Process;

typedef struct {
    CpuTicks cpu;
    CpuTicks cores[MINI_MAX_CORES];
    size_t core_count;
    double cpu_percent;
    double core_percent[MINI_MAX_CORES];
    uint64_t memory_total;
    uint64_t memory_used;
    uint64_t swap_total;
    uint64_t swap_used;
    uint64_t disk_total;
    uint64_t disk_used;
    Process *processes;
    size_t process_count;
} Snapshot;

int monitor_collect(Snapshot *current, const Snapshot *previous);
void monitor_free(Snapshot *snapshot);
int monitor_process_identity(pid_t pid, uint64_t *start_time);
int monitor_send_signal(pid_t pid, uint64_t expected_start_time, int signal_number);

#endif
