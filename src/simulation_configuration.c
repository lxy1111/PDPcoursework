#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "simulation_configuration.h"

#define MAX_LINE_LENGTH 128

static int getEntityNumber(char *);
static bool getValueFromConfigurationString(char *, int *);

/*
* A simple configuration file reader, I don't think you will need to change this (but feel free if you want to!)
* It will parse the configuration file and set the appropriate configuration points that will then feed into the simulation
* setup. It is somewhat limited in its flexibility and you need to be somewhat careful about the configuration file format, 
* but is fine for our purposes
*/
void parseConfiguration(char *filename, struct simulation_configuration_struct *simulation_configuration)
{
  FILE *f = fopen(filename, "r");
  char buffer[MAX_LINE_LENGTH], entity_copy[MAX_LINE_LENGTH];
  int value;
  while ((fgets(buffer, MAX_LINE_LENGTH, f)) != NULL)
  {
    // If the string ends with a newline then remove this to make parsing simpler
    if (isspace(buffer[strlen(buffer) - 1]))
      buffer[strlen(buffer) - 1] = '\0';
    if (strlen(buffer) > 0)
    {
      if (buffer[0] == '#')
        continue; // This line is a comment so ignore
      if (getValueFromConfigurationString(buffer, &value))
      {
        if (strstr(buffer, "SIZE_X") != NULL)
          simulation_configuration->size_x = value;
        if (strstr(buffer, "SIZE_Y") != NULL)
          simulation_configuration->size_y = value;
        if (strstr(buffer, "INITIAL_SHIPS") != NULL)
          simulation_configuration->initialShips = value;
        if (strstr(buffer, "REPORT_STATS_EVERY") != NULL)
          simulation_configuration->reportStatsEvery = value;
        if (strstr(buffer, "NUM_PORTS") != NULL)
        {
          simulation_configuration->number_ports = value;
          simulation_configuration->ports = (struct port_configuration_struct *)malloc(sizeof(struct port_configuration_struct) * value);
          for (int i = 0; i < value; i++)
          {
            simulation_configuration->ports[i].target_route_indexes = (int *)malloc(sizeof(int) * value);
          }
        }
        if (strstr(buffer, "NUM_ISLANDS") != NULL)
        {
          simulation_configuration->number_islands = value;
          simulation_configuration->islands = (struct island_configuration_struct *)malloc(sizeof(struct island_configuration_struct) * value);
        }
        if (strstr(buffer, "NUM_TIMESTEPS") != NULL)
          simulation_configuration->number_timesteps = value;
        if (strstr(buffer, "DT") != NULL)
          simulation_configuration->dt = value;
        if (strstr(buffer, "PORT_") != NULL)
        {
          strcpy(entity_copy, buffer);
          int portNumber = getEntityNumber(entity_copy);
          if (portNumber >= 0)
          {
            if (strstr(buffer, "_X") != NULL)
              simulation_configuration->ports[portNumber].x = value;
            if (strstr(buffer, "_Y") != NULL)
              simulation_configuration->ports[portNumber].y = value;
            if (strstr(buffer, "_CARGO") != NULL)
              simulation_configuration->ports[portNumber].cargo = value;
          }
          else
          {
            fprintf(stderr, "Ignoring port configuration line '%s' as this is malformed and can not extract port number\n", buffer);
          }
        }
        if (strstr(buffer, "ISLAND_") != NULL)
        {
          strcpy(entity_copy, buffer);
          int islandNumber = getEntityNumber(entity_copy);
          if (islandNumber >= 0)
          {
            if (strstr(buffer, "_X") != NULL)
              simulation_configuration->islands[islandNumber].x = value;
            if (strstr(buffer, "_Y") != NULL)
              simulation_configuration->islands[islandNumber].y = value;
          }
          else
          {
            fprintf(stderr, "Ignoring port configuration line '%s' as this is malformed and can not extract island number\n", buffer);
          }
        }
      }
      else
      {
        fprintf(stderr, "Ignoring configuration line '%s' as this is malformed\n", buffer);
      }
    }
  }
  fclose(f);
}

// Given the simulation configuration and a cell's X and Y location this will determine whether a port occupies that
// cell or not
bool isCellAPort(struct simulation_configuration_struct *config, int x, int y)
{
  for (int i = 0; i < config->number_ports; i++)
  {
    if (config->ports[i].x == x && config->ports[i].y == y)
      return true;
  }
  return false;
}

// Given the simulation configuration and a cell's X and Y location this will return the index of the port that
// lies at that location, or altertively -1 if there is no port there.
int getCellPortIndex(struct simulation_configuration_struct *config, int x, int y)
{
  for (int i = 0; i < config->number_ports; i++)
  {
    if (config->ports[i].x == x && config->ports[i].y == y)
      return i;
  }
  return -1;
}

// Given the simulation configuration and a cell's X and Y location this will determine whether an island occupies that
// cell or not
bool isCellAnIsland(struct simulation_configuration_struct *config, int x, int y)
{
  for (int i = 0; i < config->number_islands; i++)
  {
    if (config->islands[i].x == x && config->islands[i].y == y)
      return true;
  }
  return false;
}

// A helper function to parse a string with an underscore in it, this will extract the number after the underscore
// as we use this in the configuration file for setting numbers of ports and islands in the configuration
static int getEntityNumber(char *sourceString)
{
  char *underScoreLocation = strchr(sourceString, '_');
  if (underScoreLocation != NULL)
  {
    char *secondUnderScoreLocation = strchr(underScoreLocation + 1, '_');
    if (secondUnderScoreLocation != NULL)
    {
      secondUnderScoreLocation = '\0';
      return atoi(&underScoreLocation[1]);
    }
  }
  return -1;
}

// Given a string with a key-value pair (e.g. key = value) this will extract the value after the equals (it is assumed)
// to be an integer and return this via the value pointer. It returns true if such extraction was possible and
// false if not
static bool getValueFromConfigurationString(char *sourceString, int *value)
{
  char *equalsLocation = strchr(sourceString, '=');
  if (equalsLocation == NULL)
  {
    return false;
  }
  else
  {
    *value = atoi(&equalsLocation[1]);
    return true;
  }
}
