//
// Created by Gian Marco Balia
//
// src/blackboard.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <ncurses.h>
#include <sys/types.h>
#include "macros.h"

FILE *logfile;

/**
 * Parse the file descriptors and watchdog PID from the command-line arguments.
 * @param argc Number of arguments.
 * @param argv Array of arguments.
 * @param read_fds Array to store read file descriptors.
 * @param write_fds Array to store write file descriptors.
 * @param watchdog_pid Pointer to store the watchdog PID.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
 */
int parser(int argc, char *argv[], int *read_fds, int *write_fds, pid_t *watchdog_pid) {
    // * Parse read file descriptors
    for (int i = 0; i < NUM_CHILD_PIPES; i++) {
        char *endptr;
        read_fds[i] = strtol(argv[i + 1], &endptr, 10);
        if (*endptr != '\0' || read_fds[i] < 0) {
            fprintf(stderr, "Invalid read file descriptor: %s\n", argv[i + 1]);
            return EXIT_FAILURE;
        }
    }

    // * Parse write file descriptors (excluding keyboard_manager)
    for (int i = 0; i < NUM_CHILD_PIPES - 1; i++) {
        char *endptr;
        write_fds[i] = strtol(argv[NUM_CHILD_PIPES + i + 1], &endptr, 10);
        if (*endptr != '\0' || write_fds[i] < 0) {
            fprintf(stderr, "Invalid write file descriptor: %s\n", argv[NUM_CHILD_PIPES + i + 1]);
            return EXIT_FAILURE;
        }
    }

    // * Parse watchdog PID
    char *endptrwd;
    *watchdog_pid = strtol(argv[argc - 2], &endptrwd, 10);
    if (*endptrwd != '\0' || *watchdog_pid <= 0) {
        fprintf(stderr, "Invalid watchdog PID: %s\n", argv[argc - 2]);
        return EXIT_FAILURE;
    }

    // Parse logfile file descriptor and open it
    int logfile_fd = atoi(argv[argc - 1]);
    logfile = fdopen(logfile_fd, "a");
    if (!logfile) {
        perror("fdopen logfile");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * Signal handler that writes a message to the logfile when a signal is received.
 *
 * @param signum The signal number.
 */
void signal_triggered(int signum) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(logfile, "[%02d:%02d:%02d] PID: %d - %s\n", t->tm_hour, t->tm_min, t->tm_sec, getpid(),
        "Blackboard is active.");
    fflush(logfile);
}

/**
 * Writes all bytes from the buffer to the given file descriptor.
 *
 * @param fd The file descriptor.
 * @param buf Buffer containing data to write.
 * @param count Number of bytes to write.
 * @return The total number of bytes written, or -1 on error.
 */
ssize_t robust_write(int fd, const void *buf, size_t count) {
    size_t bytes_written = 0;
    const char *buffer = buf;
    while (bytes_written < count) {
        ssize_t result = write(fd, buffer + bytes_written, count - bytes_written);
        if (result == -1) {
            if (errno == EINTR)
                continue; // Retry if interrupted
            return -1; // Other errors
        }
        bytes_written += result;
    }
    return bytes_written;
}

/**
 * Initialize ncurses settings and create a new window.
 *
 * @return Pointer to the newly created window, or NULL on failure.
 */
int initialize_ncurses() {
    /*
     * Initialize ncurses settings.
     */

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
    init_pair(1, COLOR_BLUE, -1); // * Drone
    init_pair(2, COLOR_GREEN, -1); // * Targets
    init_pair(3, COLOR_YELLOW, -1); // * Obstacles

    return EXIT_SUCCESS;
}

/**
 * Modify the drone force based on the input key.
 *
 * Command keys:
 * 'w': Up Left, 'e': Up, 'r': Up Right or Reset,
 * 's': Left or Suspend, 'd': Brake, 'f': Right,
 * 'x': Down Left, 'c': Down, 'v': Down Right,
 * 'p': Pause, 'q': Quit
 *
 * @param drone_force Array representing the drone's force.
 * @param c The input character.
 */
void command_drone(int *drone_force, char c) {
    if (strchr("wsx", c)) {
        drone_force[0]--;
    }
    if (strchr("rfv", c)) {
        drone_force[0]++;
    }
    if (strchr("wer", c)) {
        drone_force[1]--;
    }
    if (strchr("xcv", c)) {
        drone_force[1]++;
    }
    if (c == 'd') {
        drone_force[0] = 0;
        drone_force[1] = 0;
    }
}

/**
 * Main function for the blackboard process.
 *
 * @param argc Argument count.
 * @param argv Array of command-line arguments.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
 *
 * The command-line arguments are:
 * - argv[1..N]: Read file descriptors
 * - argv[N+1..2N-1]: Write file descriptors (excluding keyboard_manager)
 * - argv[2N]: Watchdog PID
 * - argv[2N+1]: Logfile file descriptor
 */
int main(const int argc, char *argv[]) {
    // * Define the signal action
    struct sigaction sa1;
    memset(&sa1, 0, sizeof(sa1));
    sa1.sa_handler = signal_triggered;
    sa1.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa1, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (argc != 2 * NUM_CHILD_PIPES + 2) {
        fprintf(stderr, "Usage: %s <read_fd1> <read_fd2> ... <read_fdN> <write_fd1> ... "
                        "<write_fdN-1> <watchdog_pid> <logfile_fd>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    // * Parse argv
    int read_fds[NUM_CHILD_PIPES];
    int write_fds[NUM_CHILD_PIPES - 1];
    pid_t watchdog_pid = 0;
    if (parser(argc, argv, read_fds, write_fds, &watchdog_pid) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    // ! Map the child pipes to more meaningful names
    const int keyboard = read_fds[0];
    const int obstacle_read = read_fds[1];
    const int target_read = read_fds[2];
    const int dynamic_read = read_fds[3];

    const int obstacle_write = write_fds[0];
    const int target_write = write_fds[1];
    const int dynamic_write = write_fds[2];

    // * Initialise window's game
    if (initialize_ncurses() == EXIT_FAILURE) {
        fprintf(stderr, "Error initializing ncurses.\n");
        return EXIT_FAILURE;
    }
    // * Game variables
    WINDOW *win = NULL;
    int height = 0;
    int width = 0;
    int game_size[2] = {0, 0};
    int status = 0; // * Game status: 0=menu, 1=initialization, 2=running, -2=pause, -1=quit
    int drone_pos[4] = {0, 0, 0, 0};
    int drone_force[2] = {0, 0};
    char *grid = NULL;
    char c;
    fd_set read_keyboard;
    struct timeval timeout;
    do {
        // TODO: Implement the behaviour with the select (with multiple pipes). And nto pass the grid anymore but only the point that will change
        c = '\0';
        //usleep((useconds_t)(1e6/FRAME_RATE)); // * Frame rate of ~60Hz
        // * Attempt to read a character from the keyboard pipe (non-blocking)
        FD_ZERO(&read_keyboard);
        FD_SET(keyboard, &read_keyboard);

        timeout.tv_sec = 0;
        timeout.tv_usec = 1e6/FRAME_RATE; // * Frame rate of ~60Hz

        const int result = select(keyboard + 1, &read_keyboard, NULL, NULL, &timeout);

        if (result == -1) {
            perror("select error");
            break;
        }
        if (result > 0) {
            if (FD_ISSET(keyboard, &read_keyboard)) {
                const ssize_t bytesRead = read(keyboard, &c, 1);
                if (bytesRead == -1) {
                    perror("read keyboard");
                    break;
                }
            }
        }
        else {
            c = '\0';
        }

        // * Get the current terminal size
        getmaxyx(stdscr, height, width);

        // * Handle terminal resize
        if (KEY_RESIZE) {
            // * Delete the old window
            if (win != NULL) {
                delwin(win);
            }

            // * Create a new window that fills the screen
            win = newwin(height, width, 0, 0);
            // * Draw the border for the new window
            wattron(win, COLOR_PAIR(3)); // * YELLOW for walls
            wborder(win, '|', '|', '-', '-', '+', '+', '+', '+');
            wattroff(win, COLOR_PAIR(3));
        }

        switch (status) {
            case 0: { // * menu
                const char *message = "Press S to start or Q to quit";
                int msg_length = (int)strlen(message);

                mvwprintw(win, height / 2, (width - msg_length) / 2, "%s", message);
                if (c == 'q') status = -1; // * Then quit
                if (c == 's') status = 1; // * Run the game
                break;
            }
            case 1: { // * initialization
                game_size[0] = width;
                game_size[1] = height;
                // * Send (height,width) to obstacles, read obstacles back
                char out_buf[32];
                snprintf(out_buf, sizeof(out_buf), "%d,%d", height, width);
                // * Send "height,width" to obstacles
                if (write(obstacle_write, out_buf, strlen(out_buf)) == -1) {
                    perror("write obstacles");
                    status = -1;
                    c = 'q';
                    break;
                }

                grid = calloc(height * width,sizeof(char));
                if (!grid) {
                    perror("calloc grid");
                    status = -1;
                    c = 'q';
                    break;
                }
                if (read(obstacle_read, grid, height * width * sizeof(char)) == -1) {
                    perror("read obstacle");
                    status = -1;
                    c = 'q';
                    break;
                }

                // * Send (height,width, grid) to targets, read targets back
                if (write(target_write, out_buf, strlen(out_buf)) == -1) {
                    perror("write obstacles");
                    status = -1;
                    c = 'q';
                    break;
                }
                if (write(target_write, grid, height * width * sizeof(char)) == -1) {
                    perror("write target");
                    status = -1;
                    c = 'q';
                    break;
                }

                if (read(target_read, grid, height * width * sizeof(char)) == -1) {
                    perror("read targets");
                    status = -1;
                    c = 'q';
                    break;
                }
                // ! Clean possible dirties in grid due to pipes
                for (int i = 0; i < height * width; i++) {
                    if (!strchr("o0123456789", grid[i])) {
                        grid[i] = ' ';
                    }
                }

                // * Send grid dimention to dynamic
                if (write(dynamic_write, out_buf, sizeof(out_buf)) == -1) {
                    perror("write window dimention");
                    status = -1;
                    c = 'q';
                    break;
                }

                // * Setting drone initial positions
                drone_pos[0] = game_size[0] / 2;
                drone_pos[1] = game_size[1] / 2;
                drone_pos[2] = game_size[0] / 2;
                drone_pos[3] = game_size[1] / 2;

                // * Run the game
                status = 2;
                break;
            }
            case 2: { // * running
                // * Clean the previous position of the drone in the grid
                grid[drone_pos[1] * game_size[0] + drone_pos[0]] = ' ';

                // * Draw the new map
                for (int row = 1; row < game_size[1]-1; row++) {
                    for (int col = 1; col < game_size[0]-1; col++) {
                        if (grid[row * game_size[0] + col] == 'o') {
                            wattron(win, COLOR_PAIR(3)); // * YELLOW for obstacles
                            mvwprintw(win, row * height / game_size[1], col * width / game_size[0], "o");
                            wattroff(win, COLOR_PAIR(3));
                            continue;
                        }
                        if (strchr("0123456789", grid[row * game_size[0] + col])) {
                            wattron(win, COLOR_PAIR(2)); // * GREEN for targets
                            mvwprintw(win, row * height / game_size[1], col * width / game_size[0], "%c", grid[row * game_size[0] + col]);
                            wattroff(win, COLOR_PAIR(2));
                        }
                    }
                }
                // * Clean the previous position of the drone in the map and draw the current
                mvwprintw(win, drone_pos[1]*height/game_size[1], drone_pos[0]*width/game_size[0], " ");
                wattron(win, COLOR_PAIR(1)); // * BLUE for drone
                mvwprintw(win, drone_pos[3]*height/game_size[1], drone_pos[2]*width/game_size[0], "+");
                wattroff(win, COLOR_PAIR(1));
                // * Compute the new forces of the drone
                command_drone(drone_force, c);
                // * Send the new grid to drone dynamics
                if (robust_write(dynamic_write, grid, game_size[0] * game_size[1] * sizeof(char)) == -1) {
                    perror("robust_write target");
                    status = -1;
                    c = 'q';
                    break;
                }
                // * Send drone positions and forces generate by the user
                char msg[100];
                snprintf(msg, sizeof(msg), "%d,%d,%d,%d,%d,%d", drone_pos[0], drone_pos[1], drone_pos[2],
                    drone_pos[3], drone_force[0], drone_force[1]);
                if (write(dynamic_write, msg, sizeof(msg)) == -1) {
                    perror("write");
                    status = -1;
                    c = 'q';
                    break;
                }
                // * Retrieve the new position
                char in_buf[32];
                if (read(dynamic_read, in_buf, sizeof(in_buf)) == -1) {
                    perror("write");
                    status = -1;
                    c = 'q';
                    break;
                }
                drone_pos[0] = drone_pos[2];
                drone_pos[1] = drone_pos[3];
                if (sscanf(in_buf, "%d,%d", &drone_pos[2], &drone_pos[3]) != 2) {
                    perror("sscanf");
                    status = -1;
                    c = 'q';
                    break;
                }
                break;
            }
            default:
                break;
        }

        // * Refresh the standard screen and the new window
        wrefresh(win);
        wrefresh(stdscr);
    } while (!(status == -1  && c == 'q')); // * Exit on 'q' and if status is -2

    // Final cleanup
    if (win) {
        delwin(win);
    }
    endwin();

    free(grid);

    for (int i = 0; i < NUM_CHILD_PIPES; i++) {
        close(read_fds[i]);
    }
    for (int i = 0; i < NUM_CHILD_PIPES - 1; i++) {
        close(write_fds[i]);
    }

    return EXIT_SUCCESS;
}