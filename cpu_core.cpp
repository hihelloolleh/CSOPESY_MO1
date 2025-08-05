#include "cpu_core.h"
#include "shared_globals.h"
#include "scheduler_utils.h"
#include "instructions.h"
#include "mem_manager.h"
#include <thread>
#include <chrono>
#include <iostream>

void cpu_core_worker(int core_id) {
    while (system_running) {
        Process* process = nullptr;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !ready_queue.empty() || !system_running; });

            if (!system_running) break;

            process = select_process();
            if (process) {
                process->assigned_core = core_id;
                process->state = ProcessState::RUNNING;
                if(process->start_time.empty()) process->start_time = get_timestamp();
                core_busy[core_id] = true;
            }
        }
        
        if (process) {
            int instructions_executed_in_quantum = 0;

            while (system_running && process->program_counter < process->instructions.size()) {

                if (global_config.delay_per_exec == 0) {
                    uint64_t current_tick = cpu_ticks.load();
                    while (system_running && cpu_ticks.load() <= current_tick) {                      
                        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
                    }
                }

                execute_instruction(process);

                if (global_config.delay_per_exec > 0) {
                    uint64_t start_tick = cpu_ticks.load();
                    uint64_t end_tick = start_tick + global_config.delay_per_exec;
                    while (system_running && cpu_ticks.load() < end_tick) {
                        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
                    }
                }

                instructions_executed_in_quantum++;
                std::this_thread::sleep_for(std::chrono::milliseconds(global_config.delay_per_exec));

                if (process->state != ProcessState::RUNNING) {
                    // The process state changed due to SLEEP, CRASH, or a data page fault.
                    // Break the loop to yield the CPU.
                    break;
                }

                // Check if the quantum has expired.
                if (should_yield(process, instructions_executed_in_quantum, should_preempt(), uses_quantum())) {
                    break;
                }
            }

            core_busy[core_id] = false;
            process->last_core = core_id;

            {
                std::lock_guard<std::mutex> lock(queue_mutex);

                // The process's turn is over, figure out where it goes next.
                if (process->state == ProcessState::RUNNING) {
                    // It was still running (quantum expired or finished).
                    if (process->program_counter >= process->instructions.size()) {
                        // --- The process has completed all its instructions. ---
                        process->state = ProcessState::FINISHED;
                        process->finished = true;
                        process->end_time = get_timestamp();
         
                        process->program_counter = process->instructions.size();

                        // comment out first, might be messing with memory allocation. 
                        global_mem_manager->removeProcess(process->id);

                    }
                    else {
                        // Quantum expired, but the process is not finished. Put it back on the ready queue.
                        process->state = ProcessState::READY;
                        ready_queue.push(process);
                    }
                }
                else if (process->state == ProcessState::WAITING) {
                    process->state = ProcessState::READY;
                    ready_queue.push(process);
                }
                else if (process->state == ProcessState::CRASHED) {
                    process->finished = true;
                    process->end_time = get_timestamp();
                    global_mem_manager->removeProcess(process->id);
                }
                queue_cv.notify_all();
            }
        }
    }
}