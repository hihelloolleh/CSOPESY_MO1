// display.cpp

#include "display.h"
#include "shared_globals.h"
#include "mem_manager.h"
#include <iostream>
#include <iomanip>     
#include <mutex>       
#include <vector>      
#include <algorithm>
#include <tuple>


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

    // Count actually busy cores
    int cores_used = 0;
    for (size_t i = 0; i < core_busy.size(); ++i) {
        if (core_busy[i]) {
            cores_used++;
        }
    }

    int cores_available = global_config.num_cpu - cores_used;
    int cpu_utilization = (global_config.num_cpu > 0)
        ? static_cast<int>((static_cast<double>(cores_used) / global_config.num_cpu) * 100.0)
        : 0;

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

    // Collect and sort finished processes by end_time
    std::vector<Process*> finished;
    for (const auto& p : process_list) {
        if (p->finished) finished.push_back(p);
    }

    std::sort(finished.begin(), finished.end(), [](Process* a, Process* b) {
        return a->end_time < b->end_time;
        });

    for (const auto& p : finished) {
        output_stream << std::left << std::setw(12) << p->name
            << std::setw(25) << p->end_time
            << "Core: " << std::setw(5) << p->last_core;
            if (p->state == ProcessState::CRASHED) {
                output_stream << std::left << std::setw(10) << "Crashed";
            }
            else {
                output_stream << std::left << std::setw(10) << "Finished";
            }
            output_stream << std::setw(14) << (std::to_string(p->program_counter) + " / " + std::to_string(p->instructions.size()))
                << " Priority: " << p->priority << "\n";
    }

    output_stream << "---------------------------------------------------------\n";
}


// Displays the detailed view for a single process for 'screen -s'
void display_process_view(Process* process) {
    std::lock_guard<std::mutex> lock(queue_mutex);

    // --- Header Information ---
    std::cout << std::left << std::setw(28) << "Process name:" << process->name << "\n";
    std::cout << std::left << std::setw(28) << "ID:" << process->id << "\n";
    std::cout << std::left << std::setw(28) << "Memory (bytes):"
		<< process->memory_required << "\n";
    
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
    if (process->finished && process->state == ProcessState::FINISHED) {
        std::cout << "Finished!\n\n";
    }
    else if (process->state == ProcessState::CRASHED) {
        std::cout << "CRASHED! ";
        if (process->faulting_address.has_value()) {
            std::cout << "Memory access violation near address "
                << process->faulting_address.value() << ".";
        }
        std::cout << "\n\n";
    }
}

void show_global_process_smi() {
    std::lock_guard<std::mutex> lock(queue_mutex);

    std::cout << "--------------------------------------------\n";
    std::cout << "| PROCESS-SMI V01.00 Driver Version: 01.00 |\n";
    std::cout << "--------------------------------------------\n";

    int cores_used = 0;
    if (!core_busy.empty()) {
        for (bool is_busy : core_busy) {
            if (is_busy) {
                cores_used++;
            }
        }
    }

    double cpu_util_percent = (global_config.num_cpu > 0)
        ? (static_cast<double>(cores_used) / global_config.num_cpu) * 100.0
        : 0.0;

    std::cout << std::left << std::setw(16) << "CPU-Util:"
        << std::fixed << std::setprecision(2) << cpu_util_percent << "%\n";

    if (global_mem_manager) {
        size_t used_bytes, total_bytes;
        std::tie(used_bytes, total_bytes) = global_mem_manager->getMemoryUsageStats();

        double mem_util_percent = (total_bytes > 0)
            ? (static_cast<double>(used_bytes) / total_bytes) * 100.0
            : 0.0;

        std::cout << std::left << std::setw(16) << "Memory Usage:"
            << used_bytes << " B / " << total_bytes << " B\n";

        std::cout << std::left << std::setw(16) << "Memory Util:"
            << std::fixed << std::setprecision(2) << mem_util_percent << "%\n";
    }
    else {
        std::cout << std::left << std::setw(16) << "Memory Usage:" << "N/A\n";
        std::cout << std::left << std::setw(16) << "Memory Util:" << "N/A\n";
    }
    std::cout << "===================================\n";
    std::cout << "Running processes and memory usage:\n";
    std::cout << "-----------------------------------\n";

    // --- FIX #2: Display per-process memory in Bytes ---
    bool found_running_process = false;
    for (const auto& p : process_list) {
        if (!p->finished) {
            found_running_process = true;
            // No conversion needed. p->memory_required is already in bytes.
            size_t mem_in_bytes = p->memory_required;

            // Print in the format: [process_name] [memory in Bytes]
            std::cout << std::left << std::setw(20) << p->name
                << mem_in_bytes << " B\n";
        }
    }

    if (!found_running_process) {
        std::cout << "(No running processes)\n";
    }
    std::cout << "-----------------------------------\n";
}

void show_vmstat() {
    size_t used, total;
    std::tie(used, total) = global_mem_manager->getMemoryUsageStats();

    uint64_t total_ticks = cpu_ticks.load();
    uint64_t idle_ticks = 0;
    uint64_t active_ticks = 0;

    for (bool busy : core_busy) {
        if (busy) active_ticks++;
        else idle_ticks++;
    }

    std::cout << "\n=== vmstat ===\n\n";
    std::cout << std::left << std::setw(25) << "Total memory:" << total << " bytes\n";
    std::cout << std::left << std::setw(25) << "Used memory:" << used << " bytes\n";
    std::cout << std::left << std::setw(25) << "Free memory:" << (total - used) << " bytes\n";
    std::cout << std::left << std::setw(25) << "Idle CPU ticks:" << idle_ticks << "\n";
    std::cout << std::left << std::setw(25) << "Active CPU ticks:" << active_ticks << "\n";
    std::cout << std::left << std::setw(25) << "Total CPU ticks:" << total_ticks << "\n";
    std::cout << std::left << std::setw(25) << "Pages paged in:" << global_mem_manager->getPageInCount() << "\n";
    std::cout << std::left << std::setw(25) << "Pages paged out:" << global_mem_manager->getPageOutCount() << "\n\n";
}
