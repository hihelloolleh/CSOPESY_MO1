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

void enter_process_screen(const std::string& process_name) {
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
            continue;
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
                        std::string unique_name = generate_unique_process_name(arg2);
                        Process* new_proc = create_random_process(unique_name, mem_size);

                        // 3. Register with Memory Manager.
                        if (!global_mem_manager->createProcess(*new_proc)) {
                            std::cout << "Failed to create process <" << unique_name << ">.\n";
                            delete new_proc;
                        }
                        else {
                            // 4. Add to lists and print the correct confirmation message.
                            {
                                std::lock_guard<std::mutex> lock(queue_mutex);
                                process_list.push_back(new_proc);
                                ready_queue.push(new_proc);
                            }
                            queue_cv.notify_all();
                            std::cout << "Process <" << unique_name << "> created.\n";

                            // 5. Now, enter the screen for the newly created process.
                            enter_process_screen(unique_name);
                        }
                        continue;
                    } else {
                        std::cout << "Invalid memory allocation. Must be a power of 2 between 64 and 65536.\n";
                    }
                } catch (...) {
                    std::cout << "Invalid memory size format.\n";
                }

            }

            else if (arg1 == "-c" && !arg2.empty() && !arg3.empty()) {
                try {
                    size_t mem_size = std::stoull(arg3);
                    bool is_power_of_two = (mem_size > 0) && ((mem_size & (mem_size - 1)) == 0);
                    if (!is_power_of_two || mem_size < 64 || mem_size > 65536) {
                        std::cout << "Invalid memory size. Must be power of 2 between 64 and 65536.\n";
                        continue;
                    }

                    // Read the entire rest of the line, which contains the instruction block.
                    std::string instruction_block;
                    std::getline(ss, instruction_block);

                    // Aggressively trim whitespace and outer quotes from the entire block.
                    instruction_block.erase(0, instruction_block.find_first_not_of(" \t\n\r"));
                    instruction_block.erase(instruction_block.find_last_not_of(" \t\n\r") + 1);
                    if (instruction_block.length() >= 2 && instruction_block.front() == '"' && instruction_block.back() == '"') {
                        instruction_block = instruction_block.substr(1, instruction_block.length() - 2);
                    }
                    std::string unique_name = generate_unique_process_name(arg2);
                    Process* new_proc = new Process(g_next_pid++, unique_name, mem_size);

                    // Tokenize the instruction block by semicolons.
                    std::stringstream instr_stream(instruction_block);
                    std::string token;
                    while (std::getline(instr_stream, token, ';')) {
                        // Trim whitespace from each individual instruction token.
                        token.erase(0, token.find_first_not_of(" \t\n\r"));
                        token.erase(token.find_last_not_of(" \t\n\r") + 1);
                        if (token.empty()) continue;

                        // Manually separate the opcode from the rest of the arguments.
                        std::string opcode;
                        std::string rest_of_line;
                        size_t split_pos = token.find_first_of(" ("); // Split at first space or parenthesis

                        if (split_pos == std::string::npos) { // Command with no arguments
                            opcode = token;
                        }
                        else {
                            opcode = token.substr(0, split_pos);
                            rest_of_line = token.substr(split_pos);
                        }

                        Instruction instr;
                        instr.opcode = opcode;

                        if (opcode == "PRINT") {
                            size_t first_p = rest_of_line.find('(');
                            size_t last_p = rest_of_line.rfind(')');
                            if (first_p == std::string::npos || last_p == std::string::npos || last_p < first_p) {
                                std::cerr << "Invalid PRINT syntax in '" << token << "': missing or mismatched parentheses.\n";
                                continue;
                            }

                            std::string content = rest_of_line.substr(first_p + 1, last_p - first_p - 1);

                            // Tokenize the content by the '+' delimiter
                            std::stringstream content_stream(content);
                            std::string part;
                            while (std::getline(content_stream, part, '+')) {
                                part.erase(0, part.find_first_not_of(" \t\n\r"));
                                part.erase(part.find_last_not_of(" \t\n\r") + 1);
                                if (part.length() >= 2 && part.front() == '"' && part.back() == '"') {
                                    part = part.substr(1, part.length() - 2);
                                }
                                if (!part.empty()) instr.args.push_back(part);
                            }
                        }
                        else {
                            std::stringstream arg_stream(rest_of_line);
                            std::string arg;
                            while (arg_stream >> arg) {
                                instr.args.push_back(arg);
                            }
                        }

                        new_proc->instructions.push_back(instr);
                    }

                    if (new_proc->instructions.empty() || new_proc->instructions.size() > 50) {
                        std::cout << "Invalid command: Must have between 1 and 50 instructions.\n";
                        delete new_proc;
                        continue;
                    }

                    if (!global_mem_manager->createProcess(*new_proc)) {
                        std::cout << "Memory allocation failed for process '" << arg2 << "'.\n";
                        delete new_proc;
                        continue; // Changed to continue for better user flow
                    }

                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        process_list.push_back(new_proc);
                        ready_queue.push(new_proc);
                    }
                    queue_cv.notify_one();
                    std::cout << "Process '" << unique_name << "' created with instructions.\n";

                }
                catch (const std::invalid_argument& e) {
                    std::cout << "Invalid memory size format for '" << arg3 << "'. Please provide a number.\n";
                }
                catch (...) {
                    std::cout << "Invalid arguments for screen -c. Usage: screen -c <name> <size> \"<instructions>\"\n";
                }
            }
            else if (arg1 == "-r" && !arg2.empty()) {
                Process* target_process = nullptr;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    for (auto& p : process_list) {
                        if (p->name == arg2) { 
                            target_process = p;
                            break;
                        }
                    }
                }

                if (target_process) {
                    // Process was found. Check if it crashed.
                    if (target_process->state == ProcessState::CRASHED) {
                        std::cout << "Process <" << target_process->name
                            << "> shut down due to memory access violation error that occurred at "
                            << target_process->end_time << ". ";

                        if (target_process->faulting_address.has_value()) {
                            std::stringstream hex_stream;
                            hex_stream << "0x" << std::hex << target_process->faulting_address.value();
                            std::cout << hex_stream.str() << " invalid.\n";
                        }
                        else {
                            std::cout << "Invalid memory address.\n";
                        }
                    }
                    else {
                        enter_process_screen(arg2);
                    }
                }
                else {
                    std::cout << "Process <" << arg2 << "> not found.\n";
                }
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