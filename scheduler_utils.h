#ifndef SCHEDULER_UTILS_H
#define SCHEDULER_UTILS_H

#include "process.h"

Process* select_process();
bool should_preempt();
bool uses_quantum();
bool should_yield(int executed, bool preempt, bool quantum);

#endif
