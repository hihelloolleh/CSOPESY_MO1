#include "cpu_core.h"
#include "shared_globals.h"
#include "instructions.h"
#include "scheduler_utils.h"
#include "mem_manager.h" 

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
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            process = select_process();
        }
        
        if (!process) continue;

        if (process->state == ProcessState::FINISHED || process->state == ProcessState::CRASHED) {
            continue;
        }

        if (global_mem_manager->getProcess(process->id) == nullptr) {
            std::cerr << "Error: Process " << process->id << " started on core but not in Memory Manager.\n";
            continue;
        }

        process->assigned_core = core_id;
        process->last_core = core_id;
        if (process->start_time.empty()) {
            process->start_time = get_timestamp();
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (core_id < core_busy.size()) core_busy[core_id] = true;
        }

        // --- THIS LINE IS REMOVED TO FIX THE WARNING ---
        // int quantum = global_config.quantum_cycles; 
        
        bool preempt = should_preempt();
        bool use_quantum = uses_quantum();

        int exec_count = 0;
        while (process->state != ProcessState::FINISHED && system_running) {
            exec_count++;

            bool should_yield_now = should_yield(process, exec_count, preempt, use_quantum);
            if (should_yield_now) {
                if (global_mem_manager) {
                    global_quantum_cycle++;
                    global_mem_manager->snapshotMemory(global_quantum_cycle);
                }
                
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    ready_queue.push(process);
                }
                process->assigned_core = -1;
                break;
            }

            if (process->state == ProcessState::WAITING) {
                if (cpu_ticks < process->sleep_until_tick) {
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        ready_queue.push(process);
                    }
                    process->assigned_core = -1;
                    break;
                }
                else {
                    process->state = ProcessState::RUNNING;
                }
            }

            execute_instruction(process);
            if (process->state == ProcessState::CRASHED) {
                break;
            }

            if (process->program_counter >= process->instructions.size() && process->for_stack.empty()) {
                process->state = ProcessState::FINISHED;
                continue;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(global_config.delay_per_exec));
            cpu_ticks++;
        }

        if (process->state == ProcessState::FINISHED || process->state == ProcessState::CRASHED) {
            process->end_time = get_timestamp();
            process->finished = true;
            process->assigned_core = -1;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (core_id < core_busy.size()) {
                core_busy[core_id] = false;
            }
        }
    }
}