#ifndef ROUTEMAP_INCLUDE
#define ROUTEMAP_INCLUDE

#include "simulation_configuration.h"

void initialise_routemap(struct simulation_configuration_struct *, int, int, int, int);
void calculate_routes(struct simulation_configuration_struct *);
int generate_route(int, int, int, int);
void getNextCell(int, int, int, int *, int *);

#endif