#include "display.h"
#include "shared_globals.h"
#include <vector>
#include <iomanip>
#include <cstdlib>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <conio.h>
#endif

void clear_console() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void generate_system_report(std::ostream& output_stream) {
    std::lock_guard<std::mutex> lock(queue_mutex);

    int running_count = 0;
    for (const auto& p : process_list) {
        if (p->assigned_core != -1 && !p->finished) {
            running_count++;
        }
    }
    
    int cores_used = running_count;
    int cores_available = global_config.num_cpu - cores_used;
    int cpu_utilization = (global_config.num_cpu > 0) ? (cores_used * 100 / global_config.num_cpu) : 0;

    output_stream << "CPU utilization: " << cpu_utilization << "%\n";
    output_stream << "Cores used: " << cores_used << "\n";
    output_stream << "Cores available: " << cores_available << "\n\n";
    output_stream << "---------------------------------------------------------\n";

    output_stream << "Running processes:\n";
    for (const auto& p : process_list) {
        if (p->assigned_core != -1 && !p->finished) {
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


