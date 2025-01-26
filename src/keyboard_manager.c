//
// Created by Gian Marco Balia
//
// src/keyboard_manager.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "macros.h"

int main(int argc, char *argv[]) {
    /*
     * Keyboard process
     * @param argv[1]: Write file descriptors
    */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <write_fd>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int write_fd = atoi(argv[1]);
    if (write_fd <= 0) {
        fprintf(stderr, "Invalid write file descriptor: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    while(1){
        char c = getchar();
        switch (c) {
            case 'w': // * Up Left
            case 'e': // * Up
            case 'r': // * Up Right or Reset
            case 's': // * Left or Suspend
            case 'd': // * Brake
            case 'f': // * Right
            case 'x': // * Down Left
            case 'c': // * Down
            case 'v': // * Down Right
            case 'p': // * Pause
            case 'q': // * Quit
                if (write(write_fd, &c, sizeof(c)) == -1) {
                    perror("write");
                    return EXIT_FAILURE;
                }
            break;
            default:
                break;
        }
        usleep(1000); // * 1ms
    }

    close(write_fd);
    return EXIT_SUCCESS;
}