#include "cpu_core.h"
#include "shared_globals.h"
#include "instructions.h" 
#include <iostream>
#include <thread>
#include <chrono>

void cpu_core_worker(int core_id) {
    while (system_running) {
        Process* process = nullptr;

        // This block for getting a process (or handling idle state) is correct.
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

            // ===================================================================
            // THIS IS THE CORRECTED INTERPRETER LOOP
            // ===================================================================
            while (process->program_counter < process->instructions.size() && system_running) {
                
                // 1. Execute the instruction at the current program counter
                execute_instruction(process);

                // 2. Apply the "busy-wait" delay
                for (int i = 0; i < global_config.delay_per_exec; ++i) {
                    cpu_ticks++;
                }
                
                // 3. The execution itself costs one tick
                cpu_ticks++;

                // 4. *** THIS IS THE FIX: Advance to the next instruction ***
                process->program_counter++;
            }
            
            if (process->program_counter >= process->instructions.size()) {
                process->end_time = get_timestamp();
                process->finished = true;
            }
        }
    }
}