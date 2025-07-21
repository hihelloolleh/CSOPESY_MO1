#include "scheduler.h"
#include "shared_globals.h"
#include "mem_manager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set> 
#include <algorithm> // For std::find

// --- Clock Implementation (No change) ---
void clock_thread() {
    while (system_running) {
        cpu_ticks++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

Process* create_random_process(size_t memory_required) {
    static std::atomic<int> process_counter(0);
    process_counter++;

    Process* p = new Process();
    p->id = process_counter;
    p->name = "p" + std::to_string(p->id);
    p->priority = rand() % 100;
    p->memory_required = memory_required;

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;
    if (instruction_count < 1) instruction_count = 1; 

    std::vector<Instruction> instructions_for_process; 
    
    // --- BEGIN: Very Strict Variable Management (Refined) ---
    const int MIN_VARS_PER_PROCESS = 3; 
    const int MAX_VARS_TO_GENERATE = 10; // This process will use a maximum of 10 distinct variables
    
    // Determine how many variables this specific process instance will have
    int num_vars_for_this_process = rand() % (MAX_VARS_TO_GENERATE - MIN_VARS_PER_PROCESS + 1) + MIN_VARS_PER_PROCESS;
    
    std::vector<std::string> process_var_names;
    std::unordered_set<std::string> used_var_names_for_uniqueness; 

    // Generate unique variable names and create their DECLARE instructions
    for (int i = 0; i < num_vars_for_this_process; ++i) {
        std::string var_name;
        // Generate a variable name that is guaranteed to be unique within this process
        // and doesn't conflict with any "special" names (like "0" if that were possible)
        do {
            var_name = "v" + std::to_string(rand() % 900 + 100); // Generates v100 to v999
        } while (used_var_names_for_uniqueness.count(var_name)); 

        used_var_names_for_uniqueness.insert(var_name);
        process_var_names.push_back(var_name);

        Instruction decl_instr;
        decl_instr.opcode = "DECLARE";
        decl_instr.args = {var_name, std::to_string(rand() % 100)}; // Initial value
        instructions_for_process.push_back(decl_instr);
    }
    // Now, `process_var_names` contains all variables this process will ever use.
    // All subsequent ADD/SUBTRACT/PRINT instructions must pick from this list.
    // --- END: Very Strict Variable Management (Refined) ---


    int current_for_depth = 0;
    const int max_for_depth = 3;
    const std::vector<std::string> op_pool = { "ADD", "SUBTRACT", "PRINT", "SLEEP", "FOR" };

    // Generate the remaining instructions up to instruction_count
    while (instructions_for_process.size() < instruction_count) {
        Instruction inst;
        std::string opcode = op_pool[rand() % op_pool.size()];

        // If an instruction requiring variables is generated AND the process has no variables, skip it.
        // This defensive check is here, though process_var_names should never be empty at this point.
        if ((opcode == "ADD" || opcode == "SUBTRACT" || opcode == "PRINT") && process_var_names.empty()) {
            continue; 
        }

        if (opcode == "FOR" && current_for_depth < max_for_depth) {
            Instruction for_instr;
            for_instr.opcode = "FOR";
            int repeat_count = rand() % 4 + 2; // 2 to 5 repeats
            for_instr.args = { std::to_string(repeat_count) };

            current_for_depth++;

            int sub_instr_count = rand() % 3 + 1; // 1 to 3 sub-instructions
            for (int j = 0; j < sub_instr_count; ++j) {
                Instruction sub_inst;
                // Exclude FOR for sub-instructions
                std::string sub_opcode = op_pool[rand() % (op_pool.size() - 1)]; 

                // If sub_opcode requires variables, and somehow process_var_names is empty, skip.
                if ((sub_opcode == "ADD" || sub_opcode == "SUBTRACT" || sub_opcode == "PRINT") && process_var_names.empty()) {
                    continue; 
                }

                sub_inst.opcode = sub_opcode;

                if (sub_opcode == "ADD" || sub_opcode == "SUBTRACT") {
                    // CRITICAL: Ensure we ONLY pick from process_var_names
                    std::string dest = process_var_names[rand() % process_var_names.size()];
                    std::string op2 = process_var_names[rand() % process_var_names.size()];
                    std::string op3_val;
                    if (rand() % 2 == 0) { // 50% chance to use an existing variable as op3
                        op3_val = process_var_names[rand() % process_var_names.size()];
                    } else { // Otherwise, a literal
                        op3_val = std::to_string(rand() % 100);
                    }
                    sub_inst.args = { dest, op2, op3_val };
                } else if (sub_opcode == "PRINT") {
                    // CRITICAL: Ensure we ONLY pick from process_var_names
                    // Check process_var_names again for safety, though it should not be empty
                    if (!process_var_names.empty()) {
                        std::string var_to_print = process_var_names[rand() % process_var_names.size()];
                        // Generate a simple, single-argument instruction.
                        sub_inst.args = { var_to_print };
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
                 // CRITICAL: Ensure we ONLY pick from process_var_names
                 std::string dest = process_var_names[rand() % process_var_names.size()];
                 std::string op2 = process_var_names[rand() % process_var_names.size()];
                 std::string op3_val;
                 if (rand() % 2 == 0) {
                    op3_val = process_var_names[rand() % process_var_names.size()];
                 } else {
                    op3_val = std::to_string(rand() % 100);
                 }
                 inst.args = { dest, op2, op3_val };
            } else if (opcode == "PRINT") {
                // Check process_var_names again for safety, though it should not be empty
                if (!process_var_names.empty()) {
                    std::string var_to_print = process_var_names[rand() % process_var_names.size()];
                    // FIX: Generate a simple, single-argument instruction.
                    inst.args = { var_to_print };
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

    // Assign all virtual pages that comprise the process's total virtual memory (mem_per_proc).
    // This defines the full virtual address space for the process.
    size_t total_virtual_pages = memory_required / global_config.mem_per_frame;
    // Account for any remainder in the last page
    if (memory_required % global_config.mem_per_frame != 0) {
        total_virtual_pages++;
    }
    // Ensure at least one virtual page if mem_per_proc is very small but greater than 0
    if (total_virtual_pages == 0 && memory_required > 0) {
        total_virtual_pages = 1;
    }

    p->insPages.clear(); // Clear any existing content
    p->varPages.clear(); // Clear any existing content

    // Populate the process's page table with all its virtual page numbers.
    // The distinction between 'insPages' and 'varPages' is more for logical grouping
    // in the PCB setup; the MemoryManager sees a contiguous virtual address space.
    for (int i = 0; i < total_virtual_pages; ++i) {
        // Arbitrarily assign to varPages for simplicity as all pages are equally swappable.
        p->varPages.push_back(i); 
    }

    return p;
}
    
// --- Process Generator Thread ---
void process_generator_thread() {
    uint64_t last_gen_tick = 0;
    while (system_running) {
        if (generating_processes) {
            uint64_t current_tick = cpu_ticks.load();

            // --- A: RETRY PENDING PROCESSES ---
            // On every cycle, first check if any processes are waiting for memory.
            if (!pending_memory_queue.empty()) {
                std::lock_guard<std::mutex> lock(queue_mutex);
                int pending_count = pending_memory_queue.size();
                for (int i = 0; i < pending_count; ++i) {
                    Process* proc_to_retry = pending_memory_queue.front();
                    pending_memory_queue.pop();

                    // Try to register with memory manager again
                    if (global_mem_manager->createProcess(*proc_to_retry)) {
                        // SUCCESS! Move it to the ready queue.
                        std::cout << "[Generator] Successfully allocated memory for pending process " << proc_to_retry->name << std::endl;
                        ready_queue.push(proc_to_retry);
                        queue_cv.notify_one();
                    }
                    else {
                        // FAILED AGAIN. Put it back at the end of the pending queue to try later.
                        pending_memory_queue.push(proc_to_retry);
                    }
                }
            }

            // --- B: GENERATE NEW PROCESS ---
            // This logic only runs if the frequency condition is met.
            if (global_config.batch_process_freq > 0 &&
                current_tick > last_gen_tick &&
                current_tick % global_config.batch_process_freq == 0) {

                last_gen_tick = current_tick;

                Process* new_proc = create_random_process(global_config.mem_per_proc);

                // Attempt to register with the memory manager.
                if (global_mem_manager->createProcess(*new_proc)) {
                    // SUCCESS! Add to the master list and the ready queue.
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    ready_queue.push(new_proc);
                    queue_cv.notify_one();
                }
                else {
                    // FAILED. Add to the master list and the PENDING queue.
                    std::cout << "[Generator] Memory full. Moving new process " << new_proc->name << " to pending queue." << std::endl;
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc); // MUST add to master list to prevent memory leak on shutdown.
                    pending_memory_queue.push(new_proc);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // Sleep slightly longer to let CPUs work
    }
}