#include "cpu_core.h"
#include "shared_globals.h"
#include "instructions.h" 
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
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                continue;
            }
            process = ready_queue.front();
            ready_queue.pop();
        }

        if (process) {
            process->assigned_core = core_id;
            process->start_time = get_timestamp();

                // INTERPRETER LOOP
            while (process->program_counter < process->instructions.size() && system_running) {
                
                execute_instruction(process);

                // Apply the "busy-wait" delay
                for (int i = 0; i < global_config.delay_per_exec; ++i) {
                    cpu_ticks++;
                }
                
                cpu_ticks++;

                process->program_counter++;
            }
            
            if (process->program_counter >= process->instructions.size()) {
                process->end_time = get_timestamp();
                process->finished = true;
            }
        }
    }
}