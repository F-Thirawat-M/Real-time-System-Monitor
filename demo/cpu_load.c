#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop_gracefully(int signal_number) {
    (void)signal_number;
    running = 0;
}

int main(void) {
    struct sigaction action = {0};
    action.sa_handler = stop_gracefully;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    printf("Demo CPU workload PID: %ld\n", (long)getpid());
    fflush(stdout);
    volatile unsigned long counter = 0;
    while (running) counter++;
    printf("SIGTERM handled; graceful shutdown after %lu iterations.\n", counter);
    return 0;
}
