#include "scheduler.h"
#include "shared_globals.h"
#include "mem_manager.h" // Required for global_mem_manager
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set>
#include <algorithm>

std::atomic<int> g_next_pid(1); // Start counting PIDs from 1.

// --- Clock Implementation (No change) ---
void clock_thread() {
    while (system_running) {
        cpu_ticks++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

Process* create_random_process(const std::string& name, size_t memory_size_override) {

    Process* p = new Process();
    p->id = g_next_pid++;
    p->name = name;
    p->priority = rand() % 100;

    // --- START: MEMORY INTEGRATION LOGIC ---
    if (memory_size_override > 0) {
        p->memory_required = memory_size_override;
    } else {
        // Use a value from config for scheduler-generated processes
        p->memory_required = global_config.min_mem_per_proc;
    }

    // Your MemoryManager uses insPages and varPages to build its page table.
    // We need to populate them based on memory_required.
    p->insPages.clear();
    p->varPages.clear();
    size_t num_pages = (p->memory_required + global_config.mem_per_frame - 1) / global_config.mem_per_frame;
    for(size_t i = 0; i < num_pages; ++i) {
        // For simplicity, we'll assign all pages to varPages as your manager handles them generically.
        p->varPages.push_back(i);
    }
    // --- END: MEMORY INTEGRATION LOGIC ---

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;
    if (instruction_count < 1) instruction_count = 1;

    // --- START OF FULL INSTRUCTION GENERATION LOGIC ---
    std::vector<Instruction> instructions_for_process;
    
    // First, create a pool of variables that this process will use.
    const int MIN_VARS_PER_PROCESS = 2; 
    const int MAX_VARS_TO_GENERATE = 10;
    int num_vars_for_this_process = rand() % (MAX_VARS_TO_GENERATE - MIN_VARS_PER_PROCESS + 1) + MIN_VARS_PER_PROCESS;
    
    std::vector<std::string> process_var_names;
    std::unordered_set<std::string> used_var_names_for_uniqueness;

    // Generate unique variable names and create their DECLARE instructions first.
    // This ensures all variables are declared before they are used.
    for (int i = 0; i < num_vars_for_this_process; ++i) {
        std::string var_name;
        do {
            var_name = "v" + std::to_string(rand() % 900 + 100); // e.g., v100 to v999
        } while (used_var_names_for_uniqueness.count(var_name)); 

        used_var_names_for_uniqueness.insert(var_name);
        process_var_names.push_back(var_name);

        Instruction decl_instr;
        decl_instr.opcode = "DECLARE";
        decl_instr.args = {var_name, std::to_string(rand() % 100)}; // Initial value
        instructions_for_process.push_back(decl_instr);
    }

    int current_for_depth = 0;
    const int max_for_depth = 3;
    const std::vector<std::string> op_pool = { "ADD", "SUBTRACT", "PRINT", "SLEEP", "FOR" };

    // Generate the rest of the instructions.
    while (instructions_for_process.size() < instruction_count) {
        std::string opcode = op_pool[rand() % op_pool.size()];

        // Skip if an instruction needs variables but none were created.
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
                // Exclude FOR from sub-instructions to prevent nested FORs in this simple generator
                std::string sub_opcode = op_pool[rand() % (op_pool.size() - 1)]; 

                if ((sub_opcode == "ADD" || sub_opcode == "SUBTRACT" || sub_opcode == "PRINT") && process_var_names.empty()) {
                    continue; 
                }
                sub_inst.opcode = sub_opcode;

                if (sub_opcode == "ADD" || sub_opcode == "SUBTRACT") {
                    std::string dest = process_var_names[rand() % process_var_names.size()];
                    std::string op1 = process_var_names[rand() % process_var_names.size()];
                    std::string op2 = (rand() % 2 == 0) ? process_var_names[rand() % process_var_names.size()] : std::to_string(rand() % 100);
                    sub_inst.args = { dest, op1, op2 };
                } else if (sub_opcode == "PRINT") {
                    sub_inst.args = { "Value of ", process_var_names[rand() % process_var_names.size()], " is: ", process_var_names[rand() % process_var_names.size()] };
                } else if (sub_opcode == "SLEEP") {
                    sub_inst.args = { std::to_string(rand() % 5 + 1) }; 
                }
                for_instr.sub_instructions.push_back(sub_inst);
            }
            current_for_depth--;
            instructions_for_process.push_back(for_instr);

        } else if (opcode != "FOR") { 
            Instruction inst;
            inst.opcode = opcode;
            if (opcode == "ADD" || opcode == "SUBTRACT") {
                 std::string dest = process_var_names[rand() % process_var_names.size()];
                 std::string op1 = process_var_names[rand() % process_var_names.size()];
                 std::string op2 = (rand() % 2 == 0) ? process_var_names[rand() % process_var_names.size()] : std::to_string(rand() % 100);
                 inst.args = { dest, op1, op2 };
            } else if (opcode == "PRINT") {
                inst.args = { "Main scope print: ", process_var_names[rand() % process_var_names.size()] };
            } else if (opcode == "SLEEP") {
                inst.args = { std::to_string(rand() % 10 + 1) };
            }
            instructions_for_process.push_back(inst);
        }
    }

    p->instructions = std::move(instructions_for_process);
    // --- END OF FULL INSTRUCTION GENERATION LOGIC ---

    return p;
}
    
// --- Process Generator Thread ---
void process_generator_thread() {
    uint64_t last_gen_tick = 0;
    while (system_running) {
        if (generating_processes) {
            uint64_t current_tick = cpu_ticks.load();

            // --- A: RETRY PENDING PROCESSES ---
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

            // --- B: GENERATE NEW PROCESS ---
            if (global_config.batch_process_freq > 0 &&
                current_tick > last_gen_tick &&
                current_tick % global_config.batch_process_freq == 0) {

                last_gen_tick = current_tick;

                Process* new_proc = create_random_process("auto_p" + std::to_string(g_next_pid.load()), 0);

                if (global_mem_manager->createProcess(*new_proc)) {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    ready_queue.push(new_proc);
                    queue_cv.notify_one();
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