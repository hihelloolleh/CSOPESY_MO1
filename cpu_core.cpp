#include "cpu_core.h"
#include "shared_globals.h"
#include "instructions.h"
#include "scheduler_utils.h"

#include <iostream>
#include <thread>
#include <chrono>

void cpu_core_worker(int core_id) {
    while (system_running) {
        Process* process = nullptr;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (ready_queue.empty()) {
                lock.unlock();
                if (core_id == 0) {
                    cpu_ticks++;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                continue;
            }

            process = select_process(); 
        }

        if (!process) continue;

        process->assigned_core = core_id;
        if (process->start_time.empty()) {
            process->start_time = get_timestamp();
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (core_id < core_busy.size()) core_busy[core_id] = true;
        }

        int quantum = global_config.quantum_cycles;
        bool preempt = should_preempt();
        bool use_quantum = uses_quantum();

        int exec_count = 0;
        while (process->program_counter < process->instructions.size() && system_running) {
            execute_instruction(process);

            for (int i = 0; i < global_config.delay_per_exec; ++i)
                cpu_ticks++;
            cpu_ticks++;

            process->program_counter++;
            exec_count++;

            if (should_yield(exec_count, preempt, use_quantum)) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                ready_queue.push(process);
                break;
            }
        }

        if (process->program_counter >= process->instructions.size()) {
            process->end_time = get_timestamp();
            process->finished = true;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (core_id < core_busy.size()) {
                core_busy[core_id] = false;
            }
        }
    }
}
