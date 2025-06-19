#ifndef SCHEDULER_H
#define SCHEDULER_H

// The main loop for the thread that increments the global CPU tick counter.
void clock_thread();

// The main loop for the thread that generates processes.
void process_generator_thread();

#endif // SCHEDULER_H