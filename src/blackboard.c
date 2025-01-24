//
// Created by Gian Marco Balia
//
// src/blackboard.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
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

int initialize_ncurses() {
    // * Start curses mode
    if (initscr() == NULL) {
        return EXIT_FAILURE;
    }
    cbreak();               // * Disable line buffering
    noecho();               // * Don't echo pressed keys
    curs_set(FALSE);        // * Hide the cursor
    start_color();          // * Enable color functionality
    use_default_colors();   // * Allow default terminal colors

    // * Initialize color pairs
    init_pair(1, COLOR_BLUE, -1);    // * Drone
    init_pair(2, COLOR_GREEN, -1);   // * Targets
    init_pair(3, COLOR_YELLOW, -1);  // * Obstacles

    return EXIT_SUCCESS;
}

void game_window(WINDOW **win, int *height, int *width, int *status, int *read_fds, int *write_fds) {
    // * Remap the read and write file descriptors
    const int keyboard = read_fds[0];
    const int obstacle_read = read_fds[1];
    const int target_read = read_fds[2];
    const int dynamic_read = read_fds[3];

    const int obstacle_write = write_fds[0];
    const int target_write = write_fds[1];
    const int dynamic_write = write_fds[2];

    // * Set the keyboard commands not blocking
    int flags = fcntl(keyboard, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    flags |= O_NONBLOCK;
    if (fcntl(keyboard, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }


    // * Get the terminal dimensions
    int term_height, term_width;
    getmaxyx(stdscr, term_height, term_width);

    // * Handle terminal resize
    if (KEY_RESIZE) {
        // ! Ensure the window dimensions are positive and fit within the screen
        *height = term_height;
        *width = term_width;

        // * Delete the old window
        if (*win != NULL) {
            delwin(*win);
        }

        // * Create a new window that fills the screen
        *win = newwin(*height, *width, 0, 0);

        // * Draw the border for the new window
        wborder(*win, '|', '|', '-', '-', '+', '+', '+', '+');
    }

    switch (*status) {
        case 0: { // * Initial state
            const char *message = "Press S to start or Q to quit";
            int msg_length = strlen(message);

            mvwprintw(*win, term_height / 2, (term_width - msg_length) / 2, "%s", message);
            // TODO: The caller should handle key inputs ('S' or 'Q')
            sleep(3);
            *status = 0;
            break;
        }
        case 1: { // * Start game
            const char *message = "Game is ready";
            int msg_length = strlen(message);

            mvwprintw(*win, term_height / 2, (term_width - msg_length) / 2, "%s", message);
            // TODO: The caller should handle key inputs ('S' or 'Q')
            sleep(3);
            *status = 0;
            break;
        }
        default:
            // TODO: Add more cases for different statuses as needed
                break;
    }

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

    // * Initialise window's game
    if (initialize_ncurses() != EXIT_FAILURE) {
        // * Game variables
        WINDOW *win = NULL;
        int height = 0;
        int width = 0;
        int status = 0;
        int drone_pos[2] = {0, 0};
        int drone_force[2] = {0, 0};
        char *c;
        do {
            if (read(keyboard, c, 1) == -1 && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
                // * An actual error occurred
                perror("read");
                break;
            }
            game_window(&win, &height, &width, &status);
        } while (!(status == -2  && *c == 'q')); // * Exit on 'q' and if status is -2

        // * Delete the window
        delwin(win);
    }
    else {
        fprintf(stderr, "Error initializing ncurses.\n");
    }

    // * Clean up
    endwin(); // * Close ncurses window

    // Close file descriptors
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        close(read_fds[i]);
    }
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES - 1; i++) {
        close(write_fds[i]);
    }

    // TODO: Send a signal to the other 5 processes to close

    return EXIT_SUCCESS;
}