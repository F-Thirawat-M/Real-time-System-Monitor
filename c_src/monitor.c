#define _POSIX_C_SOURCE 200809L

#include "monitor.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static int read_file(const char *path, char *buffer, size_t capacity) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t count = read(fd, buffer, capacity - 1);
    int saved_errno = errno;
    close(fd);
    if (count < 0) {
        errno = saved_errno;
        return -1;
    }
    buffer[count] = '\0';
    return 0;
}

static int parse_cpu_line(const char *line, CpuTicks *ticks) {
    unsigned long long values[10] = {0};
    int parsed = sscanf(line,
        "%*s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
        &values[0], &values[1], &values[2], &values[3], &values[4],
        &values[5], &values[6], &values[7], &values[8], &values[9]);
    if (parsed < 4) return -1;
    ticks->total = 0;
    for (int i = 0; i < parsed; ++i) ticks->total += values[i];
    ticks->idle = values[3] + (parsed > 4 ? values[4] : 0);
    return 0;
}

static double usage_percent(CpuTicks current, CpuTicks previous) {
    if (current.total <= previous.total) return 0.0;
    uint64_t total_delta = current.total - previous.total;
    uint64_t idle_delta = current.idle >= previous.idle
        ? current.idle - previous.idle : 0;
    if (idle_delta > total_delta) idle_delta = total_delta;
    return 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
}

static int collect_cpu(Snapshot *current, const Snapshot *previous) {
    char buffer[32768];
    if (read_file("/proc/stat", buffer, sizeof buffer) < 0) return -1;
    char *save = NULL;
    for (char *line = strtok_r(buffer, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            if (parse_cpu_line(line, &current->cpu) < 0) return -1;
        } else if (strncmp(line, "cpu", 3) == 0 && isdigit((unsigned char)line[3])) {
            if (current->core_count >= MINI_MAX_CORES) continue;
            if (parse_cpu_line(line, &current->cores[current->core_count]) == 0)
                current->core_count++;
        }
    }
    if (current->cpu.total == 0) { errno = EINVAL; return -1; }
    if (previous) {
        current->cpu_percent = usage_percent(current->cpu, previous->cpu);
        size_t common = current->core_count < previous->core_count
            ? current->core_count : previous->core_count;
        for (size_t i = 0; i < common; ++i)
            current->core_percent[i] = usage_percent(current->cores[i], previous->cores[i]);
    }
    return 0;
}

static int collect_memory(Snapshot *snapshot) {
    char buffer[8192];
    if (read_file("/proc/meminfo", buffer, sizeof buffer) < 0) return -1;
    uint64_t available = 0, free_swap = 0;
    char *save = NULL;
    for (char *line = strtok_r(buffer, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        unsigned long long kib;
        if (sscanf(line, "MemTotal: %llu kB", &kib) == 1)
            snapshot->memory_total = kib * 1024ULL;
        else if (sscanf(line, "MemAvailable: %llu kB", &kib) == 1)
            available = kib * 1024ULL;
        else if (sscanf(line, "SwapTotal: %llu kB", &kib) == 1)
            snapshot->swap_total = kib * 1024ULL;
        else if (sscanf(line, "SwapFree: %llu kB", &kib) == 1)
            free_swap = kib * 1024ULL;
    }
    if (snapshot->memory_total == 0) { errno = EINVAL; return -1; }
    snapshot->memory_used = snapshot->memory_total > available
        ? snapshot->memory_total - available : 0;
    snapshot->swap_used = snapshot->swap_total > free_swap
        ? snapshot->swap_total - free_swap : 0;
    return 0;
}

static int collect_disk(Snapshot *snapshot) {
    struct statvfs disk;
    if (statvfs("/", &disk) < 0) return -1;
    snapshot->disk_total = (uint64_t)disk.f_blocks * disk.f_frsize;
    uint64_t free_bytes = (uint64_t)disk.f_bfree * disk.f_frsize;
    snapshot->disk_used = snapshot->disk_total > free_bytes
        ? snapshot->disk_total - free_bytes : 0;
    return 0;
}

static int parse_process_stat(const char *buffer, Process *process) {
    const char *open_paren = strchr(buffer, '(');
    const char *close_paren = strrchr(buffer, ')');
    if (!open_paren || !close_paren || close_paren <= open_paren) return -1;
    size_t name_length = (size_t)(close_paren - open_paren - 1);
    if (name_length >= sizeof process->name) name_length = sizeof process->name - 1;
    memcpy(process->name, open_paren + 1, name_length);
    process->name[name_length] = '\0';

    char fields[4096];
    snprintf(fields, sizeof fields, "%s", close_paren + 1);
    char *save = NULL;
    int number = 3;
    int found = 0;
    for (char *token = strtok_r(fields, " \t\n", &save); token;
         token = strtok_r(NULL, " \t\n", &save), ++number) {
        if (number == 3) { process->state = token[0]; found |= 1; }
        if (number == 14 || number == 15) {
            process->cpu_ticks += strtoull(token, NULL, 10);
            found |= (number == 14 ? 2 : 4);
        }
        if (number == 22) { process->start_time = strtoull(token, NULL, 10); found |= 8; }
        if (number == 24) { process->rss_pages = strtol(token, NULL, 10); found |= 16; }
        if (number >= 24) break;
    }
    return found == 31 ? 0 : -1;
}

static int read_process(pid_t pid, Process *process) {
    char path[64], buffer[4096];
    snprintf(path, sizeof path, "/proc/%ld/stat", (long)pid);
    if (read_file(path, buffer, sizeof buffer) < 0) return -1;
    memset(process, 0, sizeof *process);
    process->pid = pid;
    if (parse_process_stat(buffer, process) < 0) { errno = EINVAL; return -1; }

    snprintf(path, sizeof path, "/proc/%ld", (long)pid);
    struct stat details;
    if (stat(path, &details) < 0) return -1;
    struct passwd *owner = getpwuid(details.st_uid);
    if (owner) snprintf(process->user, sizeof process->user, "%s", owner->pw_name);
    else snprintf(process->user, sizeof process->user, "%lu", (unsigned long)details.st_uid);
    return 0;
}

static const Process *find_previous(const Snapshot *previous, const Process *current) {
    if (!previous) return NULL;
    for (size_t i = 0; i < previous->process_count; ++i) {
        const Process *candidate = &previous->processes[i];
        if (candidate->pid == current->pid && candidate->start_time == current->start_time)
            return candidate;
    }
    return NULL;
}

static int collect_processes(Snapshot *current, const Snapshot *previous) {
    DIR *directory = opendir("/proc");
    if (!directory) return -1;
    size_t capacity = 256;
    current->processes = calloc(capacity, sizeof *current->processes);
    if (!current->processes) { closedir(directory); return -1; }
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    uint64_t total_delta = previous && current->cpu.total > previous->cpu.total
        ? current->cpu.total - previous->cpu.total : 0;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char *end = NULL;
        long pid = strtol(entry->d_name, &end, 10);
        if (*end != '\0' || pid <= 0) continue;
        Process process;
        if (read_process((pid_t)pid, &process) < 0) continue; /* exited or inaccessible */
        const Process *old = find_previous(previous, &process);
        if (old && process.cpu_ticks >= old->cpu_ticks && total_delta)
            process.cpu_percent = 100.0 * (double)(process.cpu_ticks - old->cpu_ticks)
                * (double)current->core_count / (double)total_delta;
        if (current->memory_total && process.rss_pages > 0)
            process.memory_percent = 100.0 * (double)process.rss_pages
                * (double)page_size / (double)current->memory_total;
        if (current->process_count == capacity) {
            size_t new_capacity = capacity * 2;
            Process *larger = realloc(current->processes,
                new_capacity * sizeof *current->processes);
            if (!larger) { closedir(directory); return -1; }
            current->processes = larger;
            capacity = new_capacity;
        }
        current->processes[current->process_count++] = process;
    }
    closedir(directory);
    return 0;
}

int monitor_collect(Snapshot *current, const Snapshot *previous) {
    memset(current, 0, sizeof *current);
    if (collect_cpu(current, previous) < 0 ||
        collect_memory(current) < 0 ||
        collect_disk(current) < 0 ||
        collect_processes(current, previous) < 0) {
        monitor_free(current);
        return -1;
    }
    return 0;
}

void monitor_free(Snapshot *snapshot) {
    free(snapshot->processes);
    memset(snapshot, 0, sizeof *snapshot);
}

int monitor_process_identity(pid_t pid, uint64_t *start_time) {
    Process process;
    if (read_process(pid, &process) < 0) return -1;
    *start_time = process.start_time;
    return 0;
}

int monitor_send_signal(pid_t pid, uint64_t expected_start_time, int signal_number) {
    if (pid <= 1 || pid == getpid()) { errno = EPERM; return -1; }
    uint64_t actual_start_time;
    if (monitor_process_identity(pid, &actual_start_time) < 0) return -1;
    if (actual_start_time != expected_start_time) { errno = ESRCH; return -1; }
    return kill(pid, signal_number);
}
