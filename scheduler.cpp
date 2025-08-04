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
#include <mutex>

std::string generate_unique_process_name(const std::string& base_name) {
    std::string final_name = base_name;
    int counter = 1;

    // This loop needs to be thread-safe as it accesses the global process_list
    std::lock_guard<std::mutex> lock(queue_mutex);

    // Lambda to check if a name exists
    auto name_exists = [&](const std::string& name) {
        for (const auto& p : process_list) {
            if (p->name == name) {
                return true;
            }
        }
        return false;
        };

    // Keep trying new names until we find one that doesn't exist
    while (name_exists(final_name)) {
        final_name = base_name + "(" + std::to_string(counter) + ")";
        counter++;
    }

    return final_name;
}


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
        size_t random_mem = (rand() % (global_config.max_mem_per_proc - global_config.min_mem_per_proc + 1))
            + global_config.min_mem_per_proc;
        p->memory_required = std::max((size_t)64, random_mem);
    }

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;
    if (instruction_count < 1) instruction_count = 1;

    std::vector<Instruction> instructions;
    std::vector<std::string> known_variables;
    std::unordered_set<std::string> known_variables_set;

    int max_vars = std::min(32, instruction_count);

   std::string initial_var = "v_start";
    known_variables.push_back(initial_var);
    known_variables_set.insert(initial_var);
    instructions.push_back({"DECLARE", {initial_var, "0"}});

    for (int i = 0; i < instruction_count - 1; ++i) {
        Instruction inst;
        int choice = rand() % 100;

        if (choice < 20 && known_variables.size() < max_vars) {
            std::string new_var;
            do {
                new_var = "v" + std::to_string(rand() % 5000);
            } while (known_variables_set.count(new_var));
            
            known_variables.push_back(new_var);
            known_variables_set.insert(new_var);
            inst = {"DECLARE", {new_var, std::to_string(rand() % 100)}};
        } else { 
            int op_choice = rand() % 6; 
            if (op_choice == 0) {
                inst = { "PRINT", {known_variables[rand() % known_variables.size()]} };
            }
            else if (op_choice == 1) { // WRITE instruction
                std::stringstream hex_addr;
                size_t num_slots = p->memory_required / 2;
                if (num_slots > 0) {
                    size_t random_slot = rand() % num_slots;
                    uint16_t safe_address = random_slot * 2;
                    hex_addr << "0x" << std::hex << safe_address;
                }
                else {
                    hex_addr << "0x0"; 
                }

                std::string value_to_write = (rand() % 2 == 0)
                    ? known_variables[rand() % known_variables.size()]
                    : std::to_string(rand() % 65535);
                inst = { "WRITE", {hex_addr.str(), value_to_write} };
            }
            else if (op_choice == 2 && !known_variables.empty()) { // READ instruction
                std::string dest_var = known_variables[rand() % known_variables.size()];
                std::stringstream hex_addr;


                size_t num_slots = p->memory_required / 2;
                if (num_slots > 0) {
                    size_t random_slot = rand() % num_slots;
                    uint16_t safe_address = random_slot * 2;
                    hex_addr << "0x" << std::hex << safe_address;
                }
                else {
                    hex_addr << "0x0";
                }

                inst = { "READ", {dest_var, hex_addr.str()} };
            }
            else { // Default to ADD/SUBTRACT
                std::string dest = known_variables[rand() % known_variables.size()];
                std::string op1 = known_variables[rand() % known_variables.size()];
                std::string op2 = (rand() % 2 == 0) ? known_variables[rand() % known_variables.size()] : std::to_string(rand() % 100);
                inst = { (rand() % 2 == 0 ? "ADD" : "SUBTRACT"), {dest, op1, op2} };
            }
        }
        instructions.push_back(inst);
    }

    p->instructions = std::move(instructions);

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
                std::string base_name = "p" + std::to_string(g_next_pid.load());
                std::string unique_name = generate_unique_process_name(base_name);

                Process* new_proc = create_random_process(unique_name, 0);
                if (global_mem_manager->createProcess(*new_proc)) {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    ready_queue.push(new_proc);
                    queue_cv.notify_all();
                }
                else {
                    //std::cout << "\n[Generator] Memory full. Moving new process " << new_proc->name << " to pending queue." << std::endl;
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc);
                    pending_memory_queue.push(new_proc);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}