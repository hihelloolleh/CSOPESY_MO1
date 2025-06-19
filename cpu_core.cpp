#include "cpu_core.h"
#include "shared_globals.h"
#include <iostream>
#include <thread>

void cpu_core_worker(int core_id) {
    while (system_running) {
        Process* process = nullptr;

        // --- Wait for and get a process from the ready queue ---
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            // Wait until the queue is not empty OR the system is shutting down
            queue_cv.wait(lock, [] { return !ready_queue.empty() || !system_running; });

            // If the system is shutting down, exit the thread
            if (!system_running) {
                return;
            }

            process = ready_queue.front();
            ready_queue.pop();
        }

        // --- Execute the process ---
        if (process) {
            process->assigned_core = core_id;
            process->start_time = get_timestamp();

            std::cout << get_timestamp() << " Core " << core_id << ": Starting process " << process->name << std::endl;

            // !! THIS IS WHERE THE INTERPRETER LOGIC WILL GO !!
            // For now, we'll just simulate work and mark it as finished.
            
            // Simulating work based on instruction count
            int instruction_count = 5; // Replace with process->instructions.size() later
            for (int i = 0; i < instruction_count; ++i) {
                // Simulate instruction execution
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                 if (!system_running) break; // Allow for graceful shutdown
            }

            process->end_time = get_timestamp();
            process->finished = true;
            std::cout << get_timestamp() << " Core " << core_id << ": Finished process " << process->name << std::endl;
        }
    }
}