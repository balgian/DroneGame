//
// Created by Gian Marco Balia
//
// src/drone_dynamics.c
#include <macros.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

int main(int argc, char *argv[]) {
  /*
     * Dynamics process
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

    if (read(read_fd, grid, height * width * sizeof(char)) == -1) {
      perror("read");
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
    int Fx = 0;
    int Fy = 0;

    // * Obstacles' repultive force
    const double eta = 2.0;  // *Repulsion scaling factor
    const double rho_o = 4.0;  // * Influence distance for repulsion
    const double min_rho_o = sqrt(2.0);
    // * Targets' attractive force
    const double epsilon = 1.0;  // * Actractive scaling factor
    const double rho_t = 4.0;  // * Influence distance for actraction
    const double min_rho_t = sqrt(2.0);

    for (int i = 0; i < height; i++) {
      for (int j = 0; j < width; j++) {
        const int dx = x[1] - i;
        const int dy = y[1] - j ;
        double dist = sqrt(dx*dx + dy*dy);

        dist = dist < min_rho_o ? min_rho_o : dist;
        if (dist < rho_o && strchr("o+-|", grid[i][j])) {
          Fx -= (int)(eta*(1/dist - 1/rho_o)*dx/pow(dist,3));
          Fy -= (int)(eta*(1/dist - 1/rho_o)*dy/pow(dist,3));
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

    Fx += force_x;
    Fy += force_y;

    // * Compute the position from the force
    const int x_new = (int)((Fx*T*T + (2*M + K*T)*x[1] - M*x[0])/(M + K*T));
    const int y_new = (int)((Fy*T*T + (2*M + K*T)*y[1] - M*y[0])/(M + K*T));

    char out_buf[32];
    sprintf(out_buf, "%d,%d", x_new, y_new);
    if (write(write_fd, out_buf, sizeof(out_buf)) == -1) {
      perror("write");
      return EXIT_FAILURE;
    }
    usleep(1000);
  }
  return EXIT_SUCCESS;
}
