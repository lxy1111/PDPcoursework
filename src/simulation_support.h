#ifndef SUPPORT_INCLUDE
#define SUPPORT_INCLUDE

#include <stdbool.h>

void initialiseSimulationSupport();
bool shouldCreateNewShip(int);
bool shouldRemoveShip(int);
bool willShipMove(int);
int getTargetPort(int, int);

#endif