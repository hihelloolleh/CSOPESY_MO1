#ifndef SCHEDULER_UTILS_H
#define SCHEDULER_UTILS_H

#include "shared_globals.h"


Process* select_process();
bool should_preempt();
bool uses_quantum();
bool should_yield(Process* process,int executed, bool preempt, bool quantum);


#endif
