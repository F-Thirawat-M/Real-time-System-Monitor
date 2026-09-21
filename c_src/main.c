#define _POSIX_C_SOURCE 200809L

#include "monitor.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef enum { SORT_CPU, SORT_MEMORY, SORT_PID, SORT_NAME } SortMode;

typedef struct {
    SortMode sort;
    char search[128];
    size_t selected;
    int paused;
    int editing_search;
    int confirming;
    int pending_signal;
    pid_t pending_pid;
    uint64_t pending_start_time;
    char pending_name[MINI_NAME_LEN];
    char message[256];
} UiState;

static volatile sig_atomic_t running = 1;
static SortMode active_sort = SORT_CPU;

static void request_exit(int signal_number) {
    (void)signal_number;
    running = 0;
}

static double percent(uint64_t used, uint64_t total) {
    return total ? 100.0 * (double)used / (double)total : 0.0;
}

static void size_text(uint64_t bytes, char *result, size_t capacity) {
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double value = (double)bytes;
    size_t unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; unit++; }
    snprintf(result, capacity, "%.1f%s", value, units[unit]);
}

static int compare_process(const void *left, const void *right) {
    const Process *a = left, *b = right;
    if (active_sort == SORT_NAME) {
        int result = strcasecmp(a->name, b->name);
        if (result) return result;
    } else if (active_sort == SORT_CPU || active_sort == SORT_MEMORY) {
        double a_value = active_sort == SORT_CPU ? a->cpu_percent : a->memory_percent;
        double b_value = active_sort == SORT_CPU ? b->cpu_percent : b->memory_percent;
        if (a_value < b_value) return 1;
        if (a_value > b_value) return -1;
    }
    return (a->pid > b->pid) - (a->pid < b->pid);
}

static int contains_case_insensitive(const char *haystack, const char *needle) {
    if (!*needle) return 1;
    for (const char *start = haystack; *start; ++start) {
        const char *a = start, *b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            ++a;
            ++b;
        }
        if (!*b) return 1;
    }
    return 0;
}

static int matches(const Process *process, const char *query) {
    if (!*query) return 1;
    char pid_text[32];
    snprintf(pid_text, sizeof pid_text, "%ld", (long)process->pid);
    return contains_case_insensitive(process->name, query) ||
        contains_case_insensitive(pid_text, query);
}

static size_t visible_count(const Snapshot *snapshot, const UiState *ui) {
    size_t count = 0;
    for (size_t i = 0; i < snapshot->process_count; ++i)
        if (matches(&snapshot->processes[i], ui->search)) count++;
    return count;
}

static const Process *visible_at(const Snapshot *snapshot, const UiState *ui, size_t index) {
    for (size_t i = 0; i < snapshot->process_count; ++i) {
        if (!matches(&snapshot->processes[i], ui->search)) continue;
        if (index-- == 0) return &snapshot->processes[i];
    }
    return NULL;
}

static void clamp_selection(const Snapshot *snapshot, UiState *ui) {
    size_t count = visible_count(snapshot, ui);
    if (!count) ui->selected = 0;
    else if (ui->selected >= count) ui->selected = count - 1;
}

static void draw_gauge(const char *label, double value, uint64_t used, uint64_t total) {
    int filled = (int)(value * 20.0 / 100.0 + 0.5);
    if (filled < 0) filled = 0;
    if (filled > 20) filled = 20;
    char used_text[24], total_text[24];
    size_text(used, used_text, sizeof used_text);
    size_text(total, total_text, sizeof total_text);
    printf("%-5s [", label);
    for (int i = 0; i < 20; ++i) putchar(i < filled ? '#' : '.');
    printf("] %5.1f%%  %s / %s\n", value, used_text, total_text);
}

static void render(const Snapshot *snapshot, UiState *ui) {
    struct winsize window = {0};
    int rows = 24;
    int columns = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_row > 0)
        rows = window.ws_row;
    if (window.ws_col > 0) columns = window.ws_col;
    printf("\033[H\033[2J\033[1;36mMini-htop (C/POSIX on Linux)\033[0m   "
           "%zu processes   %s\n", snapshot->process_count,
           ui->paused ? "PAUSED" : "LIVE");
    printf("CPU   [");
    int cpu_filled = (int)(snapshot->cpu_percent * 20.0 / 100.0 + 0.5);
    for (int i = 0; i < 20; ++i) putchar(i < cpu_filled ? '#' : '.');
    printf("] %5.1f%%\n", snapshot->cpu_percent);
    draw_gauge("RAM", percent(snapshot->memory_used, snapshot->memory_total),
        snapshot->memory_used, snapshot->memory_total);
    draw_gauge("SWAP", percent(snapshot->swap_used, snapshot->swap_total),
        snapshot->swap_used, snapshot->swap_total);
    draw_gauge("DISK", percent(snapshot->disk_used, snapshot->disk_total),
        snapshot->disk_used, snapshot->disk_total);
    printf("Cores: ");
    size_t cores_per_line = columns > 25 ? (size_t)(columns - 16) / 10 : 1;
    if (cores_per_line > 12) cores_per_line = 12;
    size_t shown_cores = cores_per_line * 3;
    if (shown_cores > snapshot->core_count) shown_cores = snapshot->core_count;
    for (size_t i = 0; i < shown_cores; ++i)
    {
        if (i && i % cores_per_line == 0) printf("\n       ");
        printf("C%zu:%3.0f%% ", i, snapshot->core_percent[i]);
    }
    if (snapshot->core_count > shown_cores)
        printf("+%zu more", snapshot->core_count - shown_cores);
    printf("\n");
    printf("%-7s %-14s %-25s %4s %7s %7s\n",
        "PID", "USER", "PROCESS", "STAT", "CPU%", "RAM%");

    size_t count = visible_count(snapshot, ui);
    clamp_selection(snapshot, ui);
    size_t core_lines = shown_cores ? (shown_cores + cores_per_line - 1) / cores_per_line : 1;
    size_t table_rows = rows > (int)(12 + core_lines)
        ? (size_t)(rows - 10 - (int)core_lines) : 3;
    size_t offset = ui->selected >= table_rows ? ui->selected - table_rows + 1 : 0;
    for (size_t index = offset; index < count && index < offset + table_rows; ++index) {
        const Process *process = visible_at(snapshot, ui, index);
        if (!process) break;
        printf("%s%-7ld %-14.14s %-25.25s %4c %7.1f %7.1f\033[0m\n",
            index == ui->selected ? "\033[7m" : "",
            (long)process->pid, process->user, process->name,
            process->state, process->cpu_percent, process->memory_percent);
    }
    printf("\n/ search  c/m/p/n sort  arrows/j/k select  t TERM  K KILL\n"
           "Space pause  r refresh  q quit\n");
    if (ui->confirming)
        printf("Send %s to %s (PID %ld)? [y/N] ",
            ui->pending_signal == SIGKILL ? "SIGKILL" : "SIGTERM",
            ui->pending_name, (long)ui->pending_pid);
    else if (ui->editing_search)
        printf("Search: %s_", ui->search);
    else
        printf("Filter: %s | matches: %zu | %s",
            *ui->search ? ui->search : "(none)", count, ui->message);
    printf("\033[J");
    fflush(stdout);
}

static int refresh(Snapshot *snapshot, UiState *ui) {
    Snapshot next;
    const Process *selected = visible_at(snapshot, ui, ui->selected);
    pid_t selected_pid = selected ? selected->pid : -1;
    uint64_t selected_start = selected ? selected->start_time : 0;
    if (monitor_collect(&next, snapshot) < 0) return -1;
    monitor_free(snapshot);
    *snapshot = next;
    active_sort = ui->sort;
    qsort(snapshot->processes, snapshot->process_count,
        sizeof *snapshot->processes, compare_process);
    if (selected_pid > 0) {
        size_t index = 0;
        for (size_t i = 0; i < snapshot->process_count; ++i) {
            Process *candidate = &snapshot->processes[i];
            if (!matches(candidate, ui->search)) continue;
            if (candidate->pid == selected_pid && candidate->start_time == selected_start) {
                ui->selected = index;
                break;
            }
            index++;
        }
    }
    clamp_selection(snapshot, ui);
    return 0;
}

static void start_confirmation(const Snapshot *snapshot, UiState *ui, int signal_number) {
    const Process *selected = visible_at(snapshot, ui, ui->selected);
    if (!selected) { snprintf(ui->message, sizeof ui->message, "No process selected"); return; }
    if (selected->pid <= 1 || selected->pid == getpid()) {
        snprintf(ui->message, sizeof ui->message, "Refusing to signal PID %ld", (long)selected->pid);
        return;
    }
    ui->pending_pid = selected->pid;
    ui->pending_start_time = selected->start_time;
    ui->pending_signal = signal_number;
    snprintf(ui->pending_name, sizeof ui->pending_name, "%s", selected->name);
    ui->confirming = 1;
}

static void handle_key(const char *input, ssize_t length, Snapshot *snapshot, UiState *ui) {
    if (length <= 0) return;
    unsigned char key = (unsigned char)input[0];
    if (ui->confirming) {
        ui->confirming = 0;
        if (key == 'y' || key == 'Y') {
            if (monitor_send_signal(ui->pending_pid, ui->pending_start_time,
                    ui->pending_signal) == 0)
                snprintf(ui->message, sizeof ui->message, "Signal sent to PID %ld",
                    (long)ui->pending_pid);
            else
                snprintf(ui->message, sizeof ui->message, "Signal failed: %s", strerror(errno));
        } else snprintf(ui->message, sizeof ui->message, "Cancelled");
        return;
    }
    if (ui->editing_search) {
        if (key == '\r' || key == '\n' || key == 27) ui->editing_search = 0;
        else if (key == 127 || key == 8) {
            size_t length_now = strlen(ui->search);
            if (length_now) ui->search[length_now - 1] = '\0';
            ui->selected = 0;
        } else if (isprint(key)) {
            size_t length_now = strlen(ui->search);
            if (length_now + 1 < sizeof ui->search) {
                ui->search[length_now] = (char)key;
                ui->search[length_now + 1] = '\0';
                ui->selected = 0;
            }
        }
        return;
    }
    const Process *selected_before = visible_at(snapshot, ui, ui->selected);
    pid_t selected_pid = selected_before ? selected_before->pid : -1;
    uint64_t selected_start = selected_before ? selected_before->start_time : 0;
    int changed_sort = 0;
    if (key == 'q') running = 0;
    else if (key == '/') ui->editing_search = 1;
    else if (key == ' ') ui->paused = !ui->paused;
    else if (key == 'c') { ui->sort = SORT_CPU; changed_sort = 1; }
    else if (key == 'm') { ui->sort = SORT_MEMORY; changed_sort = 1; }
    else if (key == 'p') { ui->sort = SORT_PID; changed_sort = 1; }
    else if (key == 'n') { ui->sort = SORT_NAME; changed_sort = 1; }
    else if (key == 't') start_confirmation(snapshot, ui, SIGTERM);
    else if (key == 'K') start_confirmation(snapshot, ui, SIGKILL);
    else if (key == 'j' || (length >= 3 && key == 27 && input[1] == '[' && input[2] == 'B'))
        ui->selected++;
    else if (key == 'k' || (length >= 3 && key == 27 && input[1] == '[' && input[2] == 'A')) {
        if (ui->selected) ui->selected--;
    }
    active_sort = ui->sort;
    qsort(snapshot->processes, snapshot->process_count,
        sizeof *snapshot->processes, compare_process);
    if (changed_sort && selected_pid > 0) {
        size_t visible_index = 0;
        for (size_t i = 0; i < snapshot->process_count; ++i) {
            const Process *candidate = &snapshot->processes[i];
            if (!matches(candidate, ui->search)) continue;
            if (candidate->pid == selected_pid && candidate->start_time == selected_start) {
                ui->selected = visible_index;
                break;
            }
            visible_index++;
        }
    }
    clamp_selection(snapshot, ui);
}

static int run_once(void) {
    Snapshot first, second;
    if (monitor_collect(&first, NULL) < 0) { perror("monitor_collect"); return 1; }
    struct timespec delay = { .tv_sec = 0, .tv_nsec = 300000000L };
    nanosleep(&delay, NULL);
    if (monitor_collect(&second, &first) < 0) {
        perror("monitor_collect"); monitor_free(&first); return 1;
    }
    monitor_free(&first);
    active_sort = SORT_CPU;
    qsort(second.processes, second.process_count, sizeof *second.processes, compare_process);
    printf("Mini-htop snapshot (Linux /proc + POSIX)\n");
    printf("CPU %.1f%% (%zu cores), RAM %.1f%%, SWAP %.1f%%, DISK %.1f%%\n",
        second.cpu_percent, second.core_count,
        percent(second.memory_used, second.memory_total),
        percent(second.swap_used, second.swap_total),
        percent(second.disk_used, second.disk_total));
    printf("Processes: %zu\n", second.process_count);
    for (size_t i = 0; i < second.process_count && i < 10; ++i) {
        const Process *p = &second.processes[i];
        printf("%7ld %-14.14s %-25.25s CPU %6.1f%% RAM %5.1f%%\n",
            (long)p->pid, p->user, p->name, p->cpu_percent, p->memory_percent);
    }
    monitor_free(&second);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--once") == 0) return run_once();
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: ./mini-htop [--once|--help]\n"
             "Interactive Linux monitor: / search, c/m/p/n sort, arrows/j/k select,\n"
             "t SIGTERM, K SIGKILL, Space pause, r refresh, q quit.");
        return 0;
    }
    if (argc != 1 || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "Run in a Linux terminal, or use --once / --help.\n");
        return 2;
    }
    struct termios original, raw;
    if (tcgetattr(STDIN_FILENO, &original) < 0) { perror("tcgetattr"); return 1; }
    raw = original;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) { perror("tcsetattr"); return 1; }
    struct sigaction action = {0};
    action.sa_handler = request_exit;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    printf("\033[?1049h\033[?25l");
    fflush(stdout);

    Snapshot snapshot;
    UiState ui = {0};
    snprintf(ui.message, sizeof ui.message, "Ready");
    int result = 0;
    if (monitor_collect(&snapshot, NULL) < 0) {
        result = 1;
        goto cleanup;
    }
    while (running) {
        render(&snapshot, &ui);
        struct pollfd input = { .fd = STDIN_FILENO, .events = POLLIN };
        int ready = poll(&input, 1, 1000);
        if (ready < 0 && errno != EINTR) { result = 1; break; }
        if (ready > 0 && (input.revents & POLLIN)) {
            char keys[32];
            ssize_t length = read(STDIN_FILENO, keys, sizeof keys);
            if (length <= 0) { running = 0; break; }
            int refresh_requested = 0;
            for (ssize_t i = 0; i < length;) {
                ssize_t key_length = 1;
                if (keys[i] == 27 && i + 2 < length && keys[i + 1] == '[')
                    key_length = 3;
                if (key_length == 1 && keys[i] == 'r' &&
                    !ui.editing_search && !ui.confirming)
                    refresh_requested = 1;
                handle_key(keys + i, key_length, &snapshot, &ui);
                i += key_length;
            }
            if (refresh_requested && !ui.paused) {
                if (refresh(&snapshot, &ui) < 0) {
                    snprintf(ui.message, sizeof ui.message, "Refresh failed: %s", strerror(errno));
                }
            }
        } else if (!ui.paused && running) {
            if (refresh(&snapshot, &ui) < 0)
                snprintf(ui.message, sizeof ui.message, "Refresh failed: %s", strerror(errno));
        }
    }
    monitor_free(&snapshot);
cleanup:
    printf("\033[?25h\033[?1049l");
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    if (result) perror("mini-htop");
    return result;
}
