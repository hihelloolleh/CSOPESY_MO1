#include "shared_globals.h"
#include "config.h"
#include "cpu_core.h"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>

// Function to start all CPU core worker threads
void start_cpu_cores(std::vector<std::thread>& workers) {
    for (int i = 0; i < global_config.num_cpu; ++i) {
        workers.emplace_back(cpu_core_worker, i);
    }
    std::cout << global_config.num_cpu << " CPU cores started." << std::endl;
}

// A simple test function to add a process to the queue
void add_test_process() {
    Process* p = new Process();
    p->id = process_list.size() + 1;
    p->name = "test_proc_" + std::to_string(p->id);
    
    // In the future, this is where you'd generate random instructions
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        process_list.push_back(p);
        ready_queue.push(p);
    }
    queue_cv.notify_one(); // Wake up one sleeping core
    std::cout << "Added " << p->name << " to the ready queue." << std::endl;
}

void print_header() {
    // Basic header, can be replaced with ASCII art later
    std::cout << "\n--- CSOPESY Process Scheduler and CLI ---\n";
}

void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
    print_header();
}

void cli_loop() {
    std::string line;
    print_header();

    while (system_running) {
        std::cout << "root:\\> ";
        if (!std::getline(std::cin, line)) {
            break; // Exit on Ctrl+D or stream error
        }

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command.empty()) {
            continue;
        }

        if (command == "exit") {
            system_running = false;
            queue_cv.notify_all(); // Wake up all threads so they can terminate
            break;
        }

        if (command == "clear") {
            clear_screen();
            continue;
        }

        // --- Commands requiring initialization ---
        if (!is_initialized) {
            if (command == "initialize") {
                if (loadConfiguration("config.txt", global_config)) {
                    is_initialized = true;
                    std::cout << "System initialized successfully." << std::endl;
                    // Automatically start the CPU cores after initialization
                    std::vector<std::thread> workers;
                    start_cpu_cores(workers);
                    // In a real app, you'd manage these threads better,
                    // but for now, we'll detach them to let them run in the background.
                    for (auto& t : workers) {
                        t.detach();
                    }
                } else {
                    std::cerr << "Initialization failed. Please check config.txt." << std::endl;
                }
            } else {
                std::cerr << "Error: System not initialized. Please run 'initialize' first." << std::endl;
            }
            continue;
        }
        
        // --- Place other commands here ---
        if (command == "test-add") {
            add_test_process();
        } else {
            std::cout << "Unknown command: '" << command << "'" << std::endl;
        }
    }
}


int main() {
    cli_loop();

    std::cout << "Shutting down... please wait." << std::endl;
    
    // Cleanup allocated memory
    for (auto p : process_list) {
        delete p;
    }
    process_list.clear();

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
