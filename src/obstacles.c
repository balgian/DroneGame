//
// Created by Gian Marco Balia
//
// src/obstacles.c
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "macros.h"

FILE *logfile;

void signal_triggered(int signum) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(logfile, "[%02d:%02d:%02d] PID: %d - %s\n", t->tm_hour, t->tm_min, t->tm_sec, getpid(),
        "Obstacles is active.");
    fflush(logfile);
}

int main (int argc, char *argv[]) {
    /*
     * Obstacles process
     * @param argv[1]: Read file descriptors
     * @param argv[2]: Write file descriptors
    */
    // * Signal handler
    struct sigaction sa1;
    memset(&sa1, 0, sizeof(sa1));
    sa1.sa_handler = signal_triggered;
    sa1.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa1, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <read_fd> <write_fd> <logfile_fd>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int read_fd = atoi(argv[1]);
    if (read_fd <= 0) {
        fprintf(stderr, "Invalid read file descriptor: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int write_fd = atoi(argv[2]);
    if (write_fd <= 0) {
        fprintf(stderr, "Invalid write file descriptor: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    // * Parse logfile file descriptors
    int logfile_fd = atoi(argv[argc - 1]);
    logfile = fdopen(logfile_fd, "a");
    if (!logfile) {
        perror("fdopen logfile");
        return EXIT_FAILURE;
    }

    // * Generate obstacles
    srand(time(NULL));
    int num_obstacles = (int)(GAME_HEIGHT * GAME_WIDTH * 0.01);
    char grid[GAME_HEIGHT][GAME_WIDTH];
    memset(grid, ' ', GAME_HEIGHT * GAME_WIDTH);
    while (num_obstacles > 0) {
        int x = (rand() % (GAME_WIDTH - 2)) + 1;
        int y = (rand() % (GAME_HEIGHT - 2)) + 1;

        if (grid[y][x] == ' ' && !(x == GAME_WIDTH/2 && y == GAME_HEIGHT/2)) {
            grid[y][x] = 'o';
            num_obstacles--;
        }
    }

    if (write(write_fd, grid, GAME_HEIGHT * GAME_WIDTH * sizeof(char)) == -1) {
        perror("obstacle write");
        return EXIT_FAILURE;
    }
    // * Close the pipes
    close(read_fd);
    close(write_fd);
    return EXIT_SUCCESS;
}
