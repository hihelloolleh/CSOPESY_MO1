// main.cpp

// --- C++ Standard Libraries ---
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <chrono> // For std::this_thread::sleep_for
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
   

// A vector to hold our CPU worker threads so we can manage them during shutdown.
std::vector<std::thread> cpu_worker_threads;

// Helper function to start all CPU core worker threads after initialization.
void start_cpu_cores() {
    cpu_worker_threads.clear();
    core_busy.clear();
    core_busy.resize(global_config.num_cpu, false);
    for (int i = 0; i < global_config.num_cpu; ++i) {
        cpu_worker_threads.emplace_back(cpu_core_worker, i); // Each thread is created and runs the cpu_core_worker function with its unique core ID.
    }
    std::cout << global_config.num_cpu << " CPU cores have been started." << std::endl;
}

// helper func, refractored -s -r into one function.
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

	// If process finished cant load
    if (target_process && target_process->finished) {
        std::cout << "Process <" << process_name << "> has finished execution. Opening in read-only view.\n";
        return;
    }

    // Create new process if allowed
    if (!target_process && allow_create) {
        target_process = create_random_process(memory_size);
        target_process->name = process_name;


        if (!global_mem_manager->createProcess(*target_process)) {
            std::cout << "Failed to create process <" << process_name << ">. Not enough memory or process ID conflict.\n";
            delete target_process; // Clean up memory if registration fails.
            return;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            process_list.push_back(target_process);
            ready_queue.push(target_process);
        }
        queue_cv.notify_one();
    }

    if (!target_process) {
        std::cout << "Process <" << process_name << "> not found.\n";
        return;
    }

    clear_console();
    display_process_view(target_process);

    bool in_process_view = true;
    while (in_process_view) {
        std::cout << "root:\\> ";
        std::string process_command;
        std::getline(std::cin, process_command);

        if (process_command == "exit") {
            in_process_view = false;
        }
        else if (process_command == "process-smi") {
            display_process_view(target_process);
        }
        else {
            std::stringstream ss(process_command);
            std::string opcode;
            ss >> opcode;

            if (target_process->finished && process_command != "exit" && process_command != "process-smi") {
                std::cout << "Process has finished execution. Cannot modify instructions. Only 'exit' and 'process-smi' are allowed.\n";
                continue;
            }

            // Check if it's a valid Barebones instruction
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
                for (int i = 0; i < target_process->instructions.size(); ++i) {
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
                std::cout << "Unknown command. Only 'exit', 'process-smi', or a valid Barebones instruction (ADD, SUBTRACT, DECLARE, PRINT, SLEEP, FOR) are allowed.\n";
            }
        }
    }


    clear_console();
    print_header();
}

std::string get_scheduler_name(SchedulerType type) {
    switch (type) {
    case SchedulerType::FCFS: return "First Come First Serve (FCFS)";
    case SchedulerType::SJF: return "Shortest Job First (SJF)";
    case SchedulerType::SRTF: return "Shortest Remaining Time First (SRTF)";
    case SchedulerType::PRIORITY_NONPREEMPTIVE: return "Priority (Non-Preemptive)";
    case SchedulerType::PRIORITY_PREEMPTIVE: return "Priority (Preemptive)";
    case SchedulerType::RR: return "Round Robin (RR)";
    default: return "Unknown";
    }
}

//handles all user input, parses commands, and dispatches them to the appropriate handlers. It runs on the main thread.
void cli_loop() {
    std::string line;
    clear_console();
    print_header();
    std::cout << "\nType 'initialize' to begin or 'exit' to quit.\n\n";

    while (system_running) {
        std::cout << "root:\\> ";
        if (!std::getline(std::cin, line)) {
            if (std::cin.eof()) { // Handle Ctrl+D
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
                    std::cout << "Scheduling Algorithm: " << get_scheduler_name(global_config.scheduler_type) << "\n";
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
        }
        else if (command == "scheduler-start") {
            if (!generating_processes) {
                generating_processes = true;
                std::cout << "Process generator has been started." << std::endl;
            } else {
                std::cout << "Generator is already running." << std::endl;
            }
        } 
        else if (command == "scheduler-stop") {
            if (generating_processes) {
                generating_processes = false;
                std::cout << "Process generator has been stopped." << std::endl;
            } else {
                std::cout << "Generator is not currently running." << std::endl;
            }
        }
        else if (command == "report-util") {
            std::string log_filename = "csopesy-log.txt";
            std::ofstream log_file(log_filename);
            if (log_file.is_open()) {
                generate_system_report(log_file);
                log_file.close();
                std::cout << "Report generated at ./" << log_filename << std::endl;
            } else {
                std::cerr << "Error: Could not open " << log_filename << " for writing." << std::endl;
            }
        }
        else if (command == "screen") {
            if (arg1 == "-ls") {
                generate_system_report(std::cout);
            }
            else if (arg1 == "-s" && !arg2.empty() && !arg3.empty()) {
                size_t mem_size = 0;
                try {
                    unsigned long long val = std::stoull(arg3);
                    if (val > 0 && (val & (val - 1)) == 0) { // Check power of 2
                        mem_size = static_cast<size_t>(val);
                    }
                }
                catch (...) { /* Conversion failed */ }

                if (mem_size >= 64 && mem_size <= 65536) {
                    enter_process_screen(arg2, true, mem_size);
                }
                else {
                    std::cout << "Invalid memory allocation size. Must be a power of 2 between 64 and 65536.\n";
                }
            }
            else if (arg1 == "-r" && !arg2.empty()) {
                enter_process_screen(arg2, false);
            }
            else {
                std::cout << "Invalid screen usage. Try one of:\n";
                std::cout << "  screen -ls\n";
                std::cout << "  screen -s <process name> <process_memory_size>\n";
                std::cout << "  screen -r <process name>\n";
            }
        }
        else {
            std::cout << "Unknown command: '" << line << "'" << std::endl;
        }
    }
}


int main() {
    srand(static_cast<unsigned int>(time(nullptr))); 

    std::thread master_clock_thread(clock_thread);
    // Create 'snapshots' folder if it does not exist
#ifdef _WIN32
    struct _stat st = { 0 };
    if (_stat("snapshots", &st) == -1) {
        _mkdir("snapshots");
    }
#else
    struct stat st = { 0 };
    if (stat("snapshots", &st) == -1) {
        mkdir("snapshots", 0700);
    }
#endif


    cli_loop();

    std::cout << "\nShutdown initiated. Waiting for background threads to complete..." << std::endl;
    
    if (master_clock_thread.joinable()) master_clock_thread.join();

    for (auto& t : cpu_worker_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::cout << "Cleaning up " << process_list.size() << " process records..." << std::endl;
    for (auto p : process_list) {
        delete p;
    }
    process_list.clear();

    if (global_mem_manager) {
        global_mem_manager->flushAsyncWrites();
        delete global_mem_manager;
        global_mem_manager = nullptr;
    }

    
    std::cout << "Shutdown complete. Goodbye!" << std::endl;
    return 0;
}