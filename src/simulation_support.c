#include <time.h>
#include <stdlib.h>
#include "simulation_support.h"

// Initialises the simulation support by seeding the random number generator. Note if you do not do this
// then it will mean you random numbers are predictably chosen (i.e. the same) each run
void initialiseSimulationSupport()
{
  srand(time(NULL));
}

// Based on the number of ships in the past hundred hours, this will determine whether a new ship
// should be created or not
bool shouldCreateNewShip(int shipsInPastHundredHours)
{
  if (shipsInPastHundredHours < 10)
    return false;
  return rand() % 30 < shipsInPastHundredHours;
}

// Given the hours at sea that a ship has endured, this will return whether that ship should
// be removed or not
bool shouldRemoveShip(int hoursAtSea)
{
  if (hoursAtSea < 100)
    return false;
  return (rand() % 6 == 0);
}

// Given the number of hours at sea that a ship has endured and the number of ships in the current cell,
// this will determine whether that ship should move in this timestep or not.
bool willShipMove(int numberShipsInCell)
{
  if (numberShipsInCell < 4)
    return true;
  if (numberShipsInCell > rand() % 20 && rand() % 2 == 0)
    return false;
  return true;
}

// Generates a target point index for a ship based on the total number of ports and the current
// port that it resides in (note that this will never be the current port, it is guaranteed to be moving
// to a different port)
int getTargetPort(int numberPorts, int currentPort)
{
  int r = rand() % numberPorts;
  while (r == currentPort)
  {
    r = rand() % numberPorts;
  }
  return r;
}