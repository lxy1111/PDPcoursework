#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "route_map.h"
#include "mpi.h"

#define ROUTES_MAX 100
#define BLOCKED_CELL -20
#define LOW_SCORE -10

// Data structure to hold each route, the start and target ports along with the route itself
struct specific_route
{
  int start_x, start_y, target_x, target_y;
  int *route;
};

int size_x, size_y, current_route_index, num_blocked_cells;

int local_nx, size, myrank, basex, mem_size_x, mem_size_y;

int *blocked_cells_x;                     // X coordinates of blocked sea cells (e.g. islands)
int *blocked_cells_y;                     // Y coordinates of blocked sea cells (e.g. islands)
struct specific_route routes[ROUTES_MAX]; // All routes that we have planned

static int generate_score(int, int, int, int, int, int);
static void display_specific_route(struct specific_route *);
static bool is_cell_blocked(int, int);
void perform_halo_swap(int myrank, int size, int local_nx, int ny, int mem_size_y, int *data);

// You can uncomment this main function and compile independently to get a feeling for how the route planning works.
// This will set up a size of 16 by 16 grid with two blocked cells, and plan a route working around these blockages.
// Note this is just a testing/illustration main function driver and you will need to leave this commented out (or delete)
// to compile the code as part of the entire simulation
/*
int main() {
  size_x=16;
  size_y=16;
  current_route_index=0;
  num_blocked_cells=2;


  blocked_cells_x=(int*) malloc(sizeof(int) * 2);
  blocked_cells_y=(int*) malloc(sizeof(int) * 2);  
  blocked_cells_x[0]=2;
  blocked_cells_y[0]=12;

  blocked_cells_x[1]=5;
  blocked_cells_y[1]=15;

  int route=generate_route(0, 10, 14, 15);
  display_specific_route(&routes[route]);
}
*/

// Called from the main program to initialse the routemaps based on the configuration of the simulation
// that has been loaded in elsewhere
void initialise_routemap(struct simulation_configuration_struct *simulation_configuration, int local_nx, int myrank, int size, int basex)
{
  size_x = simulation_configuration->size_x;
  size_y = simulation_configuration->size_y;

  local_nx = local_nx;
  myrank = myrank;
  size = size;
  basex = basex;

  current_route_index = 0;
  num_blocked_cells = simulation_configuration->number_islands;
  blocked_cells_x = (int *)malloc(sizeof(int) * num_blocked_cells);
  blocked_cells_y = (int *)malloc(sizeof(int) * num_blocked_cells);
  for (int i = 0; i < simulation_configuration->number_islands; i++)
  {
    blocked_cells_x[i] = simulation_configuration->islands[i].x;
    blocked_cells_y[i] = simulation_configuration->islands[i].y;
  }
}

// Calculates the routes that have been specified in the configuration. These planned routes are then stored here and can be
// used during the simulation. Note that if it is not possible to plan a route (there are some limitation to the planning logic)
// then an error is displayed
void calculate_routes(struct simulation_configuration_struct *simulation_configuration, int (*generate_route_strategy)(int, int, int, int))
{
  for (int i = 0; i < simulation_configuration->number_ports; i++)
  {
    for (int j = 0; j < simulation_configuration->number_ports; j++)
    {
      if (i != j)
      {
        int route_index = generate_route_strategy(simulation_configuration->ports[i].x, simulation_configuration->ports[i].y,
                                                  simulation_configuration->ports[j].x, simulation_configuration->ports[j].y);

        if (route_index == -1)
        {
          fprintf(stderr, "Error, can not plan a route between points X=%d,Y=%d and X=%d,Y=%d\n",
                  simulation_configuration->ports[i].x, simulation_configuration->ports[i].y,
                  simulation_configuration->ports[j].x, simulation_configuration->ports[j].y);
        }
        else
        {
          // Swap the boundary values between processes in order for the convenience of getNextCell
          perform_halo_swap(myrank, size, local_nx, size_y, mem_size_y, routes[route_index].route);

          simulation_configuration->ports[i].target_route_indexes[j] = route_index;
          // By commenting out the following two lines you can see the routes planned
          //display_specific_route(&routes[route_index]);
        }
      }
    }
  }
}

// Given the route index, and current X and Y location of a ship this will determine the next X and Y locations
// that the ship should move to in the domain. This part is optimized. In the serial version, it traverses all the
// coordinates. In this version, we only need to care about the eight grids around the current grid and return the
// direction
void getNextCell(int routeIndex, int currentX, int currentY, int *nextX, int *nextY)
{
  int currentRouteCounter = routes[routeIndex].route[(currentX - basex + 1) * mem_size_y + currentY + 1];

  for (int i = -1; i <= 1; i++)
  {
    for (int j = -1; j <= 1; j++)
    {
      if (currentX + i >= 0 && currentX + i < size_x && currentY + j >= 0 && currentY + j < size_y && routes[routeIndex].route[((currentX - basex + 1 + i) * mem_size_y) + currentY + 1 + j] == currentRouteCounter + 1)
      {
        *nextX = i;
        *nextY = j;

        return;
      }
    }
  }
}

// Given the starting X and Y coordinate of a port, and the target port's X and Y coordinate, this function will plan a route from the
// starting port to the target one. The unique index of the planned route is returned, and the route will work around any blockages in
// the sea such as islands. This uses a simple scoring approach to determine the unidirectional route (so ships will progress by
// following the next number along on the grid)
int generate_route(int cell_source_x, int cell_source_y, int cell_target_x, int cell_target_y)
{
  routes[current_route_index].start_x = cell_source_x;
  routes[current_route_index].start_y = cell_source_y;
  routes[current_route_index].target_x = cell_target_x;
  routes[current_route_index].target_y = cell_target_y;

  mem_size_x = local_nx + 2;
  mem_size_y = size_y + 2;

  // Decompose the route
  routes[current_route_index].route = (int *)malloc(sizeof(int) * mem_size_x * mem_size_y);

  for (int i = 1; i <= local_nx; i++)
  {
    for (int j = 1; j <= size_y; j++)
    {
      if (is_cell_blocked(basex + i - 1, j - 1))
      {
        // If the cell is blocked then it is assigned the value -1
        routes[current_route_index].route[(i * mem_size_y) + j] = -1;
      }
      else
      {
        // If the cell is eligable then assign it to be zero score, this will be updated
        // if the cell is part of the route between the source and target
        routes[current_route_index].route[(i * mem_size_y) + j] = 0;
      }
    }
  }
  int grid_scores[3][3];

  if (cell_source_x - basex < local_nx && cell_source_x - basex >= 0)
  {
    routes[current_route_index].route[((cell_source_x - basex + 1) * mem_size_y) + cell_source_y + 1] = 0; // Starting port is assigned zero score
  }
  int current_x = cell_source_x;
  int current_y = cell_source_y;
  bool found_route = false;
  int routeCounter = 1;

  // This works by starting at the start port and exploring all possible movements in X and Y (9 possible movements). Each of these is scored
  // according to whether it is closer to the target port or not (or blocked etc) with the highest score being if an advance is made in both
  // dimensions and slightly lower if an advance was just made in one dimension etc.. The the highest scoring cell is then chosen for the movement
  // and this is set to the current x and y, with the algorithm looping through
  for (int number_steps = 0; number_steps < size_x * size_y && !found_route; number_steps++)
  {
    grid_scores[1][1] = LOW_SCORE; // "Moving" to the current cell is scored arbitrarily lowly as we don't want to stay here
    grid_scores[0][0] = generate_score(current_x, current_y, cell_target_x, cell_target_y, -1, -1);
    grid_scores[0][1] = generate_score(current_x, current_y, cell_target_x, cell_target_y, -1, 0);
    grid_scores[0][2] = generate_score(current_x, current_y, cell_target_x, cell_target_y, -1, 1);
    grid_scores[1][0] = generate_score(current_x, current_y, cell_target_x, cell_target_y, 0, -1);
    grid_scores[1][2] = generate_score(current_x, current_y, cell_target_x, cell_target_y, 0, 1);
    grid_scores[2][0] = generate_score(current_x, current_y, cell_target_x, cell_target_y, 1, -1);
    grid_scores[2][1] = generate_score(current_x, current_y, cell_target_x, cell_target_y, 1, 0);
    grid_scores[2][2] = generate_score(current_x, current_y, cell_target_x, cell_target_y, 1, 1);
    int current_best = LOW_SCORE, best_x = 0, best_y = 0;
    for (int i = 0; i < 3; i++)
    {
      for (int j = 0; j < 3; j++)
      {
        if (grid_scores[i][j] > current_best)
        {
          // Here we are searching for the highest score in the 9 possible movements and set best_x and best_y to
          // be the offset movement in that direction
          best_x = i - 1;
          best_y = j - 1;
          current_best = grid_scores[i][j];
        }
      }
    }
    if (current_best == LOW_SCORE)
      break; // If we are here then no valid step has been found from this point, therefore abort
    // Update current X and current Y with the cell we have identified moving to
    current_x = current_x + best_x;
    current_y = current_y + best_y;

    // If the current X and current Y are the target port then we have arrived and job done!
    if (current_x == cell_target_x && current_y == cell_target_y)
      found_route = true;
    if (current_x - basex < local_nx && current_x - basex >= 0)
    {
      routes[current_route_index].route[((current_x - basex + 1) * mem_size_y) + current_y + 1] = routeCounter;
    }
    routeCounter++;
  }

  if (found_route)
  {
    current_route_index++;
    return current_route_index - 1;
  }
  else
  {
    return -1;
  }
}

// Performs the halo swap of the boundary grids of route
void perform_halo_swap(int myrank, int size, int local_nx, int ny, int mem_size_y, int *data)
{
  MPI_Request requests[] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL};

  if (myrank > 0)
  {
    MPI_Isend(&data[1 + mem_size_y], ny, MPI_INT, myrank - 1, 0, MPI_COMM_WORLD, &requests[0]);

    MPI_Irecv(&data[1], ny, MPI_INT, myrank - 1, 0, MPI_COMM_WORLD, &requests[1]);
  }
  if (myrank < size - 1)
  {
    MPI_Isend(&data[(local_nx * mem_size_y) + 1], ny, MPI_INT, myrank + 1, 0, MPI_COMM_WORLD, &requests[2]);

    MPI_Irecv(&data[((local_nx + 1) * mem_size_y) + 1], ny, MPI_INT, myrank + 1, 0, MPI_COMM_WORLD, &requests[3]);
  }

  MPI_Waitall(4, requests, MPI_STATUSES_IGNORE);
}

// Given an x and y coordinate this will determine whether that cell is blocked or not
static bool is_cell_blocked(int x, int y)
{
  for (int i = 0; i < num_blocked_cells; i++)
  {
    if (blocked_cells_x[i] == x && blocked_cells_y[i] == y)
      return true;
  }
  return false;
}

// Given the starting X and Y coordinate, the target X and Y coordinate and the offset movement in the X and Y dimension this function will
// return the score of moving by this offset. I.e. is it quantatiatively good or bad in terms of making progress towards the final destination?
static int generate_score(int cell_source_x, int cell_source_y, int cell_target_x, int cell_target_y, int offset_x, int offset_y)
{
  if (cell_source_x + offset_x < 0)
    return LOW_SCORE;
  if (cell_source_y + offset_y < 0)
    return LOW_SCORE;
  if (cell_source_x + offset_x >= size_x)
    return LOW_SCORE;
  if (cell_source_y + offset_y >= size_y)
    return LOW_SCORE;
  if (is_cell_blocked(cell_source_x + offset_x, cell_source_y + offset_y))
    return LOW_SCORE;
  int x_diff = abs(cell_target_x - cell_source_x) - abs(cell_target_x - (cell_source_x + offset_x));
  int y_diff = abs(cell_target_y - cell_source_y) - abs(cell_target_y - (cell_source_y + offset_y));
  return x_diff + y_diff;
}

// Helper function to display a specific route on stdio, helps with debugging
static void display_specific_route(struct specific_route *route_to_display)
{
  for (int i = 0; i < size_x; i++)
  {
    char displayLine[100];
    displayLine[0] = '\0';
    for (int j = 0; j < size_y; j++)
    {
      int val = route_to_display->route[(i * size_y) + j];
      if (val >= 0)
      {
        sprintf(displayLine, "%s %d", displayLine, val);
      }
      else
      {
        sprintf(displayLine, "%s X", displayLine);
      }
    }
    printf("%s\n", displayLine);
  }
}
