//
// Created by Gian Marco Balia
//
// src/blackboard.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <ncurses.h>
#include <fcntl.h>
#include <math.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "macros.h"

FILE *logfile;

int parser(int argc, char *argv[], int *read_fds, int *write_fds);
void signal_triggered(int signum);
int initialize_ncurses();
void command_drone(int *drone_force, char c);
pid_t launch_inspection_window();
void remove_target_on_path(char grid[GAME_HEIGHT][GAME_WIDTH], int x0, int y0, int x1, int y1);

int main(const int argc, char *argv[]) {
    // * Define the signal action
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_triggered;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    // * Check if the of argument correspond
    if (argc != 2 * NUM_CHILD_PIPES + 1) {
        fprintf(stderr, "Usage: %s <read_fd1> <read_fd2> ... <read_fdN> <write_fd1> ... <write_fdN-1> <logfile_fd>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    // * Parse arguments
    int read_fds[NUM_CHILD_PIPES];
    int write_fds[NUM_CHILD_PIPES - 1];
    if (parser(argc, argv, read_fds, write_fds) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    // * Map the child pipes to more meaningful names
    const int keyboard = read_fds[0];
    const int obstacle_read = read_fds[1];
    const int target_read = read_fds[2];
    const int dynamic_read = read_fds[3];
    // const int obstacle_write = write_fds[0];
    const int target_write = write_fds[1];
    const int dynamic_write = write_fds[2];
    // * Make the named pipe to communicate with the inspector window
    mkfifo(INSPECTOR_FIFO, 0666);
    // * Initialise window's game
    if (initialize_ncurses() == EXIT_FAILURE) {
        fprintf(stderr, "Error initializing ncurses.\n");
        return EXIT_FAILURE;
    }
    // * Game variables
    WINDOW *win = NULL;
    nodelay(win, TRUE);
    // * Size of the window
    int height = 0, width = 0;
    getmaxyx(stdscr, height, width);
    // * Create the initial window
    win = newwin(height, width, 0, 0);
    // * Draw initial border
    box(win, 0, 0);
    wrefresh(win);
    // * Refresh the screen and window initially
    refresh();
    wrefresh(win);
    // * Size of the grid game
    char grid[GAME_HEIGHT][GAME_WIDTH];
    memset(grid, ' ', sizeof(grid));
    // * Game status: 0=menu, 1=initialization, 2=running, -2=pause, -1=quit
    int status = 0;
    int drone_pos[4] = {0, 0, 0, 0};
    int drone_force[2] = {0, 0};
    // * Score variables
    int score = 500000000;
    int distance_traveled = 0;
    int count_obstacles = 0;
    time_t start_time = time(NULL);
    // * Launches a new terminal window
    pid_t insp_pid = launch_inspection_window();
    // * Char read from keyboard
    char c;
    fd_set read_keyboard;
    struct timeval timeout;
    do {
        switch (status) {
            case 0: { // * Menu
                const char *message = "Press S to start or Q to quit";
                int msg_length = (int)strlen(message);
                mvwprintw(win, height / 2, (width - msg_length) / 2, "%s", message);

                // * Attempt to read a character from the keyboard pipe (non-blocking)
                FD_ZERO(&read_keyboard);
                FD_SET(keyboard, &read_keyboard);
                timeout.tv_sec = 0;
                timeout.tv_usec = 1e6/FRAME_RATE; // * Frame rate of ~60Hz
                if (select(keyboard + 1, &read_keyboard, NULL, NULL, &timeout) > 0) {
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
                // * Change the game status
                if (c == 'q') status = -1;  // * Then quit
                if (c == 's') {
                    status = 1;  // * Run the game
                    werase(win);  // * Erase entire window
                }
                break;
            }
            case 1: { // * initialization
                // * OBSTACLES
                // * Take the obstacles position
                if (read(obstacle_read, grid, GAME_HEIGHT * GAME_WIDTH * sizeof(char))  != GAME_HEIGHT * GAME_WIDTH * sizeof(char)) {
                    perror("read obstacle");
                    status = -1;
                    c = 'q';
                    break;
                }
                // * TARGETS
                // * Send the map to the targets
                if (write(target_write, grid, GAME_HEIGHT * GAME_WIDTH * sizeof(char)) == -1) {
                    perror("write target");
                    status = -1;
                    c = 'q';
                    break;
                }
                // * Take the targets position
                if (read(target_read, grid, GAME_HEIGHT * GAME_WIDTH * sizeof(char))  != GAME_HEIGHT * GAME_WIDTH * sizeof(char)) {
                    perror("read targets");
                    status = -1;
                    c = 'q';
                    break;
                }
                // * Clean possible dirties in grid due to pipes
                for (int row = 0; row < GAME_HEIGHT; row++) {
                    for (int col = 0; col < GAME_WIDTH; col++) {
                        if (!strchr("o0123456789", grid[row][col])) {
                            grid[row][col] = ' ';
                        }
                    }
                }
                // * Count hte number of obstacles for the score
                for (int r = 0; r < GAME_HEIGHT; r++) {
                    for (int col = 0; col < GAME_WIDTH; col++) {
                        if (grid[r][col] == 'o')
                            count_obstacles++;
                    }
                }
                // * Setting drone initial positions
                drone_pos[0] = GAME_WIDTH / 2;
                drone_pos[1] = GAME_HEIGHT / 2;
                drone_pos[2] = GAME_WIDTH / 2;
                drone_pos[3] = GAME_HEIGHT / 2;
                // * Run the game
                status = 2;
                break;
            }
            case 2: { // * Running
                // * Ssve the previous drone position to compute the velocity
                int prev_x = drone_pos[0], prev_y = drone_pos[1];
                // * Clean the previous position of the drone in the grid
                grid[drone_pos[1]][drone_pos[0]] = ' ';
                // * Draw the new map proportionally to the window dimension
                for (int row = 1; row < GAME_HEIGHT-1; row++) {
                    for (int col = 1; col < GAME_WIDTH-1; col++) {
                        if (grid[row][col] == 'o') {
                            wattron(win, COLOR_PAIR(3)); // * YELLOW for obstacles
                            mvwprintw(win, row * height / GAME_HEIGHT, col * width / GAME_WIDTH, "o");
                            wattroff(win, COLOR_PAIR(3));
                            continue;
                        }
                        if (strchr("0123456789", grid[row][col])) {
                            wattron(win, COLOR_PAIR(2)); // * GREEN for targets
                            mvwprintw(win, row * height / GAME_HEIGHT, col * width / GAME_WIDTH, "%c", grid[row][col]);
                            wattroff(win, COLOR_PAIR(2));
                        }
                    }
                }
                // * Attempt to read a character from the keyboard pipe (non-blocking)
                FD_ZERO(&read_keyboard);
                FD_SET(keyboard, &read_keyboard);
                timeout.tv_sec = 0;
                timeout.tv_usec = 1e6/FRAME_RATE; // * Frame rate of ~60Hz
                if (select(keyboard + 1, &read_keyboard, NULL, NULL, &timeout) > 0) {
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
                // * Clean the previous position of the drone in the map and draw the current
                mvwprintw(win, drone_pos[1]*height/GAME_HEIGHT, drone_pos[0]*width/GAME_WIDTH, " ");
                wattron(win, COLOR_PAIR(1)); // * BLUE for drone
                mvwprintw(win, drone_pos[3]*height/GAME_HEIGHT, drone_pos[2]*width/GAME_WIDTH, "+");
                wattroff(win, COLOR_PAIR(1));
                // * Compute the new forces of the drone
                command_drone(drone_force, c);
                // * Send the new grid to drone dynamics
                if (write(dynamic_write, grid, GAME_WIDTH * GAME_HEIGHT * sizeof(char)) == -1) {
                    perror("write target");
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
                // * Compute the mean velocity
                int vel_x = drone_pos[2] - prev_x;
                int vel_y = drone_pos[3] - prev_y;
                // * Remove any target along the path
                remove_target_on_path(grid, prev_x, prev_y, drone_pos[2], drone_pos[3]);
                // * Send the message containing foce, postion and velocity of the drone to the inspector window
                char insp_msg[128];
                char key;
                if (c == '\0') key = '-';
                else key = c;
                snprintf(insp_msg, sizeof(insp_msg), "%d,%d,%d,%d,%d,%d,%c", drone_force[0], -1*drone_force[1],
                    drone_pos[2], drone_pos[3], vel_x, vel_y, key);
                const int fd = open(INSPECTOR_FIFO, O_WRONLY);
                if (write(fd, insp_msg, strlen(insp_msg)) == -1) {
                    perror("write insp_pipe");
                    status = -1;
                    c = 'q';
                }
                close(fd);
                // * Update the traveled distance
                distance_traveled += abs(drone_pos[2] - prev_x) + abs(drone_pos[3] - prev_y);
                // * Compite the time
                int elapsed_time = (int)(time(NULL) - start_time);
                // * Count the remaining targets
                int count_targets = 0;
                for (int r = 0; r < GAME_HEIGHT; r++) {
                    for (int col = 0; col < GAME_WIDTH; col++) {
                        if (strchr("0123456789", grid[r][col]) != NULL)
                            count_targets++;
                    }
                }
                // * Compute the loss score
                score -= elapsed_time * 10 + distance_traveled * 5 + count_obstacles/((10 -count_targets) * 3000);
                if (score < 0) score = 0;
                if (count_targets == 0) {
                    status = -1;
                    c = 'q';
                    mvwprintw(win, height/2, width/2, "YOU WIN SCORE %d", score);
                }
                if (score <= 0) {
                    status = -1;
                    c = 'q';
                    mvwprintw(win, height/2, width/2, "GAME OVER");
                }
                if (c == 'q') {
                    status = -1;
                }
                break;
            }
            default: break;
        }
        // * See if the windows is resized
        int new_height = 0, new_width = 0;
        getmaxyx(stdscr, new_height, new_width);
        if (!(new_height == height && new_width == width)) {
            height = new_height;
            width = new_width;

            // * Delete old window and create a new one
            delwin(win);
            win = newwin(height, width, 0, 0);
        }
        // * Draw border for new window
        box(win, 0, 0);   // * Redraw border
        // * Stampa il punteggio a posizione y=0, x=4
        mvwprintw(win, 0, 4, "Score: %d", score);
        mvwprintw(win, 0, width-20, "Press q to quit");
        wrefresh(win);
        refresh();  // * Ensure standard screen updates
        // * Refresh the standard screen and the new window
        wrefresh(win);
        wrefresh(stdscr);
    } while (!(status == -1  && c == 'q')); // * Exit on 'q' and if status is -1

    // * Close the inspector window
    kill(-insp_pid, SIGTERM);
    waitpid(insp_pid, NULL, 0);
    // * Final cleanup
    if (win) {
        delwin(win);
    }
    endwin();

    for (int i = 0; i < NUM_CHILD_PIPES; i++) {
        close(read_fds[i]);
    }
    for (int i = 0; i < NUM_CHILD_PIPES - 1; i++) {
        close(write_fds[i]);
    }
    fclose(logfile);

    return EXIT_SUCCESS;
}

int parser(int argc, char *argv[], int *read_fds, int *write_fds) {
    /*
     * Parse the file descriptors and watchdog PID from the command-line arguments.
     * @param argc Number of arguments.
     * @param argv Array of arguments.
     * @param read_fds Array to store read file descriptors.
     * @param write_fds Array to store write file descriptors.
     * @param watchdog_pid Pointer to store the watchdog PID.
     * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
    */
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
    // * Parse logfile file descriptor and open it
    int logfile_fd = atoi(argv[argc - 1]);
    logfile = fdopen(logfile_fd, "a");
    if (!logfile) {
        perror("fdopen logfile");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void signal_triggered(int signum) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        fprintf(logfile, "[%02d:%02d:%02d] PID: %d - %s\n", t->tm_hour, t->tm_min, t->tm_sec, getpid(),
                "Blackboard is active.");
        fflush(logfile);
}

int initialize_ncurses() {
    /*
     * Initialize ncurses settings and create a new window.
     * @return Pointer to the newly created window, or NULL on failure.
     */
    // * Start curses mode
    if (initscr() == NULL) {
        return EXIT_FAILURE;
    }
    cbreak();  // * Disable line buffering
    noecho();  // * Don't echo pressed keys
    curs_set(FALSE);  // * Hide the cursor
    start_color();  // * Enable color functionality
    use_default_colors();  // * Allow default terminal colors
    // * Initialize color pairs
    init_pair(1, COLOR_BLUE, -1); // * Drone
    init_pair(2, COLOR_GREEN, -1); // * Targets
    init_pair(3, COLOR_YELLOW, -1); // * Obstacles

    return EXIT_SUCCESS;
}

void command_drone(int *drone_force, char c) {
    /*
     * Modify the drone force based on the input key.
     * Command keys:
     * 'w': Up Left, 'e': Up, 'r': Up Right or Reset,
     * 's': Left or Suspend, 'd': Brake, 'f': Right,
     * 'x': Down Left, 'c': Down, 'v': Down Right,
     * 'p': Pause, 'q': Quit
     * -------
     * @param drone_force Array representing the drone's force.
     * @param c The input character.
    */
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

pid_t launch_inspection_window() {
    /*
     * Launches a new terminal window running the "inspector" program.
     * Returns:
     * @return pid The process ID (pid) of the newly created child process.
    */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid == 0) {
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            exit(EXIT_FAILURE);
        }
        execlp("gnome-terminal", "gnome-terminal", "--disable-factory", "--", "bash", "-c", "./inspector; exec bash", (char *)NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }
    return pid;
}

void remove_target_on_path(char grid[GAME_HEIGHT][GAME_WIDTH], int x0, int y0, int x1, int y1) {
    // * To see more about this -> "https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm"
    // * Compute the directions
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    // * Starting Bresenham's error
    int err = dx + dy;
    while (1) {
        // * REmove the target if it is inside the grid
        if (x0 >= 0 && x0 < GAME_WIDTH && y0 >= 0 && y0 < GAME_HEIGHT) {
            if (strchr("0123456789", grid[y0][x0]) != NULL) {
                grid[y0][x0] = ' ';
            }
        }
        // * Stop when it is reached the last point (x1, y1)
        if (x0 == x1 && y0 == y1) {
            break;
        }
        // * Update the e2
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0  += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0  += sy;
        }
    }
}