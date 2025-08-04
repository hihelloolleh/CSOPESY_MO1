// --- C++ Standard Libraries ---
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <chrono>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// --- Project-Specific Headers ---
#include "shared_globals.h"
#include "config.h"
#include "cpu_core.h"
#include "scheduler.h"
#include "display.h"
#include "instructions.h"
#include "process.h"
#include "mem_manager.h"

std::vector<std::thread> cpu_worker_threads;

void start_cpu_cores() {
    cpu_worker_threads.clear();
    core_busy.clear();
    core_busy.resize(global_config.num_cpu, false);
    for (int i = 0; i < global_config.num_cpu; ++i) {
        cpu_worker_threads.emplace_back(cpu_core_worker, i);
    }
    std::cout << global_config.num_cpu << " CPU cores have been started." << std::endl;
}

void enter_process_screen(const std::string& process_name, bool allow_create, size_t memory_size = 0) {
    Process* target_process = nullptr;

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (auto& p : process_list) {
            if (p->name == process_name) {
                target_process = p;
                break;
            }
        }
    }

    if (!target_process && allow_create) {
        target_process = create_random_process(process_name, memory_size);

        if (!global_mem_manager->createProcess(*target_process)) {
            std::cout << "Failed to create process <" << process_name << ">.\n";
            delete target_process;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            process_list.push_back(target_process);
            ready_queue.push(target_process);
        }
        queue_cv.notify_all();
        std::cout << "Process <" << process_name << "> created.\n";
    }

    if (!target_process) {
        std::cout << "Process <" << process_name << "> not found.\n";
        return;
    }


    // === INTERACTIVE CLI FOR PROCESS ===
    bool in_process_view = true;
    while (in_process_view) {
        clear_console();
        display_process_view(target_process);
        std::cout << "root:\\" << process_name << "> ";
        std::string process_command;
        std::getline(std::cin, process_command);

        if (process_command == "exit") {
            in_process_view = false;
        }
        else if (process_command == "process-smi") {
            show_global_process_smi();
        }
        else {
            std::stringstream ss(process_command);
            std::string opcode;
            ss >> opcode;

            if (target_process->finished && process_command != "exit" && process_command != "process-smi") {
                std::cout << "Process has finished execution. Cannot modify instructions.\n";
                continue;
            }

            if (opcode == "FOR") {
                int repeat_count;
                ss >> repeat_count;
                if (repeat_count <= 0) {
                    std::cout << "Invalid repeat count.\n";
                    continue;
                }

                std::vector<Instruction> loop_body;
                std::string loop_line;
                std::cout << "Enter loop body (type ENDFOR to finish):\n";
                while (true) {
                    std::cout << ">> ";
                    std::getline(std::cin, loop_line);
                    if (loop_line == "ENDFOR") break;

                    std::stringstream lss(loop_line);
                    std::string sub_opcode;
                    lss >> sub_opcode;

                    Instruction sub_instr;
                    sub_instr.opcode = sub_opcode;

                    std::string arg;
                    while (lss >> arg)
                        sub_instr.args.push_back(arg);

                    loop_body.push_back(sub_instr);
                }

                Instruction for_instr;
                for_instr.opcode = "FOR";
                for_instr.args = { std::to_string(repeat_count) };
                for_instr.sub_instructions = loop_body;

                target_process->instructions.push_back(for_instr);
                std::cout << "Instructions in process:\n";
                for (size_t i = 0; i < target_process->instructions.size(); ++i) {
                    std::cout << i << ": " << target_process->instructions[i].opcode << "\n";
                }

                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    ready_queue.push(target_process);
                }
                queue_cv.notify_one();
            }
            else if (
                opcode == "DECLARE" || opcode == "ADD" || opcode == "SUBTRACT" ||
                opcode == "PRINT" || opcode == "SLEEP"
                ) {
                Instruction instr;
                instr.opcode = opcode;

                std::string arg;
                while (ss >> arg)
                    instr.args.push_back(arg);

                if (opcode == "SLEEP") {
                    if (instr.args.empty() || !std::all_of(instr.args[0].begin(), instr.args[0].end(), ::isdigit)) {
                        std::cout << "Invalid SLEEP duration.\n";
                        continue;
                    }
                }

                target_process->instructions.push_back(instr);
                std::cout << "Instruction added: " << opcode << "\n";

                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    ready_queue.push(target_process);
                }
                queue_cv.notify_one();
            }
            else {
                std::cout << "Unknown command. Try one of: ADD, SUBTRACT, DECLARE, PRINT, SLEEP, FOR, process-smi, exit.\n";
            }
        }
    }

    clear_console();
    print_header();
}

void cli_loop() {
    std::string line;
    clear_console();
    print_header();
    std::cout << "\nType 'initialize' to begin or 'exit' to quit.\n\n";

    while (system_running) {
        std::cout << "root:\\> ";
        if (!std::getline(std::cin, line)) {
            if (std::cin.eof()) {
                system_running = false;
                queue_cv.notify_all();
            }
            break;
        }

        std::stringstream ss(line);
        std::string command, arg1, arg2, arg3;
        ss >> command >> arg1 >> arg2 >> arg3;

        if (command.empty()) continue;

        if (command == "exit") {
            system_running = false;
            queue_cv.notify_all();
            break;
        }
        if (command == "clear") {
            clear_console();
            print_header();
            continue;
        }

        if (!is_initialized) {
            if (command == "initialize") {
                if (loadConfiguration("config.txt", global_config)) {
                    global_mem_manager = new MemoryManager(global_config);
                    is_initialized = true;
                    std::cout << "System initialized successfully from config.txt." << std::endl;
                    start_cpu_cores();
                    std::thread(process_generator_thread).detach();
                } else {
                    std::cerr << "Initialization FAILED. Please check config.txt and try again." << std::endl;
                }
            } else {
                std::cerr << "Error: System not initialized. Please run 'initialize' first." << std::endl;
            }
            continue;
        }

        if (command == "initialize") {
            std::cout << "System is already initialized." << std::endl;
        } else if (command == "scheduler-start") {
            generating_processes = true;
            std::cout << "Process generator started." << std::endl;
        } else if (command == "scheduler-stop") {
            generating_processes = false;
            std::cout << "Process generator stopped." << std::endl;
        } else if (command == "process-smi") {
            show_global_process_smi();
        } else if (command == "screen") {
            if (arg1 == "-ls") {
                generate_system_report(std::cout);
            } else if (arg1 == "-s" && !arg2.empty() && !arg3.empty()) {
                try {
                    size_t mem_size = std::stoull(arg3);
                    // Validate memory size as per spec
                    bool is_power_of_two = (mem_size > 0) && ((mem_size & (mem_size - 1)) == 0);
                    if (is_power_of_two && mem_size >= 64 && mem_size <= 65536) {
                        enter_process_screen(arg2, true, mem_size);
                        continue;
                    } else {
                        std::cout << "Invalid memory allocation. Must be a power of 2 between 64 and 65536.\n";
                    }
                } catch (...) {
                    std::cout << "Invalid memory size format.\n";
                }

            }

            else if (arg1 == "-c" && !arg2.empty() && !arg3.empty()) {
                std::string remaining_line;
                std::getline(ss, remaining_line); // Get instruction string

                // Remove possible surrounding quotes
                if (!remaining_line.empty() && remaining_line.front() == '"') {
                    remaining_line.erase(0, 1);
                }
                if (!remaining_line.empty() && remaining_line.back() == '"') {
                    remaining_line.pop_back();
                }

                try {
                    size_t mem_size = std::stoull(arg3);
                    bool is_power_of_two = (mem_size > 0) && ((mem_size & (mem_size - 1)) == 0);
                    if (!is_power_of_two || mem_size < 64 || mem_size > 65536) {
                        std::cout << "Invalid memory size. Must be power of 2 between 64 and 65536.\n";
                        return;
                    }

                    Process* new_proc = new Process(g_next_pid++, arg2, mem_size);

                    // Parse instructions
                    std::stringstream instr_stream(remaining_line);
                    std::string token;
                    while (std::getline(instr_stream, token, ';')) {
                        std::stringstream instr_line(token);
                        std::string opcode;
                        instr_line >> opcode;

                        Instruction instr;
                        instr.opcode = opcode;

                        std::string arg;
                        while (instr_line >> arg) {
                            instr.args.push_back(arg);
                        }

                        new_proc->instructions.push_back(instr);
                    }

                    if (new_proc->instructions.size() < 1 || new_proc->instructions.size() > 50) {
                        std::cout << "invalid command\n";
                        delete new_proc;
                        return;
                    }

                    // Register process with memory manager
                    if (!global_mem_manager->createProcess(*new_proc)) {
                        std::cout << "Memory allocation failed for process '" << arg2 << "'.\n";
                        delete new_proc;
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        process_list.push_back(new_proc);
                        ready_queue.push(new_proc);
                    }
                    queue_cv.notify_one();

                    std::cout << "Process '" << arg2 << "' created with instructions.\n";
                }
                catch (...) {
                    std::cout << "Invalid arguments for screen -c\n";
                }
            }
            
            else if (arg1 == "-r" && !arg2.empty()) {
                enter_process_screen(arg2, false);
                continue;
            } else {
                std::cout << "Invalid screen usage. Try 'screen -ls' or 'screen -s <name> <mem_size>'.\n";
            }
        } else {
            std::cout << "Unknown command: '" << line << "'" << std::endl;
        }
    }
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    std::thread master_clock_thread(clock_thread);

    cli_loop();

    std::cout << "\nShutdown initiated. Waiting for background threads to complete..." << std::endl;
    
    if (master_clock_thread.joinable()) master_clock_thread.join();

    for (auto& t : cpu_worker_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // --- CLEAN UP THE MEMORY MANAGER ---
    if (global_mem_manager) {
        global_mem_manager->flushAsyncWrites(); // Call your existing cleanup function
        delete global_mem_manager;
        global_mem_manager = nullptr;
    }
    // ---

    std::cout << "Cleaning up " << process_list.size() << " process records..." << std::endl;
    for (auto p : process_list) {
        delete p;
    }
    process_list.clear();

    std::cout << "Shutdown complete. Goodbye!" << std::endl;
    return 0;
}