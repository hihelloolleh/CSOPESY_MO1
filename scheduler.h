#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

// The main loop for the thread that increments the global CPU tick counter.
void clock_thread();

// The main loop for the thread that generates processes.
void process_generator_thread();

// Creates a new random process
Process* create_random_process();

#endif // SCHEDULER_H