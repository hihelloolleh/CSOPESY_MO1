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

// --- Project-Specific Headers ---
#include "shared_globals.h"
#include "config.h"
#include "cpu_core.h"
#include "scheduler.h"
#include "display.h"

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
        std::string command, arg1, arg2;
        ss >> command >> arg1 >> arg2;

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
                    is_initialized = true;
                    std::cout << "System initialized successfully from config.txt." << std::endl;
                    start_cpu_cores();
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
            else if (arg1 == "-s" && !arg2.empty()) {
                std::string process_name = arg2;
                Process* target_process = nullptr;
                
                // First check if process already exists
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    for (auto& p : process_list) {
                        if (p->name == process_name) {
                            target_process = p;
                            break;
                        }
                    }
                }

                // If process doesn't exist, create it
                if (target_process == nullptr) {
                    target_process = create_random_process();
                    target_process->name = process_name;
                    
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        process_list.push_back(target_process);
                        ready_queue.push(target_process);
                    }
                    queue_cv.notify_one();
                }

                // Enter process view
                bool in_process_view = true;
                while (in_process_view) {
                    display_process_view(target_process);
                    
                    if (target_process->finished) {
                        std::cout << "root:\\> ";
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        in_process_view = false;
                        continue;
                    }
                    
                    std::cout << "root:\\> ";
                    std::string process_command;
                    std::getline(std::cin, process_command);

                    if (process_command == "exit") {
                        in_process_view = false;
                    } else if (process_command == "process-smi") {
                        continue;
                    }
                }
                clear_console();
                print_header();
            }
            else if (arg1 == "-r" && !arg2.empty()) {
                std::string process_name = arg2;
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

                if (target_process != nullptr) {
                    bool in_process_view = true;
                    while (in_process_view) {
                        display_process_view(target_process);
                        
                        if (target_process->finished) {
                            std::cout << "root:\\> ";
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            in_process_view = false;
                            continue;
                        }
                        
                        std::cout << "root:\\> ";
                        std::string process_command;
                        std::getline(std::cin, process_command);

                        if (process_command == "exit") {
                            in_process_view = false;
                        } else if (process_command == "process-smi") {
                            continue;
                        }
                    }
                    clear_console();
                    print_header();
                } else {
                    std::cout << "Process <" << process_name << "> not found.\n";
                }
            } else {
                 std::cout << "Invalid screen command. Use 'screen -ls', 'screen -s <name>', or 'screen -r <name>'.\n";
            }
        }
        else {
            std::cout << "Unknown command: '" << line << "'" << std::endl;
        }
    }
}


int main() {
    srand(static_cast<unsigned int>(time(nullptr))); 
    std::thread generator_thread(process_generator_thread);
    std::thread master_clock_thread(clock_thread);

    cli_loop();

    std::cout << "\nShutdown initiated. Waiting for background threads to complete..." << std::endl;
    
    if (master_clock_thread.joinable()) master_clock_thread.join();
    if (generator_thread.joinable()) generator_thread.join();
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
    
    std::cout << "Shutdown complete. Goodbye!" << std::endl;
    return 0;
}