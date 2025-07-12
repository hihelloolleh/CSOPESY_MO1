#include "shared_globals.h"
#include <chrono>
#include <iomanip>
#include <sstream>


// --- System Clock Definition ---
std::atomic<uint64_t> cpu_ticks(0);

// --- Process Generation Definition ---
std::atomic<bool> generating_processes(false); // Starts in the "off" state

// --- Global State Definitions ---
std::atomic<bool> system_running(true);
std::atomic<bool> is_initialized(false);

// --- Configuration Definition ---
Config global_config;

// --- Memory Management Definitions ---
MemoryManager* global_mem_manager = nullptr;

// --- Process Management Definitions ---
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::queue<Process*> ready_queue;
std::vector<Process*> process_list;
std::vector<bool> core_busy;

// --- Utility Function Definitions ---
// Extracted from fcfs-2.cpp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    time_t time = std::chrono::system_clock::to_time_t(now);
    tm local_tm;
    #if defined(_WIN32) || defined(_WIN64)
        localtime_s(&local_tm, &time); // Windows
    #else
        localtime_r(&time, &local_tm); // POSIX (macOS/Linux)
    #endif

    std::stringstream ss;
    ss << std::put_time(&local_tm, "(%m/%d/%Y %I:%M:%S%p)");
    return ss.str();
}

std::atomic<int> global_quantum_cycle = 0; //Quantum Cycle