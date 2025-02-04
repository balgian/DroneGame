//
// Created by Gian Marco Balia
//
// main.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include "macros.h"

void write_log(FILE *logfile, pid_t pid, const char *message) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(logfile, "[%02d:%02d:%02d] PID: %d - %s\n",
            t->tm_hour, t->tm_min, t->tm_sec, pid, message);
    fflush(logfile);
}

int create_pipes(int pipes[NUM_CHILDREN_WITH_PIPES][2]) {
    /*
    * Function to create NUM_PIPES pipes.
    * @param pipes An array to store the file descriptors of the pipes.
    * @return 0 on success, -1 on failure.
    */
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            // * Close any previously opened pipes before exiting
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return -1;
        }
    }
    return 0;
}

int create_processes(int pipes[NUM_CHILDREN_WITH_PIPES][2], pid_t pids[NUM_CHILDREN_WITH_PIPES], pid_t pgid) {
    /*
     * Function to create child and blackboard processes.
     * @param pipes An array containing the file descriptors of the pipes.
     * @param pids An array to store the PIDs of the child processes.
     * @return 0 on success, -1 on failure.
     */
    // * Array of executable paths corresponding to each child process
    const char *child_executables[NUM_CHILDREN_WITH_PIPES] = {
        "./keyboard_manager",
        "./obstacles",
        "./targets_generator",
        "./drone_dynamics",
    };
    /*
     * Create 5 processes:
     * - 0: Keyboard input manager (read & write)
     * - 1: Obstacle generator (read & write)
     * - 2: Target generators (read & write)
     * - 3: Drone dynamics process (only write)
    */
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            // * Cleanup: kill any previously created children
            for (int k = 0; k < i; k++) {
                kill(pids[k], SIGTERM);
            }
            return -1;
        }
        if (pids[i] == 0) {
            // * Join the main process's process group
            if (setpgid(0, pgid) == -1) {
                perror("setpgid in child");
                exit(EXIT_FAILURE);
            }

            // * Close all pipes except the child’s own
            for (int j = 0; j < NUM_CHILDREN_WITH_PIPES; j++) {
                if (j != i) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            // * If this is child #0 (keyboard_manager), it's write-only. So we close the read end of pipe[i], keep the write end open.
            if (i == 0) {
                close(pipes[i][0]); // * child #0 does NOT read
                char write_pipe_str[10];
                snprintf(write_pipe_str, sizeof(write_pipe_str), "%d", pipes[i][1]);
                execl(child_executables[i], child_executables[i], write_pipe_str, NULL);
            } else {
                char read_pipe_str[10], write_pipe_str[10];
                snprintf(read_pipe_str, sizeof(read_pipe_str), "%d", pipes[i][0]);
                snprintf(write_pipe_str, sizeof(write_pipe_str), "%d", pipes[i][1]);
                execl(child_executables[i], child_executables[i], read_pipe_str, write_pipe_str, NULL);
            }

            // * If execl returns, an error occurred
            perror("execl");
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

pid_t create_watchdog_process(pid_t pgid) {
    /*
     * Function to create the watchdog process.
     * @return PID of the watchdog in case of success, -1 in case of failure.
     */
    const pid_t watchdog_pid = fork();
    if (watchdog_pid < 0) {
        perror("fork watchdog");
        return -1;
    }
    if (watchdog_pid == 0) {
        char pgid_str[10];
        snprintf(pgid_str, sizeof(pgid_str), "%d", pgid);
        // * Execute the watchdog executable
        execl("./watchdog", "watchdog", pgid_str, NULL);


        // * If execl returns, an error occurred
        perror("execl");
        exit(EXIT_FAILURE);
    }

    return watchdog_pid;
}

pid_t create_blackboard_process(int pipes[NUM_CHILDREN_WITH_PIPES][2], const pid_t watchdog_pid, int logfile_fd, pid_t pgid) {
    /*
     * Function to create the blackboard process.
     * @param pipes Array containing the file descriptors of the pipes.
     * @param watchdog_pid PID of the watchdog process.
     * @return PID of the blackboard in case of success, -1 in case of failure.
    */
    const pid_t blackboard_pid = fork();
    if (blackboard_pid < 0) {
        perror("fork blackboard");
        return -1;
    }
    if (blackboard_pid == 0) {
        // * Join the main process's process group
        if (setpgid(0, pgid) == -1) {
            perror("setpgid in child");
            exit(EXIT_FAILURE);
        }

        // * Close write ends in the blackboard for keyboard_manager
        close(pipes[0][1]);

        /*
         * Prepare arguments for the blackboard executable
         * Pass all read pipe descriptors and the watchdog PID
         * args[0] = "./blackboard"
         * args[1..NUM_CHILDREN_WITH_PIPES] = read_fds
         * args[NUM_CHILDREN_WITH_PIPES + 1..2*NUM_CHILDREN_WITH_PIPES - 1] = write_fds (excluding keyboard_manager)
         * args[2*NUM_CHILDREN_WITH_PIPES] = watchdog_pid
        */
        char watchdog_pid_str[10];
        snprintf(watchdog_pid_str, sizeof(watchdog_pid_str), "%d", watchdog_pid);
        char logfile_fd_str[10];
        snprintf(logfile_fd_str, sizeof(logfile_fd_str), "%d", logfile_fd);

        // * Allocate memory for arguments
        // ! executable name + read_fds + write_fds (excluding keyboard_manager) + NULL
        const int total_args = 2 * NUM_CHILDREN_WITH_PIPES + 1;
        char **args = malloc(total_args * sizeof(char *));
        if (!args) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        args[0] = "./blackboard";

        // * Add all read_fds
        int arg_index = 1;
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            char *read_pipe_str = malloc(10);
            if (!read_pipe_str) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            snprintf(read_pipe_str, 10, "%d", pipes[i][0]);
            args[arg_index++] = read_pipe_str;
        }

        // * Add all write_fds (excluding keyboard_manager)
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            if (i == 0) continue; // Skip keyboard_manager for write_fds
            char *write_pipe_str = malloc(10);
            if (!write_pipe_str) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            snprintf(write_pipe_str, 10, "%d", pipes[i][1]);
            args[arg_index++] = write_pipe_str;
        }
        // * Add watchdog_pid
        args[arg_index++] = watchdog_pid_str;
        args[arg_index++] = logfile_fd_str;
        args[arg_index] = NULL; // * NULL terminate the argument list

        // * Execute the blackboard executable with the necessary arguments
        execvp(args[0], args);

        // * If execvp returns, an error occurred
        perror("execvp blackboard");

        // * Free allocated memory
        for (int i = 1; i < arg_index; i++) {
            free(args[i]);
        }
        free(args);
        exit(EXIT_FAILURE);
    }

    return blackboard_pid;
}

int main(void) {
    // * Set the main process as the leader of a new process group
    pid_t pgid = setpgid(0, getpid());
    if (pgid == -1) {
        perror("setpgid");
        exit(EXIT_FAILURE);
    }

    int pipes[NUM_CHILDREN_WITH_PIPES][2]; // * Array to hold pipe file descriptors
    pid_t pids[NUM_CHILDREN_WITH_PIPES]; // * Array to hold child and blackboard PIDs

    // * Step 1: Create pipes
    if (create_pipes(pipes) == -1) {
        fprintf(stderr, "Failed to create pipes.\n");
        exit(EXIT_FAILURE);
    }

    // * Step 2: Create the logfile
    FILE *logfile = fopen("logfile.txt", "w");
    if (!logfile) {
        perror("Errore apertura logfile");
        exit(EXIT_FAILURE);
    }
    int logfile_fd = fileno(logfile);
    write_log(logfile, getpid(), "Master process started.");

    // * Step 3: Create processes that use pipes
    if (create_processes(pipes, pids, pgid) == -1) {
        fprintf(stderr, "Failed to create processes.\n");
        // * Close all pipes before exiting
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        exit(EXIT_FAILURE);
    }

    // * Step 4: Create the Watchdog process
    const pid_t watchdog_pid = create_watchdog_process(pgid);
    if (watchdog_pid == -1) {
        fprintf(stderr, "Failed to create watchdog process.\n");
        // * Terminate already created child processes
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            kill(pids[i], SIGTERM);
        }
        // * Close all pipes before exiting
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        exit(EXIT_FAILURE);
    }

    // * Step 5: Create the Blackboard Process
    const pid_t blackboard_pid = create_blackboard_process(pipes, watchdog_pid, logfile_fd, pgid);
    if (blackboard_pid == -1) {
        fprintf(stderr, "Failed to create blackboard process.\n");
        // * Terminate child processes and watchdog
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            kill(pids[i], SIGTERM);
        }
        kill(watchdog_pid, SIGTERM);
        // * Close all pipes before exiting
        for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        exit(EXIT_FAILURE);
    }

    // ! Step 6: Close All Pipes in the Parent Process
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // ! Step 7: Wait for All Child Processes and Blackboard to Finish
    for (int i = 0; i < NUM_CHILDREN_WITH_PIPES; i++) {
        waitpid(pids[i], NULL, 0);
    }
    waitpid(blackboard_pid, NULL, 0);
    waitpid(watchdog_pid, NULL, 0);

    return 0;
}