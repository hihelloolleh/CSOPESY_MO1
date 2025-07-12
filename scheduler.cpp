#include "scheduler.h"
#include "shared_globals.h"
#include "mem_manager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set> // Still useful for ensuring unique names within a process during generation

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
    if (instruction_count < 1) instruction_count = 1; // Ensure at least one instruction

    std::vector<Instruction> instructions_for_process; // Will hold all instructions for this process
    
    // --- Strategy for variable generation ---
    // Instead of directly populating `p->variables`, we now generate `DECLARE` instructions
    // which will, upon execution, assign virtual addresses via `get_or_assign_variable_virtual_address`.
    // We keep a temporary set of `string` variable names here for the random generator
    // to pick from when creating ADD/SUBTRACT/PRINT instructions.
    
    std::unordered_set<std::string> temp_declared_var_names;
    std::vector<std::string> temp_var_names_vec; // For random access

    // Always declare at least a few variables at the start of the program
    // so ADD/SUBTRACT/PRINT have something to work with.
    int initial_declarations = rand() % 5 + 3; // 3 to 7 initial variables
    for (int i = 0; i < initial_declarations; ++i) {
        std::string var_name = "v" + std::to_string(rand() % 1000);
        while(temp_declared_var_names.count(var_name)) { // Ensure unique variable name for this process
            var_name = "v" + std::to_string(rand() % 1000);
        }
        temp_declared_var_names.insert(var_name);
        temp_var_names_vec.push_back(var_name);

        Instruction decl_instr;
        decl_instr.opcode = "DECLARE";
        decl_instr.args = {var_name, std::to_string(rand() % 100)}; // Initial value
        instructions_for_process.push_back(decl_instr);
    }
    
    // Now, generate the main body of instructions
    const std::vector<std::string> op_pool = { "ADD", "SUBTRACT", "PRINT", "SLEEP", "FOR" };
    // Exclude DECLARE from the random pool for main instructions since we pre-declared
    // or let it be handled by read/write helpers for undeclared access.
    
    int current_for_depth = 0;
    const int max_for_depth = 3;

    for (int i = 0; instructions_for_process.size() < instruction_count; ++i) {
        Instruction inst;
        std::string opcode = op_pool[rand() % op_pool.size()];

        if (opcode == "FOR" && current_for_depth < max_for_depth) {
            Instruction for_instr;
            for_instr.opcode = "FOR";
            int repeat_count = rand() % 4 + 2; // 2 to 5 repeats
            for_instr.args = { std::to_string(repeat_count) };

            current_for_depth++;

            int sub_instr_count = rand() % 3 + 1; // 1 to 3 sub-instructions
            for (int j = 0; j < sub_instr_count; ++j) {
                Instruction sub_inst;
                std::string sub_opcode = op_pool[rand() % (op_pool.size() - 1)]; // Don't nest FOR directly for simplicity

                sub_inst.opcode = sub_opcode;

                if (sub_opcode == "ADD" || sub_opcode == "SUBTRACT") {
                    if (temp_var_names_vec.empty()) continue; // Need variables to operate
                    std::string dest = temp_var_names_vec[rand() % temp_var_names_vec.size()];
                    std::string op2 = temp_var_names_vec[rand() % temp_var_names_vec.size()];
                    std::string op3 = (rand() % 2 == 0) ? temp_var_names_vec[rand() % temp_var_names_vec.size()] : std::to_string(rand() % 100);
                    sub_inst.args = { dest, op2, op3 };
                } else if (sub_opcode == "PRINT") {
                    if (temp_var_names_vec.empty()) continue;
                    std::string var_to_print = temp_var_names_vec[rand() % temp_var_names_vec.size()];
                    sub_inst.args = { "Val: ", var_to_print };
                } else if (sub_opcode == "SLEEP") {
                    sub_inst.args = { std::to_string(rand() % 5 + 1) }; // Shorter sleep in loops
                }

                if (!sub_inst.opcode.empty()) { // Only add if a valid instruction was generated
                    for_instr.sub_instructions.push_back(sub_inst);
                }
            }
            current_for_depth--;

            if (!for_instr.sub_instructions.empty()) { // Only add FOR if it has a body
                instructions_for_process.push_back(for_instr);
            }

        } else { // Not a FOR loop, or max depth reached
            if (opcode == "ADD" || opcode == "SUBTRACT") {
                 if (temp_var_names_vec.empty()) continue; // Need variables to operate
                 std::string dest = temp_var_names_vec[rand() % temp_var_names_vec.size()];
                 std::string op2 = temp_var_names_vec[rand() % temp_var_names_vec.size()];
                 std::string op3 = (rand() % 2 == 0) ? temp_var_names_vec[rand() % temp_var_names_vec.size()] : std::to_string(rand() % 100);
                 inst.args = { dest, op2, op3 };
            } else if (opcode == "PRINT") {
                if (temp_var_names_vec.empty()) { // If no variables, just print a generic message
                    inst.args = { "Hello from ", p->name };
                } else {
                    std::string var_to_print = temp_var_names_vec[rand() % temp_var_names_vec.size()];
                    inst.args = { "Value of ", var_to_print, " is: ", var_to_print };
                }
            } else if (opcode == "SLEEP") {
                inst.args = { std::to_string(rand() % 10 + 1) };
            }
            // For DECLARE if included in op_pool for main loop:
            // This is now handled by the initial declarations and auto-declaration on first access.
            // If you wanted to randomly DECLARE new variables throughout the program,
            // you'd add similar logic here, potentially expanding `temp_var_names_vec`.

            if (!inst.opcode.empty()) {
                instructions_for_process.push_back(inst);
            }
        }
    }

    p->instructions = std::move(instructions_for_process);

    // Calculate pages needed for instructions (fixed size, based on count)
    // Assuming 1 instruction == 1 byte for page calculation simplicity, or a fixed size per instruction
    // Let's assume average instruction size, or simplify to just instruction count for page allocation.
    // Given your mem_per_frame 16, and min/max_ins 100, let's say one instruction is small (e.g., 1 byte).
    // So 100 instructions could be 100 bytes.
    // For simplicity, let's just make it proportional to instruction count.
    int insBytes = p->instructions.size() * 2; // Roughly, assuming an instruction header/opcode is 2 bytes or so.
    if (insBytes < 1) insBytes = 2; // At least one byte for instructions

    // Calculate varPages based on potential max variables if all are declared.
    // A process is given `global_config.mem_per_proc` total memory for its virtual address space.
    // The number of varPages should reflect this.
    int varPagesCount = (global_config.mem_per_proc + global_config.mem_per_frame - 1) / global_config.mem_per_frame;

    // IMPORTANT: The variable pages are for the *process's data*, not for the instructions.
    // The `mem-per-proc` (4096) is the total virtual memory size for the process, covering
    // both instructions and data/variables.
    // Let's rethink `insPages` and `varPages` to simply reflect how many *virtual* pages
    // the process *could* use within its `mem_per_proc` total.

    // A process's virtual memory size is fixed by global_config.mem_per_proc.
    // Number of virtual pages = mem_per_proc / mem_per_frame
    int total_virtual_pages = global_config.mem_per_proc / global_config.mem_per_frame;

    p->insPages.clear();
    p->varPages.clear();

    // Assign virtual pages. For simplicity, let's say the first portion is for instructions,
    // and the rest is for data/variables.
    // This is a simplification; in a real system, instruction and data spaces might be distinct
    // or loaded based on usage. For this, we just need the pages to exist for the MemoryManager.
    
    // Assign all virtual pages (from 0 up to total_virtual_pages - 1) to the process's page table.
    // When an instruction or variable is accessed, the virtual address will map to one of these pages.
    for (int i = 0; i < total_virtual_pages; ++i) {
        // We'll arbitrarily say the first half are "instruction" pages, second half "variable" pages
        // This is primarily for the PCB to hold all possible pages for the process.
        if (i < total_virtual_pages / 2) {
            p->insPages.push_back(i); 
        } else {
            p->varPages.push_back(i);
        }
    }
    // Ensure at least one page if mem_per_proc is very small but > 0
    if (total_virtual_pages == 0 && global_config.mem_per_proc > 0) {
        p->varPages.push_back(0); // At least one page
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
                    // std::cout << "Memory full. Process " << new_proc->name << " will be re-queued.\n"; // Suppress for cleaner output
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        ready_queue.push(new_proc); // Requeue to tail
                    }
                    continue; // Skip the rest of this iteration if creation failed
                }

                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    ready_queue.push(new_proc);
                }

                queue_cv.notify_one(); // Tell a CPU core there's work
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}