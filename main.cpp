// g++ main.cpp config.cpp shared_globals.cpp cpu_core.cpp scheduler.cpp -o csopesy_emu.exe -std=c++17 -lpthread
// csopesy_emu.exe

#include "shared_globals.h"
#include "config.h"
#include "cpu_core.h"
#include "scheduler.h" 
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <cstdlib> // For srand
#include <ctime>   // For time

// A vector to hold our worker threads so we can manage them
std::vector<std::thread> cpu_worker_threads;

// Function to start all CPU core worker threads
void start_cpu_cores() {
    // Clear any old threads first, just in case
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

void clear_screen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
    print_header();
}

// The main loop for handling user commands from the command-line interface.
void cli_loop() {
    std::string line;
    clear_screen(); // Start with a clean screen

    while (system_running) {
        std::cout << "root:\\> ";
        if (!std::getline(std::cin, line)) {
            // This handles Ctrl+D (on Linux) or end of input stream
            if (std::cin.eof()) {
                std::cout << "EOF detected, exiting." << std::endl;
                system_running = false;
                queue_cv.notify_all();
            }
            break; 
        }

        std::stringstream ss(line);
        std::string command;
        ss >> command;

        if (command.empty()) {
            continue;
        }

        // --- Handle commands that can be run anytime ---
        if (command == "exit") {
            system_running = false;
            queue_cv.notify_all(); // Wake all threads so they can check the running flag and exit
            break;
        }

        if (command == "clear") {
            clear_screen();
            continue;
        }

        // --- Gatekeeper: Check if the system is initialized ---
        if (!is_initialized) {
            if (command == "initialize") {
                if (loadConfiguration("config.txt", global_config)) {
                    is_initialized = true;
                    std::cout << "System initialized successfully from config.txt." << std::endl;
                    start_cpu_cores(); // Start the CPU cores now that we know how many we need
                } else {
                    std::cerr << "Initialization FAILED. Please check config.txt and try again." << std::endl;
                }
            } else {
                std::cerr << "Error: System not initialized. Please run 'initialize' first." << std::endl;
            }
            continue; // Go back to the start of the loop
        }
        
        // --- Commands that require initialization ---
        if (command == "scheduler-start") {
            if (!generating_processes) {
                generating_processes = true; // This is the 'on' switch for the generator thread
                std::cout << "Process generator has been started." << std::endl;
            } else {
                std::cout << "Generator is already running." << std::endl;
            }
        } else if (command == "scheduler-stop") {
            if (generating_processes) {
                generating_processes = false; // This is the 'off' switch
                std::cout << "Process generator has been stopped." << std::endl;
            } else {
                std::cout << "Generator is not currently running." << std::endl;
            }
        } else if (command == "initialize") {
             std::cout << "System is already initialized." << std::endl;
        }
        else {
            std::cout << "Unknown command: '" << command << "'" << std::endl;
        }
    }
}


int main() {
    // Seed the random number generator ONCE at the very start of the program
    srand(static_cast<unsigned int>(time(nullptr))); 

    // --- Start Background System Threads ---
    // These threads will run for the entire lifetime of the application.
    std::thread clock(clock_thread);
    std::thread generator(process_generator_thread);
    
    // The main CLI loop will run on the main thread, blocking until it's time to exit.
    cli_loop();

    std::cout << "\nShutdown initiated. Waiting for threads to complete..." << std::endl;
    
    // --- Graceful Shutdown Sequence ---
    
    // 1. Wait for the background system threads to finish.
    if (clock.joinable()) clock.join();
    if (generator.joinable()) generator.join();
    
    // 2. Wait for the CPU worker threads to finish.
    for (auto& t : cpu_worker_threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // 3. Cleanup dynamically allocated memory for all created processes.
    std::cout << "Cleaning up " << process_list.size() << " process records..." << std::endl;
    for (auto p : process_list) {
        delete p;
    }
    process_list.clear();

    std::cout << "Shutdown complete. Goodbye!" << std::endl;
    return 0;
}