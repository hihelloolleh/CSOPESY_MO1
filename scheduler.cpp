#include "scheduler.h"
#include "shared_globals.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set>


// --- Clock Implementation ---
void clock_thread() {
    while (system_running) {
        cpu_ticks++;
        // The sleep duration determines the "speed" of your emulator.
        // 10ms means 100 ticks per second.
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

    std::unordered_set<std::string> declared_vars;
    std::vector<Instruction> instructions;

    auto declare_variable = [&](std::string var_name = "") {
        if (var_name.empty()) {
            var_name = "v" + std::to_string(rand() % 1000);
            while (declared_vars.count(var_name))
                var_name = "v" + std::to_string(rand() % 1000);
        }

        Instruction decl;
        decl.opcode = "DECLARE";
        decl.args = { var_name, std::to_string(rand() % 100) };
        declared_vars.insert(var_name);
        instructions.push_back(decl);
        return var_name;
        };

    const std::vector<std::string> op_pool = { "DECLARE", "ADD", "SUBTRACT", "PRINT", "SLEEP" };

    while (instructions.size() < instruction_count) {
        Instruction inst;
        std::string opcode = op_pool[rand() % op_pool.size()];
        std::vector<std::string> vars(declared_vars.begin(), declared_vars.end());

        inst.opcode = opcode;

        if (opcode == "DECLARE") {
            declare_variable();
        }

        else if (opcode == "ADD" || opcode == "SUBTRACT") {
            // Always ensure dest exists (reuse or new)
            std::string dest;
            if (!vars.empty() && rand() % 2 == 0) {
                dest = vars[rand() % vars.size()];
            }
            else {
                dest = declare_variable();
                vars.push_back(dest);
            }

            std::string op2;
            if (!vars.empty()) {
                op2 = vars[rand() % vars.size()];
            }
            else {
                op2 = declare_variable();
                vars.push_back(op2);
            }

            std::string op3;
            if (!vars.empty() && rand() % 2 == 0) {
                op3 = vars[rand() % vars.size()];
            }
            else {
                op3 = std::to_string(rand() % 100); // immediate
            }

            inst.args = { dest, op2, op3 };
        }

        else if (inst.opcode == "PRINT") {
            if (!vars.empty()) {
                std::string var = vars[rand() % vars.size()];
                inst.args = { "Value of " + var + ": ", var };
            }
            else {
                inst.args = { "Hello world from ", p->name };
            }
        }

        else if (inst.opcode == "SLEEP") {
            int ticks = rand() % 10 + 1;
            inst.args = { std::to_string(ticks) };
        }

        instructions.push_back(inst);
    }

    p->instructions = std::move(instructions);
    return p;
}

    
    // std::cout << get_timestamp() << " [Generator] Created process " << p->name << " with " << instruction_count << " instructions." << std::endl;
    // The generation should happen silently in the background so it doesn't interrupt the user's console input.
    


void process_generator_thread() {
    uint64_t last_gen_tick = 0;
    while (system_running) {
        if (generating_processes) {
            uint64_t current_tick = cpu_ticks.load();
            // Check if enough time has passed since the last generation
            // The check 'global_config.batch_process_freq > 0' prevents a divide-by-zero error if the value is 0.
            if (global_config.batch_process_freq > 0 && current_tick > last_gen_tick && current_tick % global_config.batch_process_freq == 0) {
                last_gen_tick = current_tick; // Update the last generation time
                
                Process* new_proc = create_random_process();
                
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    process_list.push_back(new_proc); // Add to the master list
                    ready_queue.push(new_proc);       // Add to the line for the CPUs
                }
                queue_cv.notify_one(); // Tell a waiting CPU core there's a new job
            }
        }

        // Sleep for a short time to prevent this thread from using 100% CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}