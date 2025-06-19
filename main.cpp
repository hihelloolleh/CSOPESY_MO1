#include "shared_globals.h"
#include "config.h"
#include "cpu_core.h"
#include "scheduler.h"
#include "display.h" // <-- Includes the separate display library
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <fstream>

// A vector to hold our worker threads so we can manage them
std::vector<std::thread> cpu_worker_threads;

// Function to start all CPU core worker threads
void start_cpu_cores() {
    cpu_worker_threads.clear();
    for (int i = 0; i < global_config.num_cpu; ++i) {
        cpu_worker_threads.emplace_back(cpu_core_worker, i);
    }
    std::cout << global_config.num_cpu << " CPU cores started and running in the background." << std::endl;
}

void print_header() {
    std::cout << R"(
-------------------------------------------------                                                                        
  _____  _____  ___________ _____ _______   __
/  __ \/  ___||  _  | ___ \  ___/  ___\ \ / /
| /  \/\ `--. | | | | |_/ / |__ \ `--. \ V / 
| |     `--. \| | | |  __/|  __| `--. \ \ /  
| \__/\/\__/ /\ \_/ / |   | |___/\__/ / | |  
 \____/\____/  \___/\_|   \____/\____/  \_/ 

-------------------------------------------------     
    )" << std::endl;
    std::cout << "Type 'initialize' to begin or 'exit' to quit.\n\n";
}

// The main loop for handling user commands from the command-line interface.
void cli_loop() {
    std::string line;
    clear_console(); // Use the function from display.h/cpp
    print_header();

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
        std::string command, arg1;
        ss >> command >> arg1;

        if (command.empty()) continue;

        if (command == "exit") {
            system_running = false;
            queue_cv.notify_all();
            break;
        }
        if (command == "clear") {
            clear_console(); // Use the function from display.h/cpp
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
        
        if (command == "screen" && arg1 == "-ls") {
            generate_system_report(std::cout); 
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
        else if (command == "initialize") {
             std::cout << "System is already initialized." << std::endl;
        }
        else {
            std::cout << "Unknown command: '" << line << "'" << std::endl;
        }
    }
}


int main() {
    srand(static_cast<unsigned int>(time(nullptr))); 
    std::thread generator(process_generator_thread);
    cli_loop();
    std::cout << "\nShutdown initiated. Waiting for threads to complete..." << std::endl;
    if (generator.joinable()) generator.join();
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