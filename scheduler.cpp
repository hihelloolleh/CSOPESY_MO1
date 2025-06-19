#include "scheduler.h"
#include "shared_globals.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <random> // Better for random number generation

// --- Clock Implementation ---
void clock_thread() {
    while (system_running) {
        cpu_ticks++;
        // The sleep duration determines the "speed" of your emulator.
        // 10ms means 100 ticks per second.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// --- Process Generator Implementation ---

// Helper function to create a single process with a randomized script
Process* create_random_process() {
    static std::atomic<int> process_counter(0); // Safely count processes
    process_counter++;

    Process* p = new Process();
    p->id = process_counter;
    p->name = "p" + std::to_string(p->id);

    // Determine the number of instructions for this process
    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;

    // A simple list of available opcodes to choose from
    const std::vector<std::string> opcodes = {"DECLARE", "ADD", "PRINT"};

    for (int i = 0; i < instruction_count; ++i) {
        Instruction inst;
        int choice = rand() % opcodes.size();
        inst.opcode = opcodes[choice];

        // Generate arguments based on opcode
        if (inst.opcode == "DECLARE") {
            inst.args.push_back("v" + std::to_string(rand() % 5)); // var name (e.g., v0, v1...)
            inst.args.push_back(std::to_string(rand() % 100));     // value
        } else if (inst.opcode == "ADD") {
            inst.args.push_back("v" + std::to_string(rand() % 5)); // dest var
            inst.args.push_back("v" + std::to_string(rand() % 5)); // src var 1
            inst.args.push_back("v" + std::to_string(rand() % 5)); // src var 2
        } else if (inst.opcode == "PRINT") {
             // As per spec, use a fixed message
            inst.args.push_back("Hello world from " + p->name + "!");
        }
        p->instructions.push_back(inst);
    }
    
    // std::cout << get_timestamp() << " [Generator] Created process " << p->name << " with " << instruction_count << " instructions." << std::endl;
    // The generation should happen silently in the background so it doesn't interrupt the user's console input.
    
    return p;
}


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