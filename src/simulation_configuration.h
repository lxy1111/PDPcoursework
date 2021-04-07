#ifndef CONFIGURATION_INCLUDE
#define CONFIGURATION_INCLUDE

// Configuration of a port, it's X and Y location along with amount of cargo loaded into ships
// from this port and all the route indexes to target ports
struct port_configuration_struct
{
  int x, y, cargo;
  int *target_route_indexes;
};

// Configuration of an island, nice and simple as just store it's X and Y location
struct island_configuration_struct
{
  int x, y;
};

// Overall configuration of the simulation
struct simulation_configuration_struct
{
  // size_x = Size of global domain in X
  // size_y = Size of global domain in Y
  // number_ports = Total number ports in the global domain
  // number_islands = Total number islands in the global domain
  // number_timesteps = Total number of timesteps to run the simulation for
  // dt = Number of hours between each timestep, for instance if this is 10 then each timestep will advance the clock by 10 hours
  // initialShips = Number of initial ships
  // reportStatsEvery = Frequency (in timesteps) that statistics should be reported
  int size_x, size_y, number_ports, number_islands, number_timesteps, dt, initialShips, reportStatsEvery;
  struct port_configuration_struct *ports;
  struct island_configuration_struct *islands;
};

void parseConfiguration(char *, struct simulation_configuration_struct *);
bool isCellAPort(struct simulation_configuration_struct *, int, int);
int getCellPortIndex(struct simulation_configuration_struct *, int, int);
bool isCellAnIsland(struct simulation_configuration_struct *, int, int);

#endif