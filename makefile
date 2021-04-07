SRC = src/simulation_configuration.c src/main.c src/route_map.c src/simulation_support.c
LFLAGS=-lm
CFLAGS=-O3
CC=mpicc

all: 
	$(CC) -o ships $(SRC) $(CFLAGS) $(LFLAGS)

