#include "scheduler.h"
#include "shared_globals.h"
#include "mem_manager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set> // Still useful for ensuring unique names during generation

// --- Clock Implementation (No change) ---
void clock_thread() {
    while (system_running) {
        cpu_ticks++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

Process* create_random_process() {
    static std::atomic<int> process_counter(0);
    process_counter++;

    Process* p = new Process();
    p->id = process_counter;
    p->name = "p" + std::to_string(p->id);
    p->priority = rand() % 100;

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;
    if (instruction_count < 1) instruction_count = 1; 

    std::vector<Instruction> instructions_for_process; 
    
    // --- BEGIN: Stricter Variable Management ---
    // Define a fixed, small pool of variable names for THIS process.
    // This pool will be used to ensure all generated instructions reference valid,
    // pre-accounted-for virtual addresses within the process's memory limit.
    const int NUM_FIXED_VARS = 5; // A small, fixed number of variables per process (e.g., 5 to 10)
    std::vector<std::string> process_var_names;
    
    // Pre-generate unique variable names for this process and add initial DECLARE instructions
    for (int i = 0; i < NUM_FIXED_VARS; ++i) {
        std::string var_name = "v" + std::to_string(rand() % 900 + 100); // v100 to v999 for variety
        // Ensure uniqueness within this process, though unlikely with random % 900
        while (std::find(process_var_names.begin(), process_var_names.end(), var_name) != process_var_names.end()) {
            var_name = "v" + std::to_string(rand() % 900 + 100);
        }
        process_var_names.push_back(var_name);

        Instruction decl_instr;
        decl_instr.opcode = "DECLARE";
        decl_instr.args = {var_name, std::to_string(rand() % 100)}; // Initial value
        instructions_for_process.push_back(decl_instr);
    }
    // All subsequent ADD/SUBTRACT/PRINT instructions will only use variables from `process_var_names`.
    // This explicitly limits how much `next_available_variable_address` will grow.
    // --- END: Stricter Variable Management ---


    int current_for_depth = 0;
    const int max_for_depth = 3;
    const std::vector<std::string> op_pool = { "ADD", "SUBTRACT", "PRINT", "SLEEP", "FOR" };

    for (int i = 0; instructions_for_process.size() < instruction_count; ++i) {
        Instruction inst;
        std::string opcode = op_pool[rand() % op_pool.size()];

        if (opcode == "FOR" && current_for_depth < max_for_depth) {
            Instruction for_instr;
            for_instr.opcode = "FOR";
            int repeat_count = rand() % 4 + 2; 
            for_instr.args = { std::to_string(repeat_count) };

            current_for_depth++;

            int sub_instr_count = rand() % 3 + 1; 
            for (int j = 0; j < sub_instr_count; ++j) {
                Instruction sub_inst;
                std::string sub_opcode = op_pool[rand() % (op_pool.size() - 1)]; 

                sub_inst.opcode = sub_opcode;

                if (sub_opcode == "ADD" || sub_opcode == "SUBTRACT") {
                    if (process_var_names.empty()) continue; // Skip if no variables exist (shouldn't happen with fixed vars)
                    std::string dest = process_var_names[rand() % process_var_names.size()];
                    std::string op2 = process_var_names[rand() % process_var_names.size()];
                    std::string op3_val = std::to_string(rand() % 100);
                    if (rand() % 2 == 0) { // 50% chance to use an existing variable as op3, otherwise a literal
                        op3_val = process_var_names[rand() % process_var_names.size()];
                    }
                    sub_inst.args = { dest, op2, op3_val };
                } else if (sub_opcode == "PRINT") {
                    if (process_var_names.empty()) { 
                        sub_inst.args = { "Loop Hello!" };
                    } else {
                        std::string var_to_print = process_var_names[rand() % process_var_names.size()];
                        sub_inst.args = { "Loop Var:", var_to_print };
                    }
                } else if (sub_opcode == "SLEEP") {
                    sub_inst.args = { std::to_string(rand() % 5 + 1) }; 
                }

                if (!sub_inst.opcode.empty()) { 
                    for_instr.sub_instructions.push_back(sub_inst);
                }
            }
            current_for_depth--;

            if (!for_instr.sub_instructions.empty()) { 
                instructions_for_process.push_back(for_instr);
            }

        } else { // Not a FOR loop, or max depth reached
            if (opcode == "ADD" || opcode == "SUBTRACT") {
                 if (process_var_names.empty()) continue;
                 std::string dest = process_var_names[rand() % process_var_names.size()];
                 std::string op2 = process_var_names[rand() % process_var_names.size()];
                 std::string op3_val = std::to_string(rand() % 100);
                 if (rand() % 2 == 0) {
                    op3_val = process_var_names[rand() % process_var_names.size()];
                 }
                 inst.args = { dest, op2, op3_val };
            } else if (opcode == "PRINT") {
                if (process_var_names.empty()) { 
                    inst.args = { "Hello from ", p->name };
                } else {
                    std::string var_to_print = process_var_names[rand() % process_var_names.size()];
                    inst.args = { "Value of ", var_to_print, ": ", var_to_print };
                }
            } else if (opcode == "SLEEP") {
                inst.args = { std::to_string(rand() % 10 + 1) };
            }

            if (!inst.opcode.empty()) {
                instructions_for_process.push_back(inst);
            }
        }
    }

    p->instructions = std::move(instructions_for_process);

    // Assign all virtual pages (from 0 up to total_virtual_pages - 1) to the process's page table.
    int total_virtual_pages = global_config.mem_per_proc / global_config.mem_per_frame;
    if (global_config.mem_per_proc % global_config.mem_per_frame != 0) { // Account for partial last page
        total_virtual_pages++;
    }
    if (total_virtual_pages == 0 && global_config.mem_per_proc > 0) { // Ensure at least one page if mem_per_proc is very small but > 0
        total_virtual_pages = 1;
    }


    p->insPages.clear(); // Clear existing
    p->varPages.clear(); // Clear existing

    // Assign all virtual page numbers (0 to total_virtual_pages - 1) to the process's page table.
    // The distinction between insPages and varPages is mostly conceptual for PCB setup;
    // MemoryManager just sees a block of virtual memory.
    for (int i = 0; i < total_virtual_pages; ++i) {
        p->varPages.push_back(i); // Arbitrarily put all into varPages for simplicity in PCB
    }

    return p;
}
    
// --- Process Generator Thread (No change apart from `create_random_process` call) ---
void process_generator_thread() {
    uint64_t last_gen_tick = 0;
    while (system_running) {
        if (generating_processes) {
            uint64_t current_tick = cpu_ticks.load();

            if (global_config.batch_process_freq > 0 &&
                current_tick > last_gen_tick &&
                current_tick % global_config.batch_process_freq == 0) {

                last_gen_tick = current_tick;

                Process* new_proc = create_random_process();

                if (!global_mem_manager->createProcess(*new_proc, global_config.mem_per_proc)) {
                    // Memory full. Re-queue the process for a later attempt.
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        ready_queue.push(new_proc); 
                    }
                    continue; 
                }

                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    ready_queue.push(new_proc);
                }

                queue_cv.notify_one(); 
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}