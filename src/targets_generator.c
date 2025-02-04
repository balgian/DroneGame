//
// Created by Gian Marco Balia
//
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

FILE *logfile;

void signal_triggered(int signum) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    logfile = fopen("logfile.txt", "a");
    fprintf(logfile, "[%02d:%02d:%02d] PID: %d - %s\n", t->tm_hour, t->tm_min, t->tm_sec, getpid(),
        "Targets is active.");
    fflush(logfile);
}

int main (int argc, char *argv[]) {
    /*
     * Targets process
     * @param argv[1]: Read file descriptors
     * @param argv[2]: Write file descriptors
    */

    // * Signal handler
    struct sigaction sa1;
    memset(&sa1, 0, sizeof(sa1));
    sa1.sa_handler = signal_triggered;
    sigaction(SIGUSR1, &sa1, NULL);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <read_fd> <write_fd>\n", argv[0]);
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

    // * Read height and width from blackboard
    char buffer[32];
    ssize_t n = read(read_fd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        perror("read");
        return EXIT_FAILURE;
    }
    // * To treat 'buffer' as a C-string
    buffer[n] = '\0';

    int height, width;
    if (sscanf(buffer, "%d,%d", &height, &width) != 2) {
        fprintf(stderr, "Failed to parse height,width from '%s'\n", buffer);
        return EXIT_FAILURE;
    }

    char grid[height][width];
    memset(grid, ' ', height*width);

    if (read(read_fd, grid, height * width * sizeof(char)) == -1) {
        perror("read");
        return EXIT_FAILURE;
    }

    // * Generate targets
    srand(time(NULL));
    int num_target = 9;
    while (num_target > 0) {
        int x = (rand() % (width - 2)) + 1;
        int y = (rand() % (height - 2)) + 1;

        if (grid[y][x] == ' ' && !(x == width/2 && y == height/2)) {
            grid[y][x] = num_target + '0';
            num_target --;
        }
    }

    if (write(write_fd, grid, height * width * sizeof(char)) == -1) {
        perror("obstacle write");
        return EXIT_FAILURE;
    }
    // * Close the pipes
    close(read_fd);
    close(write_fd);
    return EXIT_SUCCESS;
}
