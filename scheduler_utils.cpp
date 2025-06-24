#include "scheduler_utils.h"
#include "shared_globals.h"
#include <algorithm>
#include <vector>
#include <queue>

Process* select_process() {
    if (ready_queue.empty()) return nullptr;

    std::vector<Process*> temp;
    while (!ready_queue.empty()) {
        temp.push_back(ready_queue.front());
        ready_queue.pop();
    }

    Process* selected = nullptr;

    switch (global_config.scheduler_type) {
    case SchedulerType::FCFS:
    case SchedulerType::RR:
        selected = temp.front();
        break;

    case SchedulerType::SJF:
        selected = *std::min_element(temp.begin(), temp.end(),
            [](Process* a, Process* b) {
                return (a->instructions.size() == b->instructions.size()) ? a->id < b->id
                    : a->instructions.size() < b->instructions.size();
            });
        break;

    case SchedulerType::SRTF:
        selected = *std::min_element(temp.begin(), temp.end(),
            [](Process* a, Process* b) {
                int a_remain = static_cast<int>(a->instructions.size()) - a->program_counter;
                int b_remain = static_cast<int>(b->instructions.size()) - b->program_counter;
                return (a_remain == b_remain) ? a->id < b->id : a_remain < b_remain;
            });
        break;

    case SchedulerType::PRIORITY_NONPREEMPTIVE:
    case SchedulerType::PRIORITY_PREEMPTIVE:
        selected = *std::min_element(temp.begin(), temp.end(),
            [](Process* a, Process* b) {
                return (a->priority == b->priority) ? a->id < b->id : a->priority < b->priority;
            });
        break;

    default:
        selected = temp.front();
        break;
    }

    for (Process* p : temp) {
        if (p != selected) ready_queue.push(p);
    }

    return selected;
}

bool should_preempt() {
    return global_config.scheduler_type == SchedulerType::SRTF ||
        global_config.scheduler_type == SchedulerType::PRIORITY_PREEMPTIVE;
}

bool uses_quantum() {
    return global_config.scheduler_type == SchedulerType::RR;
}

bool should_yield(Process* current, int executed, bool preempt, bool quantum) {
    if (quantum && executed >= global_config.quantum_cycles) {
        return true;
    }

    if (preempt && !ready_queue.empty()) {
        Process* next = nullptr;

        {
            std::lock_guard<std::mutex> lock(queue_mutex);

            std::vector<Process*> temp;
            while (!ready_queue.empty()) {
                temp.push_back(ready_queue.front());
                ready_queue.pop();
            }

            if (!temp.empty()) {
                next = *std::min_element(temp.begin(), temp.end(),
                    [](Process* a, Process* b) {
                        return (a->priority == b->priority) ? a->id < b->id : a->priority < b->priority;
                    });

                for (Process* p : temp) ready_queue.push(p);
            }
        }

        if (next && next->priority < current->priority) {
            return true;
        }
    }

    return false;
}
