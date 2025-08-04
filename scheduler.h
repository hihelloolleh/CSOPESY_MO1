#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "process.h"

// The main loop for the thread that increments the global CPU tick counter.
void clock_thread();

std::string generate_unique_process_name(const std::string& base_name);

// Creates a new random process
Process* create_random_process(const std::string& name, size_t memory_size);

// The main loop for the thread that generates processes.
void process_generator_thread();

#endif // SCHEDULER_H