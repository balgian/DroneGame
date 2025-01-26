//
// Created by Gian Marco Balia
//
// src/obstacles.c
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main (int argc, char *argv[]) {
    /*
     * Obstacles process
     * @param argv[1]: Read file descriptors
     * @param argv[2]: Write file descriptors
    */
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

    // * Generate obstacles
    srand(time(NULL));
    int num_obstacles = (int)(height * width * 0.01);
    char grid[height][width];
    memset(grid, ' ', height * width);
    while (num_obstacles > 0) {
        int x = (rand() % (width - 2)) + 1;
        int y = (rand() % (height - 2)) + 1;

        if (grid[y][x] == ' ' && !(x == width/2 && y == height/2)) {
            grid[y][x] = 'o';
            num_obstacles--;
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
