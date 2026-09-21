#define _POSIX_C_SOURCE 200809L

#include "monitor.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    Snapshot first, second;
    assert(monitor_collect(&first, NULL) == 0);
    assert(first.memory_total > 0);
    assert(first.core_count > 0);
    assert(first.process_count > 0);
    assert(monitor_collect(&second, &first) == 0);
    assert(second.disk_total > 0);
    monitor_free(&second);
    monitor_free(&first);

    uint64_t start_time = 0;
    assert(monitor_process_identity(getpid(), &start_time) == 0);
    assert(monitor_send_signal(getpid(), start_time, SIGTERM) == -1);
    assert(errno == EPERM);

    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        for (;;) pause();
    }
    assert(monitor_process_identity(child, &start_time) == 0);
    assert(monitor_send_signal(child, start_time + 1, SIGTERM) == -1);
    assert(errno == ESRCH);
    assert(monitor_send_signal(child, start_time, SIGTERM) == 0);
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

    pid_t forced_child = fork();
    assert(forced_child >= 0);
    if (forced_child == 0) {
        for (;;) pause();
    }
    assert(monitor_process_identity(forced_child, &start_time) == 0);
    assert(monitor_send_signal(forced_child, start_time, SIGKILL) == 0);
    assert(waitpid(forced_child, &status, 0) == forced_child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    puts("C monitor tests: PASS");
    return 0;
}
