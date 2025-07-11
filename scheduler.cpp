#include "scheduler.h"
#include "shared_globals.h"
#include "mem_manager.h"
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
    int current_for_depth = 0;
    const int max_for_depth = 3;
    static std::atomic<int> process_counter(0);
    process_counter++;

    Process* p = new Process();
    p->id = process_counter;
    p->name = "p" + std::to_string(p->id);
    p->priority = rand() % 100;

    int instruction_count = rand() % (global_config.max_ins - global_config.min_ins + 1) + global_config.min_ins;

    std::unordered_set<std::string> declared_vars;
    std::vector<Instruction> instructions;
    std::vector<std::string> vars(declared_vars.begin(), declared_vars.end());


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

    const std::vector<std::string> op_pool = { "DECLARE", "ADD", "SUBTRACT", "PRINT", "SLEEP", "FOR"};

    auto generate_random_instruction = [&](std::vector<std::string>& vars) -> Instruction {
        Instruction inst;
        std::string opcode = op_pool[rand() % (op_pool.size() - 1)]; // Exclude FOR for sub instructions

        inst.opcode = opcode;

        if (opcode == "DECLARE") {
            std::string new_var = declare_variable();
            vars.push_back(new_var);
            // `declare_variable` already adds to instructions.
            inst = Instruction{}; // Skip further processing.
        }

        else if (opcode == "ADD" || opcode == "SUBTRACT") {
            std::string dest = !vars.empty() ? vars[rand() % vars.size()] : declare_variable();
            std::string op2 = !vars.empty() ? vars[rand() % vars.size()] : declare_variable();
            std::string op3 = (rand() % 2 == 0 && !vars.empty()) ? vars[rand() % vars.size()] : std::to_string(rand() % 100);

            inst.args = { dest, op2, op3 };
        }

        else if (opcode == "PRINT") {
            if (!vars.empty()) {
                std::string var = vars[rand() % vars.size()];
                inst.args = { "Value of " + var + ": ", var };
            }
            else {
                inst.args = { "Hello world from ", p->name };
            }
        }

        else if (opcode == "SLEEP") {
            inst.args = { std::to_string(rand() % 10 + 1) };
        }

        return inst;
        };


    while (instructions.size() < instruction_count) {
        std::string opcode = op_pool[rand() % op_pool.size()];

        if (opcode == "FOR" && current_for_depth < max_for_depth) {
            Instruction for_instr;
            for_instr.opcode = "FOR";
            int repeat_count = rand() % 4 + 2;
            for_instr.args = { std::to_string(repeat_count) };

            current_for_depth++;  // Increase depth

            int sub_instr_count = rand() % 3 + 1;
            for (int i = 0; i < sub_instr_count; ++i) {
                Instruction sub = generate_random_instruction(vars);

                // OPTIONAL: Prevent nested FOR inside another FOR
                if (sub.opcode != "FOR" && !sub.opcode.empty()) {
                    for_instr.sub_instructions.push_back(sub);
                }
            }

            current_for_depth--;  // Done with this FOR

            instructions.push_back(for_instr);
        }
        else {
            Instruction inst = generate_random_instruction(vars);
            if (!inst.opcode.empty()) {
                instructions.push_back(inst);
            }
        }
    }


    p->instructions = std::move(instructions);

    int varsPerPage = 1; // 1 variable per page (adjust based on real size)
    int insPerPage = 1;  // 1 instruction per page

    int varPageCount = (declared_vars.size() + varsPerPage - 1) / varsPerPage;
    int insPageCount = (instruction_count + insPerPage - 1) / insPerPage;

    for (int i = 0; i < insPageCount; ++i)
        p->insPages.push_back(i);

    for (int i = 0; i < varPageCount; ++i)
        p->varPages.push_back(insPageCount + i);

    return p;
}

    
    // std::cout << get_timestamp() << " [Generator] Created process " << p->name << " with " << instruction_count << " instructions." << std::endl;
    // The generation should happen silently in the background so it doesn't interrupt the user's console input.
    


void process_generator_thread() {
    uint64_t last_gen_tick = 0;
    while (system_running) {
        if (generating_processes) {
            uint64_t current_tick = cpu_ticks.load();

            // Ensure generation frequency is valid
            if (global_config.batch_process_freq > 0 &&
                current_tick > last_gen_tick &&
                current_tick % global_config.batch_process_freq == 0) {

                last_gen_tick = current_tick;

                Process* new_proc = create_random_process();

                if (!global_mem_manager->createProcess(*new_proc, global_config.mem_per_proc)) {
                    std::cout << "Memory full. Process " << new_proc->name << " will be re-queued.\n";
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    ready_queue.push(new_proc); // Requeue to tail
                    continue;
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
