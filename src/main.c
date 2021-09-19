#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "simulation_configuration.h"
#include "simulation_support.h"
#include "route_map.h"
#include "mpi.h"

#define MAX_SHIPS_PER_CELL 200
#define ROUTE_PLANNER_TO_USE 0
#define SIMULATION_TO_USE 0

// Data associated with each ship
struct ship_struct
{
  int route, hoursAtSea, id, cargoAmount;
  bool willMoveThisTimestep;
};

// Data associated with each port
struct port_struct
{
  int shipsInPastHundredHours[10];
  int port_index, cargoShipped, cargoArrived;
};

// Each cell in the domain
// x=X coordinate of the cell
// y=Y coordiante of the cell
// isWater=is the cell water (sea) that the ship can sail on
// isPort=is the cell a port
// isIsland=is the cell an island that must be avoided
// number_ships=the number of ships that currently reside in this cell
struct cell_struct
{
  int x, y;
  bool isWater, isPort, isIsland;
  struct port_struct port_data;
  struct ship_struct *ships_data[MAX_SHIPS_PER_CELL];
  int number_ships;
};

// The domain in the serial version is divided into sub_domain in the parallel version
struct cell_struct *sub_domain;
int currentShipId = 0;
int basex = 0;
int size, myrank, nx, ny, local_nx;

// Data type for defining ship
MPI_Datatype shiptype;

static void finalise_simulation();
static void run_simulation(struct simulation_configuration_struct *, void (*)(int, int), void (*)(struct simulation_configuration_struct *), void (*)(struct simulation_configuration_struct *), void (*)(int, int, int, int *, int *), int (*)(struct cell_struct *), void (*)());
static void run_route_planner(struct simulation_configuration_struct, int, int, int, int, int (*)(int, int, int, int));
static void init_simulation(int, int);
static void initialiseDomain(struct simulation_configuration_struct *);
static void initialisePort(struct simulation_configuration_struct *, struct cell_struct *, int, int);
static void reportFinalInformation(struct simulation_configuration_struct *);
static void updateProperties(struct simulation_configuration_struct *);
static void updateMovement(struct simulation_configuration_struct *, void (*)(int, int, int, int *, int *), int (*)(struct cell_struct *));
static void processPort(struct simulation_configuration_struct *, struct cell_struct *);
static void processWater(struct cell_struct *, int);
static int findFreeShipIndex(struct cell_struct *);
static void reportStatistics(struct simulation_configuration_struct *, int);
static void reportGeneralStatistics(struct simulation_configuration_struct *, int);
static void perform_halo_swap(int, int, int, int, int);
static void initializeHalos();

// Program entry point, loads up the configuration and runs the simulation
int main(int argc, char *argv[])
{
  struct port_struct port;
  struct ship_struct ship;
  struct cell_struct cell;

  if (argc < 1)
  {
    fprintf(stderr, "You must provide the simulation configuration as an input parameter\n");
    return -1;
  }

  // Initialize MPI
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  // Define derived data type for ship_struct
  int length[5] = {1, 1, 1, 1, 1};
  MPI_Aint disp[5], base;
  MPI_Datatype type[5];

  MPI_Get_address(&ship.route, &disp[0]);
  MPI_Get_address(&ship.hoursAtSea, &disp[1]);
  MPI_Get_address(&ship.id, &disp[2]);
  MPI_Get_address(&ship.cargoAmount, &disp[3]);
  MPI_Get_address(&ship.willMoveThisTimestep, &disp[4]);

  base = disp[0];
  disp[0] = disp[0] - base;
  disp[1] = disp[1] - base;
  disp[2] = disp[2] - base;
  disp[3] = disp[3] - base;
  disp[4] = disp[4] - base;

  type[0] = MPI_INT;
  type[1] = MPI_INT;
  type[2] = MPI_INT;
  type[3] = MPI_INT;
  type[4] = MPI_C_BOOL;

  // Commit the data type called shiptype
  MPI_Type_create_struct(5, length, disp, type, &shiptype);
  MPI_Type_commit(&shiptype);

  struct simulation_configuration_struct simulation_configuration;
  parseConfiguration(argv[1], &simulation_configuration);

  // calculate the size for sub_domain
  nx = simulation_configuration.size_x;
  ny = simulation_configuration.size_y;
  local_nx = nx / size;

  if (local_nx * size < nx)
  {
    int specialranks = nx - local_nx * size;
    if (myrank < specialranks)
    {
      local_nx++;
      basex = myrank * local_nx;
    }
    else
    {
      basex = specialranks * (local_nx + 1) + (myrank - specialranks) * local_nx;
    }
  }
  else
  {
    basex = myrank * local_nx;
  }

// This is a resuable framework for route planner. If there are different ways of generating route, just add ROUTE_PLANNER_TO_USE
// and write the corresponding function
#if ROUTE_PLANNER_TO_USE == 0
  run_route_planner(simulation_configuration, local_nx, myrank, size, basex, generate_route);
#endif

// This is a framework to make the program reusable. If there are more ways of simulation, just add SIMULATION_TO_USE
// and write the corresponding simualtion functions
#if SIMULATION_TO_USE == 0
  run_simulation(&simulation_configuration, init_simulation, initialiseDomain, updateProperties, getNextCell, findFreeShipIndex, finalise_simulation);
#endif

  MPI_Finalize();
  return 0;
}

// Decompose the domain and separate it into sub_domains for each process
static void init_simulation(int mem_size_x, int mem_size_y)
{
  sub_domain = (struct cell_struct *)malloc(sizeof(struct cell_struct) * mem_size_x * mem_size_y);
}

// Free sub_domain
static void finalise_simulation()
{
  free(sub_domain);
}

// start route planning
static void run_route_planner(struct simulation_configuration_struct simulation_configuration, int local_nx, int myrank, int size, int basex, int (*generate_route_strategy)(int, int, int, int))
{
  initialise_routemap(&simulation_configuration, local_nx, myrank, size, basex);
  initialiseSimulationSupport();

  // Parallelize the route planning and record the time
  MPI_Barrier(MPI_COMM_WORLD);

  double time1 = MPI_Wtime();

  calculate_routes(&simulation_configuration, generate_route_strategy);

  MPI_Barrier(MPI_COMM_WORLD);

  double time2 = MPI_Wtime();

  if (myrank == 0)
  {
    printf("The time of route planning is %g\n", time2 - time1);
  }
}

// Start simulation
static void run_simulation(struct simulation_configuration_struct *simulation_configuration, void (*init_simulation)(int, int), void (*initialise_domain_strategy)(struct simulation_configuration_struct *), void (*update_properties_strategy)(struct simulation_configuration_struct *), void (*get_next_cell_strategy)(int, int, int, int *, int *), int (*find_fresh_index_strategy)(struct cell_struct *), void (*finalise_simulation)())
{
  int mem_size_x = local_nx + 2;
  int mem_size_y = ny + 2;

  init_simulation(mem_size_x, mem_size_y);

  MPI_Barrier(MPI_COMM_WORLD);
  double time1 = MPI_Wtime();

  initialise_domain_strategy(simulation_configuration);

  int hours = 0;

  // Run the parallelized simulation - will loop through the configured number of timesteps
  for (int i = 0; i < simulation_configuration->number_timesteps; i++)
  {
    update_properties_strategy(simulation_configuration);

    updateMovement(simulation_configuration, get_next_cell_strategy, find_fresh_index_strategy);

    if (i % simulation_configuration->reportStatsEvery == 0)
      reportGeneralStatistics(simulation_configuration, hours);
    hours += simulation_configuration->dt; // Update the simulation hours by dt which is the number of hours per timestep
  }
  MPI_Barrier(MPI_COMM_WORLD);
  double time2 = MPI_Wtime();

  if (myrank == 0)
  {
    printf("The time of simulation is %g\n", time2 - time1);
  }

  reportFinalInformation(simulation_configuration);

  finalise_simulation();
}

// Reports the final information about the simulation when it is about to terminate
static void reportFinalInformation(struct simulation_configuration_struct *simulation_configuration)
{
  int *statistics = NULL;
  int len = 0;
  MPI_Request request1, request2;
  MPI_Status status;
  if (myrank == 0)
  {
    printf("======= Final report at %d hours =======\n", simulation_configuration->dt * simulation_configuration->number_timesteps);
  }

  for (int j = 1; j <= local_nx; j++)
  {
    for (int k = 1; k <= ny; k++)
    {
      struct cell_struct *specific_cell = &sub_domain[(j * (ny + 2)) + k];
      if (specific_cell->isPort)
      {
        len += 3;
        statistics = (int *)realloc(statistics, sizeof(int) * len);

        statistics[len - 3] = specific_cell->port_data.port_index;
        statistics[len - 2] = specific_cell->port_data.cargoShipped;
        statistics[len - 1] = specific_cell->port_data.cargoArrived;

        if (myrank == 0)
        {
          printf("Port %d shipped %d tonnes and %d arrived\n", specific_cell->port_data.port_index, specific_cell->port_data.cargoShipped, specific_cell->port_data.cargoArrived);
        }
      }
    }
  }
  if (myrank != 0)
  {
    MPI_Isend(&len, 1, MPI_INT, 0, myrank, MPI_COMM_WORLD, &request1);
    MPI_Isend(statistics, len, MPI_INT, 0, myrank, MPI_COMM_WORLD, &request2);
  }
  else
  {
    for (int i = 1; i < size; i++)
    {
      MPI_Recv(&len, 1, MPI_INT, i, i, MPI_COMM_WORLD, &status);
      if (len > 0)
      {
        int *receiver = (int *)malloc(sizeof(int) * len);
        MPI_Recv(&receiver[0], len, MPI_INT, i, i, MPI_COMM_WORLD, &status);
        for (int m = 0; m < len; m += 3)
        {
          printf("Port %d shipped %d tonnes and %d arrived\n", receiver[m], receiver[m + 1], receiver[m + 2]);
        }
      }
    }
  }
}

// Initialises the grid data structure based on the simulation configuration that has been read in
static void initialiseDomain(struct simulation_configuration_struct *simulation_configuration)
{

  for (int j = 1; j <= local_nx; j++)
  {
    for (int k = 1; k <= ny; k++)
    {
      sub_domain[(j * (ny + 2)) + k].x = j;
      sub_domain[(j * (ny + 2)) + k].y = k;
      for (int z = 0; z < MAX_SHIPS_PER_CELL; z++)
      {
        sub_domain[(j * (ny + 2)) + k].ships_data[z] = NULL;
      }
      // Now we set the type of grid cell based on the configuration
      if (isCellAPort(simulation_configuration, basex + j - 1, k - 1))
      {
        sub_domain[(j * (ny + 2)) + k].isPort = true;
        sub_domain[(j * (ny + 2)) + k].isIsland = false;
        sub_domain[(j * (ny + 2)) + k].isWater = false;
        initialisePort(simulation_configuration, &sub_domain[(j * (ny + 2)) + k], basex + j - 1, k - 1);
      }
      else if (isCellAnIsland(simulation_configuration, basex + j - 1, k - 1))
      {
        sub_domain[(j * (ny + 2)) + k].isPort = false;
        sub_domain[(j * (ny + 2)) + k].isIsland = true;
        sub_domain[(j * (ny + 2)) + k].isWater = false;
        sub_domain[(j * (ny + 2)) + k].number_ships = 0;
      }
      else
      {
        sub_domain[(j * (ny + 2)) + k].isPort = false;
        sub_domain[(j * (ny + 2)) + k].isIsland = false;
        sub_domain[(j * (ny + 2)) + k].isWater = true;
        sub_domain[(j * (ny + 2)) + k].number_ships = 0;
      }
    }
  }
}

// Initialises a single port in the domain based on the simulation configuration, the specific cell configuration, the X and Y coordinates
static void initialisePort(struct simulation_configuration_struct *simulation_configuration, struct cell_struct *specific_cell, int x_coord, int y_coord)
{
  specific_cell->port_data.port_index = getCellPortIndex(simulation_configuration, x_coord, y_coord);
  for (int i = 0; i < simulation_configuration->initialShips; i++)
  {
    struct ship_struct *newShip = (struct ship_struct *)malloc(sizeof(struct ship_struct));
    newShip->hoursAtSea = 0;
    newShip->cargoAmount = 0;
    newShip->id = currentShipId++;
    newShip->willMoveThisTimestep = true;
    int currentPortIndex = specific_cell->port_data.port_index;
    int targetPort = getTargetPort(simulation_configuration->number_ports, currentPortIndex);
    newShip->route = simulation_configuration->ports[currentPortIndex].target_route_indexes[targetPort];
    specific_cell->ships_data[i] = newShip;
  }
  specific_cell->number_ships = simulation_configuration->initialShips;
  specific_cell->port_data.cargoArrived = 0;
  specific_cell->port_data.cargoShipped = 0;
}

// Reports general statistics about the state of the simulation, called periodically during the simulation run
static void reportGeneralStatistics(struct simulation_configuration_struct *simulation_configuration, int time)
{
  int shipsAtSea = 0, shipsInPort = 0, cargoInTransit = 0;
  int globalShipsAtSea, globalShipsInport, globalCargoTransit;
  for (int j = 1; j <= local_nx; j++)
  {
    for (int k = 1; k <= ny; k++)
    {
      struct cell_struct *specific_cell = &sub_domain[(j * (ny + 2)) + k];
      if (specific_cell->isPort)
        shipsInPort += specific_cell->number_ships;
      if (specific_cell->isWater)
      {
        shipsAtSea += specific_cell->number_ships;
        for (int z = 0; z < MAX_SHIPS_PER_CELL; z++)
        {
          if (specific_cell->ships_data[z] != NULL)
            cargoInTransit += specific_cell->ships_data[z]->cargoAmount;
        }
      }
    }
  }
  MPI_Allreduce(&shipsAtSea, &globalShipsAtSea, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&shipsInPort, &globalShipsInport, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&cargoInTransit, &globalCargoTransit, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  if (myrank == 0)
  {
    printf("======= Report at %d hours =======\n", time);
    printf("%d ships at sea, %d ships in port, %d tonnes in transit\n", globalShipsAtSea, globalShipsInport, globalCargoTransit);
  }
}

// Updates the properties of the domain cells for a specific timestep, following the logic defined by the shipping company
static void updateProperties(struct simulation_configuration_struct *simulation_configuration)
{
  for (int j = 1; j <= local_nx; j++)
  {
    for (int k = 1; k <= ny; k++)
    {
      struct cell_struct *specific_cell = &sub_domain[(j * (ny + 2)) + k];
      if (specific_cell->isPort)
      {
        // If this is a port then perform port specific updates
        processPort(simulation_configuration, specific_cell);
      }
      else if (specific_cell->isWater)
      {
        // If this is water then perform water specific updates
        processWater(specific_cell, simulation_configuration->dt);
      }
    }
  }
}

// Will update the moment of ships from a specific cell to their next one respectively
static void updateMovement(struct simulation_configuration_struct *simulation_configuration, void (*get_next_cell_strategy)(int, int, int, int *, int *), int (*find_fresh_index_strategy)(struct cell_struct *))
{
  // Define the sending buffers, lengths of them and the positions of y
  int len1 = 0;
  struct ship_struct *sendShips1 = NULL;
  int len2 = 0;
  struct ship_struct *sendShips2 = NULL;
  int *ys1 = NULL;
  int *ys2 = NULL;

  for (int j = 1; j <= local_nx; j++)
  {
    for (int k = 1; k <= ny; k++)
    {
      struct cell_struct *specific_cell = &sub_domain[(j * (ny + 2)) + k];
      // Loop through all the possible ships in this cell
      for (int z = 0; z < MAX_SHIPS_PER_CELL; z++)
      {
        if (specific_cell->ships_data[z] != NULL && specific_cell->ships_data[z]->willMoveThisTimestep)
        {
          int newX, newY;
          // Asks the route planner for the next cell to move to based on the route this ship is following and the
          // current X and Y location of the ship. This is returned via the newX and newY pointers
          get_next_cell_strategy(specific_cell->ships_data[z]->route, basex + specific_cell->x - 1, specific_cell->y - 1, &newX, &newY);

          specific_cell->ships_data[z]->willMoveThisTimestep = false;

          // If next cell is on the bottom boundary of sub_domain, save the ship in the first sending buffer
          if (j + newX == local_nx + 1)
          {
            len1++;
            sendShips1 = (struct ship_struct *)realloc(sendShips1, sizeof(struct ship_struct) * len1);
            ys1 = (int *)realloc(ys1, sizeof(int) * len1);

            sendShips1[len1 - 1].id = specific_cell->ships_data[z]->id;
            sendShips1[len1 - 1].hoursAtSea = specific_cell->ships_data[z]->hoursAtSea;
            sendShips1[len1 - 1].cargoAmount = specific_cell->ships_data[z]->cargoAmount;
            sendShips1[len1 - 1].route = specific_cell->ships_data[z]->route;
            sendShips1[len1 - 1].willMoveThisTimestep = specific_cell->ships_data[z]->willMoveThisTimestep;

            ys1[len1 - 1] = k + newY;

            specific_cell->ships_data[z] = NULL;
            specific_cell->number_ships--;
          }
          else if (j + newX == 0) // If next cell is on the top boundary of sub_domain, save the ship in the second sending buffer
          {
            len2++;
            sendShips2 = (struct ship_struct *)realloc(sendShips2, sizeof(struct ship_struct) * len2);
            ys2 = (int *)realloc(ys2, sizeof(int) * len2);

            sendShips2[len2 - 1].id = specific_cell->ships_data[z]->id;
            sendShips2[len2 - 1].hoursAtSea = specific_cell->ships_data[z]->hoursAtSea;
            sendShips2[len2 - 1].cargoAmount = specific_cell->ships_data[z]->cargoAmount;
            sendShips2[len2 - 1].route = specific_cell->ships_data[z]->route;
            sendShips2[len2 - 1].willMoveThisTimestep = specific_cell->ships_data[z]->willMoveThisTimestep;

            ys2[len2 - 1] = k + newY;

            specific_cell->ships_data[z] = NULL;
            specific_cell->number_ships--;
          }
          else // Otherwise update it in its own area
          {
            int newIndex = find_fresh_index_strategy(&sub_domain[((j + newX) * (ny + 2)) + k + newY]);
            if (newIndex > -1)
            {
              sub_domain[((j + newX) * (ny + 2)) + k + newY].ships_data[newIndex] = specific_cell->ships_data[z];
              specific_cell->ships_data[z] = NULL;
              specific_cell->number_ships--;
              sub_domain[((j + newX) * (ny + 2)) + k + newY].number_ships++;
            }
          }
        }
      }
    }
  }

  MPI_Request requests[] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL};

  // If the first sending buffer is not empty, send it to the next neighboring process
  if (len1 > 0)
  {
    if (myrank < size - 1)
    {
      MPI_Isend(&len1, 1, MPI_INT, myrank + 1, myrank, MPI_COMM_WORLD, &requests[0]);

      MPI_Isend(&sendShips1[0], len1, shiptype, myrank + 1, myrank, MPI_COMM_WORLD, &requests[1]);

      MPI_Isend(&ys1[0], len1, MPI_INT, myrank + 1, myrank, MPI_COMM_WORLD, &requests[2]);
    }
  }
  else if (len1 == 0) // Otherwise send the length 0 to the next neighboring process
  {
    if (myrank < size - 1)
    {
      MPI_Isend(&len1, 1, MPI_INT, myrank + 1, myrank, MPI_COMM_WORLD, &requests[0]);
    }
  }

  // If the second sending buffer is not empty, send it to the previous neighboring process
  if (len2 > 0)
  {
    if (myrank > 0)
    {
      MPI_Isend(&len2, 1, MPI_INT, myrank - 1, myrank, MPI_COMM_WORLD, &requests[3]);

      MPI_Isend(&sendShips2[0], len2, shiptype, myrank - 1, myrank, MPI_COMM_WORLD, &requests[4]);

      MPI_Isend(&ys2[0], len2, MPI_INT, myrank - 1, myrank, MPI_COMM_WORLD, &requests[5]);
    }
  }
  else if (len2 == 0) // Otherwise send the length 0 to the previous neighboring process
  {
    if (myrank > 0)
    {
      MPI_Isend(&len2, 1, MPI_INT, myrank - 1, myrank, MPI_COMM_WORLD, &requests[3]);
    }
  }

  // Define the receiving buffers
  struct ship_struct *receiveShips1 = NULL;
  struct ship_struct *receiveShips2 = NULL;
  int *receiveys1 = NULL;
  int *receiveys2 = NULL;
  int cell_amount = 0;
  MPI_Status status;

  // Cells in the boundary receive messages
  if (myrank < size - 1)
  {
    MPI_Recv(&cell_amount, 1, MPI_INT, myrank + 1, myrank + 1, MPI_COMM_WORLD, &status);

    // If the amount of cells is above 0, receive them and update them to the sub_domain
    if (cell_amount > 0)
    {
      receiveShips1 = (struct ship_struct *)malloc(sizeof(struct ship_struct) * cell_amount * 100000);
      receiveys1 = (int *)realloc(receiveys1, sizeof(int) * cell_amount * 10000);

      MPI_Recv(&receiveShips1[0], cell_amount, shiptype, myrank + 1, myrank + 1, MPI_COMM_WORLD, &status);

      MPI_Recv(&receiveys1[0], cell_amount, MPI_INT, myrank + 1, myrank + 1, MPI_COMM_WORLD, &status);

      for (int j = 0; j < cell_amount; j++)
      {
        int y = receiveys1[j];

        int newIndex = find_fresh_index_strategy(&sub_domain[local_nx * (ny + 2) + y]);
        if (newIndex > -1)
        {
          sub_domain[local_nx * (ny + 2) + y].ships_data[newIndex] = &receiveShips1[j];

          sub_domain[local_nx * (ny + 2) + y].number_ships++;
        }
      }
    }
  }

  if (myrank > 0)
  {
    MPI_Recv(&cell_amount, 1, MPI_INT, myrank - 1, myrank - 1, MPI_COMM_WORLD, &status);

    // If the amount of cells is above 0, receive them and update them to the sub_domain
    if (cell_amount > 0)
    {
      receiveShips2 = (struct ship_struct *)malloc(sizeof(struct ship_struct) * cell_amount * 100000);
      receiveys2 = (int *)realloc(receiveys2, sizeof(int) * cell_amount * 10000);

      MPI_Recv(&receiveShips2[0], cell_amount, shiptype, myrank - 1, myrank - 1, MPI_COMM_WORLD, &status);

      MPI_Recv(&receiveys2[0], cell_amount, MPI_INT, myrank - 1, myrank - 1, MPI_COMM_WORLD, &status);

      for (int j = 0; j < cell_amount; j++)
      {
        int y = receiveys2[j];

        int newIndex = find_fresh_index_strategy(&sub_domain[ny + 2 + y]);
        if (newIndex > -1)
        {

          sub_domain[ny + 2 + y].ships_data[newIndex] = &receiveShips2[j];
          sub_domain[ny + 2 + y].number_ships++;
        }
      }
    }
  }

  MPI_Waitall(6, requests, MPI_STATUSES_IGNORE);

  if (sendShips1 != NULL)
    free(sendShips1);
  if (sendShips2 != NULL)
    free(sendShips2);
  if (ys1 != NULL)
    free(ys1);
  if (ys2 != NULL)
    free(ys2);
}

// Port specific processing for a timestep, given the simulation configuration and the specific cell data structure that represents this port
// this function will perform the necessary updates as per the behaviour defined by the shipping company.
static void processPort(struct simulation_configuration_struct *simulation_configuration, struct cell_struct *specific_cell)
{
  int totalShips = 0;
  for (int i = 0; i < 9; i++)
  {
    // This assumes that we have DT of 10, hence working back the past 10 timesteps. This is an OK assumption to make if you
    // want to keep it simple
    specific_cell->port_data.shipsInPastHundredHours[i] = specific_cell->port_data.shipsInPastHundredHours[i + 1];
    totalShips += specific_cell->port_data.shipsInPastHundredHours[i];
  }
  specific_cell->port_data.shipsInPastHundredHours[9] = specific_cell->number_ships;
  totalShips += specific_cell->number_ships;
  // Having calculated the total number of ships in the past hundred hours, let's see if we need to create a new one
  if (shouldCreateNewShip(totalShips))
  {
    // Create a new ship and initialise values
    struct ship_struct *newShip = (struct ship_struct *)malloc(sizeof(struct ship_struct));
    newShip->hoursAtSea = 0;
    newShip->cargoAmount = 0;
    newShip->id = currentShipId++;
    // Finds a free index in the ports data structure to store this new ship
    int nextIndex = findFreeShipIndex(specific_cell);
    if (nextIndex > -1)
    {
      specific_cell->ships_data[nextIndex] = newShip;
      specific_cell->number_ships++;
    }
  }
  // Now loop through each possible ship in port and handle it
  for (int z = 0; z < MAX_SHIPS_PER_CELL; z++)
  {
    if (specific_cell->ships_data[z] != NULL)
    {
      // Update arrived cargo in port
      specific_cell->port_data.cargoArrived += specific_cell->ships_data[z]->cargoAmount;
      if (specific_cell->number_ships > 1 && shouldRemoveShip(specific_cell->ships_data[z]->hoursAtSea))
      {
        // If we have more than one ship in port and we should remove this one then eliminate it
        specific_cell->ships_data[z] = NULL;
        specific_cell->number_ships--;
      }
      else
      {
        // Figure out where ship should move to (the target port) and assign cargo to it. Note that the cargo assignment is very simple as
        // a specific port will load up the same amount of cargo for each ship (and the specific amount for each port is defined in the
        // configuration file)
        specific_cell->ships_data[z]->willMoveThisTimestep = true;
        int currentPortIndex = specific_cell->port_data.port_index;
        int targetPort = getTargetPort(simulation_configuration->number_ports, currentPortIndex);
        specific_cell->ships_data[z]->route = simulation_configuration->ports[currentPortIndex].target_route_indexes[targetPort];
        specific_cell->ships_data[z]->cargoAmount = simulation_configuration->ports[currentPortIndex].cargo;
        specific_cell->port_data.cargoShipped += specific_cell->ships_data[z]->cargoAmount;
      }
    }
  }
}

// Process a grid cell per timestep if it is water. As well as the specific cell, also pass in dt which is the number
// of hours that each timestep represents
static void processWater(struct cell_struct *specific_cell, int dt)
{
  // Loop through each possible ship in the water cell and update its properties
  for (int z = 0; z < MAX_SHIPS_PER_CELL; z++)
  {
    if (specific_cell->ships_data[z] != NULL)
    {
      if (willShipMove(specific_cell->number_ships))
      {
        specific_cell->ships_data[z]->willMoveThisTimestep = true;
      }
      specific_cell->ships_data[z]->hoursAtSea += dt;
    }
  }
}

// Given the data structure that stores a specific cell, this will identify the index of the first free location that
// a ship can be stored in, both port and water cells need to store ships from one timestep to the next
static int findFreeShipIndex(struct cell_struct *specific_cell)
{
  for (int z = 0; z < MAX_SHIPS_PER_CELL; z++)
  {
    if (specific_cell->ships_data[z] == NULL)
      return z;
  }
  return -1;
}
