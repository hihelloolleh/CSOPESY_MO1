// display.cpp

#include "display.h"
#include "shared_globals.h"
#include <iostream>
#include <iomanip> // For std::setw and std::left
#include <cstdlib> // For system()

// Utility function to clear the console screen
void clear_console() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// Prints the main application header
void print_header() {
    std::cout << R"(
  _____  _____  ___________ _____ _______   __
 /  __ \/  ___||  _  | ___ \  ___/  ___\ \ / /
 | /  \/\ `--. | | | | |_/ / |__ \ `--. \ V / 
 | |     `--. \| | | |  __/|  __| `--. \ \ /  
 | \__/\/\__/ /\ \_/ / |   | |___/\__/ / | |  
  \____/\____/  \___/\_|   \____/\____/  \_/  
    )" << std::endl;
    std::cout << "-------------------------------------------------\n";
}

// Generates and prints the system report for 'screen -ls' and 'report-util'
void generate_system_report(std::ostream& output_stream) {
    std::lock_guard<std::mutex> lock(queue_mutex);

    int running_count = 0;
    for (const auto& p : process_list) {
        if (!p->finished) {
            running_count++;
        }
    }
    
    int cores_used = running_count;
    int cores_available = global_config.num_cpu - cores_used;
    int cpu_utilization = (global_config.num_cpu > 0) ? (static_cast<int>((static_cast<double>(cores_used) / global_config.num_cpu) * 100.0)) : 0;

    output_stream << "CPU utilization: " << cpu_utilization << "%\n";
    output_stream << "Cores used: " << cores_used << "\n";
    output_stream << "Cores available: " << cores_available << "\n\n";
    output_stream << "---------------------------------------------------------\n";

    output_stream << "Running processes:\n";
    for (const auto& p : process_list) {
        if (!p->finished) {
            output_stream << std::left << std::setw(12) << p->name
                          << std::setw(25) << p->start_time
                          << "Core: " << std::left << std::setw(5) << p->assigned_core
                          << p->program_counter << " / " << p->instructions.size() << "\n";
        }
    }
    output_stream << "\n";

    output_stream << "Finished processes:\n";
    for (const auto& p : process_list) {
        if (p->finished) {
            output_stream << std::left << std::setw(12) << p->name
                          << std::setw(25) << p->end_time
                          << std::left << std::setw(10) << "Finished"
                          << p->instructions.size() << " / " << p->instructions.size() << "\n";
        }
    }
    output_stream << "---------------------------------------------------------\n";
}

// Displays the detailed view for a single process for 'screen -s'
void display_process_view(Process* process) {
    std::lock_guard<std::mutex> lock(queue_mutex);

    clear_console();

    // --- Header Information ---
    std::cout << std::left << std::setw(28) << "Process name:" << process->name << "\n";
    std::cout << std::left << std::setw(28) << "ID:" << process->id << "\n";
    
    // --- Logs Section ---
    std::cout << "\nLogs:\n";
    if (process->logs.empty()) {
        std::cout << "(No output generated yet)\n";
    } else {
        for(const auto& log_line : process->logs) {
            std::cout << log_line << "\n";
        }
    }

    // --- Status Section ---
    std::cout << "\n";
    std::cout << std::left << std::setw(28) << "Current instruction line:" << process->program_counter << "\n";
    std::cout << std::left << std::setw(28) << "Lines of code:" << process->instructions.size() << "\n\n";

    // --- Finished Message ---
    if (process->finished) {
        std::cout << "Finished!\n\n";
    }
}