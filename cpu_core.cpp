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
                
                // ==========================================================
                // === PHASE 3: INSTRUCTION PAGE FAULT HANDLING =============
                // ==========================================================

                // 1. Calculate the virtual address of the instruction we are ABOUT to execute.
                const size_t AVG_INSTRUCTION_SIZE = 8; // This must match the value in scheduler.cpp
                uint16_t instruction_address = process->program_counter * AVG_INSTRUCTION_SIZE;
                
                // 2. "Touch" the memory page for this instruction.
                //    The touchPage method will page it in if it's not resident
                //    and will return true if a page fault just occurred.
                if (global_mem_manager->touchPage(process->id, instruction_address)) {
                    // A page fault for an instruction happened! The process must wait.
                    /*
                    std::cout << "[CPU Core " << core_id << "] P" << process->id 
                              << ": Instruction page fault at address " << instruction_address 
                              << ". Yielding CPU." << std::endl;
                    */
                    process->state = ProcessState::WAITING; // Mark as waiting for I/O
                    break; // Break out of the execution loop to yield the CPU core.
                }
                
                // ==========================================================
                
                // 3. If we get here, the instruction's page is guaranteed to be in memory.
                //    Now we can execute it. Any DATA page faults will be handled inside here.
                execute_instruction(process);
                
                instructions_executed_in_quantum++;
                std::this_thread::sleep_for(std::chrono::milliseconds(global_config.delay_per_exec));

                if (process->state != ProcessState::RUNNING) {
                    // If the process went to WAITING (for SLEEP) or CRASHED, break.
                    break;
                }

                if (should_yield(process, instructions_executed_in_quantum, should_preempt(), uses_quantum())) {
                    break;
                }
            }

            core_busy[core_id] = false;
            process->last_core = core_id;

            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                bool work_was_added = false; // A flag to see if we need to notify

                // The process's turn is over, figure out where it goes next.
                if (process->state == ProcessState::RUNNING) {
                    // It was still running (quantum expired or finished).
                    if (process->program_counter >= process->instructions.size()) {
                        process->state = ProcessState::FINISHED;
                        process->finished = true;
                        process->end_time = get_timestamp();
                        global_mem_manager->removeProcess(process->id);
                    } else {
                        // Quantum expired, put it back on the ready queue.
                        process->state = ProcessState::READY;
                        ready_queue.push(process);
                    }
                } else if (process->state == ProcessState::WAITING) {
                    // It was waiting for a page fault to resolve or for SLEEP.
                    // In either case, it's ready for another turn.
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