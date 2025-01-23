//
// Created by Gian Marco Balia
//
// src/blackboard.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ncurses.h>
#include "macros.h"

int parser(int argc, char *argv[], int *read_fds, int *write_fds, pid_t *watchdog_pid) {
    // * Parse read file descriptors
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        char *endptr;
        read_fds[i] = strtol(argv[i + 1], &endptr, 10);
        if (*endptr != '\0' || read_fds[i] < 0) {
            fprintf(stderr, "Invalid read file descriptor: %s\n", argv[i + 1]);
            return EXIT_FAILURE;
        }
    }

    // * Parse write file descriptors (excluding keyboard_manager)
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES - 1; i++) {
        char *endptr;
        write_fds[i] = strtol(argv[NUM_CHILDREN_WITH_PIPES + i + 1], &endptr, 10);
        if (*endptr != '\0' || write_fds[i] < 0) {
            fprintf(stderr, "Invalid write file descriptor: %s\n", argv[NUM_CHILDREN_WITH_PIPES + i + 1]);
            return EXIT_FAILURE;
        }
    }

    // * Parse watchdog PID
    char *endptr;
    *watchdog_pid = strtol(argv[argc - 1], &endptr, 10);
    if (*endptr != '\0' || *watchdog_pid <= 0) {
        fprintf(stderr, "Invalid watchdog PID: %s\n", argv[argc - 1]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void resize_window(WINDOW **win) {
    int new_height, new_width;

    // Get the new terminal dimensions
    getmaxyx(stdscr, new_height, new_width);

    // Ensure the window dimensions are positive and fit within the screen
    if (*win != NULL) {
        delwin(*win); // Delete the old window
    }

    // Create a new window that fills the screen minus the border
    *win = newwin(new_height, new_width, 0, 0);

    // Draw the border for the new window
    wborder(*win, '|', '|', '-', '-', '+', '+', '+', '+');

    // Refresh the standard screen and the new window
    wrefresh(stdscr);
    wrefresh(*win);
}

int main(int argc, char *argv[]) {
    /*
     * Blackboard process
     * @param argv[1..N]: Read file descriptors
     * @param argv[N+1..2N-1]: Write file descriptors (excluding keyboard_manager)
     * @param argv[2N]: Watchdog PID
    */

    if (argc != 2 * NUM_CHILDREN_WITH_PIPES + 1) {
        fprintf(stderr, "Usage: %s <read_fd1> <read_fd2> ... <read_fdN> <write_fd1> ... <write_fdN-1> <watchdog_pid>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    // * Parse argv
    int read_fds[NUM_CHILDREN_WITH_PIPES];
    int write_fds[NUM_CHILDREN_WITH_PIPES - 1];
    pid_t watchdog_pid = 0;
    if (parser(argc, argv, read_fds, write_fds, &watchdog_pid) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    // * Remap the read and write file descriptors
    const int keyboard = read_fds[0];
    const int obstacle_read = read_fds[1];
    const int target_read = read_fds[2];
    const int dynamic_read = read_fds[3];

    const int obstacle_write = write_fds[0];
    const int target_write = write_fds[1];
    const int dynamic_write = write_fds[2];

    /*
     * Initialization and setup of the map:
     * - Open a new ncurses window;
     * - Deactivate mouse support;
     * - Delete the echo of the keys;
     * - Delete the cursor highlight.
    */
    WINDOW *win = NULL;
    int ch;

    if (initscr() == NULL) {
        fprintf(stderr, "Error initializing ncurses.\n");
        return EXIT_FAILURE;
    }
    // * Start color functionality
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);    // * Define color pair 1: red text on black background
    init_pair(2, COLOR_GREEN, COLOR_BLACK); // * Define color pair 2: green text on black background
    init_pair(3, COLOR_BLUE, COLOR_WHITE);  // * Define color pair 3: blue text on white background

    // * Disable mouse, echo, and cursor
    mousemask(0, NULL);
    cbreak(); // * Disable line buffering
    noecho(); // * Disable echoing of typed characters
    keypad(stdscr, TRUE); // * Enable function keys
    curs_set(0); // * Hide the cursor

    // * Initial window setup
    attron(COLOR_PAIR(1));
    // Initial setup of the window
    resize_window(&win);
    attroff(COLOR_PAIR(1));

    while ((ch = getch()) != 'q') { // Exit on 'q'
        if (ch == KEY_RESIZE) {     // Handle terminal resize
            resize_window(&win);
        }
    }

    // * Clean up
    delwin(win); // * Delete the window
    endwin(); // * Close ncurses window

    // Close file descriptors
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        close(read_fds[i]);
    }
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES - 1; i++) {
        close(write_fds[i]);
    }

    return EXIT_SUCCESS;
}