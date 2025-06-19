#include "shared_globals.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

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

// --- Cross-platform Local Time Function ---
tm get_localtime(time_t time) {
    tm result;

#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&result, &time); // Windows safe version
#else
    localtime_r(&time, &result); // POSIX (Linux/macOS) safe version
#endif

    return result;
}

// --- Utility Function Definitions ---
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    time_t time = std::chrono::system_clock::to_time_t(now);
    tm local_tm = get_localtime(time);

    std::stringstream ss;
    ss << std::put_time(&local_tm, "(%m/%d/%Y %I:%M:%S%p)");
    return ss.str();
}
