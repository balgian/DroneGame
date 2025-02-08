//
// Created by Gian Marco Balia
//
// src/watchdog.c
// watchdog.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>

int get_random_interval(int min, int max);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        perror("Usage: Watchdog <process_group_id>");
        exit(EXIT_FAILURE);
    }

    // * Parse the PGID
    pid_t pgid = (pid_t) atoi(argv[1]);
    if (pgid < 0) {
        perror("Invalid PGID");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL) ^ getpid());
    while (1) {
        // * Generate a random interval between 4 and 10 seconds
        const int sleep_time = get_random_interval(4, 10);
        sleep(1);

        // * Send SIGUSR1 to the process group
        if (kill(-pgid, SIGUSR1) == -1) { // ! Negative PGID sends to the group
            if (errno == ESRCH) {
                fprintf(stderr, "No such process group: %d. Watchdog exiting.\n", pgid);
            } else {
                perror("kill");
            }
        }
    }
    return 0;
}

int get_random_interval(int min, int max) {
    return min + rand() % (max - min + 1);
}