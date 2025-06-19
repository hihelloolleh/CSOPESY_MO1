#include "shared_globals.h"
#include <chrono>
#include <iomanip>
#include <sstream>

// --- Global State Definitions ---
std::atomic<bool> system_running(true);
std::atomic<bool> is_initialized(false);

// --- Configuration Definition ---
Config global_config;

// --- Process Management Definitions ---
std::mutex queue_mutex;
std::condition_variable queue_cv;
std::queue<Process*> ready_queue;
std::vector<Process*> process_list;

// --- Utility Function Definitions ---
// Extracted from fcfs-2.cpp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    time_t time = std::chrono::system_clock::to_time_t(now);
    tm local_tm;
    localtime_s(&local_tm, &time); // Using safer localtime_s for Windows

    std::stringstream ss;
    ss << std::put_time(&local_tm, "(%m/%d/%Y %I:%M:%S%p)");
    return ss.str();
}