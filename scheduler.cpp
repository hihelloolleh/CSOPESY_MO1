#include "scheduler.h"
#include "shared_globals.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

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

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;

    // We will add more opcodes here as we implement them.
    const std::vector<std::string> opcodes = {
    "DECLARE", "ADD", "SUBTRACT", "PRINT", "SLEEP"
    }; //added declare, add, subtract and print

    for (int i = 0; i < instruction_count; ++i) {
        Instruction inst;
        inst.opcode = opcodes[rand() % opcodes.size()];

        if (inst.opcode == "DECLARE") {
            std::string var_name = "v" + std::to_string(rand() % 5);
            int value = rand() % 100;
            inst.args = { var_name, std::to_string(value) };
        }
        else if (inst.opcode == "ADD" || inst.opcode == "SUBTRACT") {
            std::string var1 = "v" + std::to_string(rand() % 5);
            std::string operand1 = "v" + std::to_string(rand() % 5);
            std::string operand2 = std::to_string(rand() % 50);  // Could be a value
            inst.args = { var1, operand1, operand2 };
        }
        else if (inst.opcode == "PRINT") {
            // 50% chance to print a variable or a plain message
            if (rand() % 2 == 0) {
                std::string var = "v" + std::to_string(rand() % 5);
                inst.args = { "Value of " + var + ": ", var };
            }
            else {
                inst.args = { "Hello world from ", p->name };
            }
        }
        else if (inst.opcode == "SLEEP") {
            int ticks = rand() % 10 + 1;  // Sleep between 1 and 10 ticks
            inst.args = { std::to_string(ticks) };
        }

        p->instructions.push_back(inst);
    }
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