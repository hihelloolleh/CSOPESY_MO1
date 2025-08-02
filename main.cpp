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
extern std::atomic<int> g_next_pid; // Make sure g_next_pid from scheduler.cpp is accessible

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

    // Check if process already exists
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (auto& p : process_list) {
            if (p->name == process_name) {
                target_process = p;
                break;
            }
        }
    }

    // Create new process if allowed and it doesn't exist
    if (!target_process && allow_create) {
        target_process = create_random_process(process_name, memory_size); // Pass memory size
        
        // Attempt to register with memory manager
        if (!global_mem_manager->createProcess(*target_process)) {
            std::cout << "Failed to create process <" << process_name << ">. Not enough memory or process ID conflict.\n";
            delete target_process; // Clean up memory if registration fails.
            return;
        }

        // Add to global lists and ready queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            process_list.push_back(target_process);
            ready_queue.push(target_process);
        }
        queue_cv.notify_one();
        std::cout << "Process <" << process_name << "> created with " << memory_size << " bytes of memory.\n";
    }

    if (!target_process) {
        std::cout << "Process <" << process_name << "> not found.\n";
        return;
    }

    // Enter the interactive view for the process
    display_process_view(target_process);
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
                    // --- INSTANTIATE THE MEMORY MANAGER ---
                    global_mem_manager = new MemoryManager(global_config);
                    // ---
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
                    } else {
                        std::cout << "Invalid memory allocation. Must be a power of 2 between 64 and 65536.\n";
                    }
                } catch (...) {
                    std::cout << "Invalid memory size format.\n";
                }
            } else if (arg1 == "-r" && !arg2.empty()) {
                enter_process_screen(arg2, false);
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