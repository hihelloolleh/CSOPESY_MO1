#include "scheduler.h"
#include "shared_globals.h"
#include "mem_manager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set>
#include <algorithm>

std::atomic<int> g_next_pid(1);

void clock_thread() {
    while (system_running) {
        cpu_ticks++;
        if (is_initialized && global_mem_manager && (cpu_ticks.load() % 100 == 0)) {
            global_mem_manager->snapshotMemory(cpu_ticks.load());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

Process* create_random_process(const std::string& name, size_t memory_size_override) {
    Process* p = new Process();
    p->id = g_next_pid++;
    p->name = name;
    p->priority = rand() % 100;

    if (memory_size_override > 0) {
        p->memory_required = memory_size_override;
    } else {
        p->memory_required = (rand() % (global_config.max_mem_per_proc - global_config.min_mem_per_proc + 1)) 
                             + global_config.min_mem_per_proc;
    }

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;
    if (instruction_count < 1) instruction_count = 1;

    std::vector<Instruction> instructions;
    std::vector<std::string> known_variables;
    std::unordered_set<std::string> known_variables_set;

    std::string initial_var = "v_start";
    known_variables.push_back(initial_var);
    known_variables_set.insert(initial_var);
    instructions.push_back({"DECLARE", {initial_var, "0"}});

    for (int i = 0; i < instruction_count - 1; ++i) {
        Instruction inst;
        int choice = rand() % 100;

        if (choice < 20) { 
            std::string new_var;
            do {
                new_var = "v" + std::to_string(rand() % 5000);
            } while (known_variables_set.count(new_var));
            
            known_variables.push_back(new_var);
            known_variables_set.insert(new_var);
            inst = {"DECLARE", {new_var, std::to_string(rand() % 100)}};
        } else { 
            if (rand() % 4 == 0) {
                 inst = {"PRINT", {known_variables[rand() % known_variables.size()]}};
            } else {
                std::string dest = known_variables[rand() % known_variables.size()];
                std::string op1 = known_variables[rand() % known_variables.size()];
                std::string op2 = (rand() % 2 == 0) ? known_variables[rand() % known_variables.size()] : std::to_string(rand() % 100);
                inst = {(rand() % 2 == 0 ? "ADD" : "SUBTRACT"), {dest, op1, op2}};
            }
        }
        instructions.push_back(inst);
    }

    p->instructions = std::move(instructions);

    // --- PHASE 3: CALCULATE MEMORY SEGMENTS ---
    // This is a simulator approximation. We assume each instruction
    // would take up an average of 8 bytes if it were real machine code.
    const size_t AVG_INSTRUCTION_SIZE = 8;
    p->instruction_segment_size = p->instructions.size() * AVG_INSTRUCTION_SIZE;

    // Ensure the data segment has at least some minimum space (e.g., one page).
    // If the instructions take up almost all the memory, this prevents errors.
    if (p->instruction_segment_size + 256 > p->memory_required) {
        if (p->memory_required > 256) {
            p->instruction_segment_size = p->memory_required - 256;
        } else {
            // This process is too small for its code, an edge case.
            p->instruction_segment_size = p->memory_required;
        }
    }
    
    // --- FIX: Ensure the data segment starts on an even boundary ---
    // This prevents writes from crossing page boundaries.
    p->instruction_segment_size &= ~1; 

    return p;
}
    
void process_generator_thread() {
    uint64_t last_gen_tick = 0;
    while (system_running) {
        if (generating_processes) {
            uint64_t current_tick = cpu_ticks.load();
            if (!pending_memory_queue.empty()) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                size_t pending_count = pending_memory_queue.size();
                for (size_t i = 0; i < pending_count; ++i) {
                    Process* proc_to_retry = pending_memory_queue.front();
                    pending_memory_queue.pop();
                    if (global_mem_manager->createProcess(*proc_to_retry)) {
                        std::cout << "[Generator] Successfully allocated memory for pending process " << proc_to_retry->name << std::endl;
                        ready_queue.push(proc_to_retry);
                        queue_cv.notify_one();
                    }
                    else {
                        pending_memory_queue.push(proc_to_retry);
                    }
                }
            }
            if (global_config.batch_process_freq > 0 &&
                current_tick > last_gen_tick &&
                current_tick % global_config.batch_process_freq == 0) {
                last_gen_tick = current_tick;
                Process* new_proc = create_random_process("auto_p" + std::to_string(g_next_pid.load()), 0);
                if (global_mem_manager->createProcess(*new_proc)) {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    ready_queue.push(new_proc);
                    queue_cv.notify_all();
                }
                else {
                    std::cout << "[Generator] Memory full. Moving new process " << new_proc->name << " to pending queue." << std::endl;
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    pending_memory_queue.push(new_proc);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}