//
// Created by Gian Marco Balia
//
// src/drone_dynamics.c
#include <macros.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>

FILE *logfile;

ssize_t robust_read(int fd, void *buf, size_t count) {
  /*
   * Ensures that exactly `count` bytes are read from the file descriptor `fd`.
   * Returns the total number of bytes read on success, or -1 on failure.
  */
  size_t bytes_read = 0;
  char *buffer = buf;
  while (bytes_read < count) {
    ssize_t result = read(fd, buffer + bytes_read, count - bytes_read);
    if (result == -1) {
      if (errno == EINTR) continue; // Retry if interrupted
      return -1; // Other errors
    }
    if (result == 0) break; // EOF
    bytes_read += result;
  }
  return bytes_read;
}

void signal_triggered(int signum) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  fprintf(logfile, "[%02d:%02d:%02d] PID: %d - %s\n", t->tm_hour, t->tm_min, t->tm_sec, getpid(),
      "Dynamics is active.");
  fflush(logfile);
}

int main(int argc, char *argv[]) {
  /*
     * Dynamics process
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
    fprintf(stderr, "Invalid write file descriptor: %s\n", argv[2]);
    return EXIT_FAILURE;
  }

  // * Parse logfile file descriptors
  int logfile_fd = atoi(argv[argc - 1]);
  logfile = fdopen(logfile_fd, "a");
  if (!logfile) {
    perror("fdopen logfile");
    return EXIT_FAILURE;
  }

  // * Receive the game dimention
  char in_buf[32];
  if (read(read_fd, in_buf, sizeof(in_buf)) < 0) {
    perror("read");
    return EXIT_FAILURE;
  }

  int height, width;
  if (sscanf(in_buf, "%d,%d", &height, &width) != 2) {
    fprintf(stderr, "Failed to parse height,width from '%s'\n", in_buf);
    return EXIT_FAILURE;
  }

  // * Physic parameters
  const double M = 1.0;
  const double K = 1.0;
  const double T = 1.0/FRAME_RATE;
  while(1) {
    // * Receive the updated map
    char grid[height][width];
    memset(grid, ' ', height*width);

    if (robust_read(read_fd, grid, height * width * sizeof(char)) != height * width * sizeof(char)) {
      perror("robust_read grid");
      return EXIT_FAILURE;
    }

    int x[2], y[2], force_x, force_y;
    char msg[100];
    if (read(read_fd, msg, sizeof(msg)) == -1) {
      perror("read");
      return EXIT_FAILURE;
    }

    if (sscanf(msg, "%d,%d,%d,%d,%d,%d", &x[0], &y[0], &x[1], &y[1], &force_x, &force_y) != 6) {
      fprintf(stderr, "Failed to parse message: %s\n", msg);
      return EXIT_FAILURE;
    }
    // * Total force
    double Fx = 0.0;
    double Fy = 0.0;

    // * Obstacles' repultive force
    const double eta = 3.0;  // *Repulsion scaling factor
    const double rho_o = 3.0;  // * Influence distance for repulsion
    const double min_rho_o = 1;
    // * Targets' attractive force
    const double epsilon = 1.3;  // * Actractive scaling factor
    const double rho_t = 4.0;  // * Influence distance for actraction
    const double min_rho_t = 1;

    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        const int dx = x[1] - j;
        const int dy = y[1] - i ;
        double dist = sqrt(dx*dx + dy*dy);

        dist = dist < min_rho_o ? min_rho_o : dist;
        if (dist < rho_o && strchr("o+-|", grid[i][j])) {
          Fx -= eta*(1/dist - 1/rho_o)*dx/pow(dist,3);
          Fy -= eta*(1/dist - 1/rho_o)*dy/pow(dist,3);
          continue;
        }

        dist = sqrt(dx*dx + dy*dy);
        dist = dist < min_rho_t ? min_rho_t : dist;
        if (dist < rho_t && strchr("0123456789", grid[i][j])) {
          Fx -= epsilon*dx/dist;
          Fy -= epsilon*dy/dist;
        }
      }
    }

    Fx += (double)force_x;
    Fy += (double)force_y;

    // * Compute the position from the force
    int x_new = x[1] + force_x;//(int)((Fx*T*T + (2*M + K*T)*x[1] - M*x[0])/(M + K*T));
    int y_new = y[1] + force_y;//(int)((Fy*T*T + (2*M + K*T)*y[1] - M*y[0])/(M + K*T));

    char out_buf[32];
    sprintf(out_buf, "%d,%d", x_new, y_new);
    if (write(write_fd, out_buf, sizeof(out_buf)) == -1) {
      perror("write");
      return EXIT_FAILURE;
    }
    usleep(2000);
  }
  return EXIT_SUCCESS;
}
