# MSc: Parallel design patterns final coursework

## Requirements

* ICC compiler
* MPI

### Cirrus

To get the ICC compiler and MPI on Cirrus, run:

```console
$ module load intel-compilers-19
$ module load mpt
```

---

## Program structure

source file: main.c route_map.c simulation_configuration.c simulation_support.c

header file: route_map.h simulation_configuration.h simulation_support.h 

Config file: config_1.txt config_2.txt

encapsulation of functionalities:

* route_map.h and route_map.c
void initialise_routemap(struct simulation_configuration_struct *, int, int, int, int);
void calculate_routes(struct simulation_configuration_struct *);
int generate_route(int, int, int, int);
void getNextCell(int, int, int, int *, int *);

* simulation_configuration.h and simulation_configuration.c
void parseConfiguration(char *, struct simulation_configuration_struct *);
bool isCellAPort(struct simulation_configuration_struct *, int, int);
int getCellPortIndex(struct simulation_configuration_struct *, int, int);
bool isCellAnIsland(struct simulation_configuration_struct *, int, int);

* simulation_support.h and simulation_support.c
void initialiseSimulationSupport();
bool shouldCreateNewShip(int);
bool shouldRemoveShip(int);
bool willShipMove(int);
int getTargetPort(int, int);

* main.c
static void finalise_simulation();
static void run_simulation(struct simulation_configuration_struct *, int, int, int, int, void (*)(int, int), void (*)(struct simulation_configuration_struct *), void (*)());
static void init_simulation(int, int);
static void initialiseDomain(struct simulation_configuration_struct *);
static void initialisePort(struct simulation_configuration_struct *, struct cell_struct *, int, int);
static void simulation(struct simulation_configuration_struct *);
static void reportFinalInformation(struct simulation_configuration_struct *);
static void updateProperties(struct simulation_configuration_struct *);
static void updateMovement(struct simulation_configuration_struct *);
static void processPort(struct simulation_configuration_struct *, struct cell_struct *);
static void processWater(struct cell_struct *, int);
static int findFreeShipIndex(struct cell_struct *);
static void reportStatistics(struct simulation_configuration_struct *, int);
static void reportGeneralStatistics(struct simulation_configuration_struct *, int);
static void perform_halo_swap(int, int, int, int, int);
static void initializeHalos();

---

## Compilation

Compiling command is included in the file makefile:

```console
$ make makefile or make
```

---

## Usage

To run the program:


Run the executable file:

```console
$ mpirun [-n NUM_PROCESS] ./ships [CONFIG_FILE]
```

For example, to run with 4 processes and config_1.txt:

```console
$ mpirun -n 4 ./ships config_1.txt 
```

The program will output information on its operation. For example:

```
The time of route planning is 8.8e-05
======= Report at 0 hours =======
21 ships at sea, 0 ships in port, 320 tonnes in transit
======= Report at 100 hours =======
23 ships at sea, 5 ships in port, 370 tonnes in transit
======= Report at 200 hours =======
30 ships at sea, 0 ships in port, 460 tonnes in transit
======= Report at 300 hours =======
31 ships at sea, 3 ships in port, 450 tonnes in transit
======= Report at 400 hours =======
38 ships at sea, 2 ships in port, 580 tonnes in transit
======= Report at 500 hours =======
44 ships at sea, 4 ships in port, 670 tonnes in transit
======= Report at 600 hours =======
52 ships at sea, 5 ships in port, 740 tonnes in transit
======= Report at 700 hours =======
63 ships at sea, 2 ships in port, 980 tonnes in transit
======= Report at 800 hours =======
71 ships at sea, 5 ships in port, 1080 tonnes in transit
======= Report at 900 hours =======
72 ships at sea, 6 ships in port, 1130 tonnes in transit
The time of simulation is 0.007345
======= Final report at 1000 hours =======
Port 0 shipped 4980 tonnes and 1930 arrived
Port 1 shipped 2320 tonnes and 4040 arrived
```

Other examples of running the program include:

```console
$ mpirun -n 8 ./ships config_1.txt
$ mpirun -n 16 ./ships config_2.txt
```


