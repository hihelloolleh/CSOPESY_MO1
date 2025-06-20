#ifndef SHARED_GLOBALS_H
#define SHARED_GLOBALS_H

#include "config.h"
#include "process.h"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <vector>
#include <cstdint> // For uint64_t

// --- System Clock ---
extern std::atomic<uint64_t> cpu_ticks;

// --- Process Generation ---
extern std::atomic<bool> generating_processes;

// --- Global State ---
extern std::atomic<bool> system_running;
extern std::atomic<bool> is_initialized;

// --- Configuration ---
extern Config global_config;

// --- Process Management ---
extern std::mutex queue_mutex;
extern std::condition_variable queue_cv;
extern std::queue<Process*> ready_queue;
extern std::vector<Process*> process_list; // Master list of all processes created
extern std::vector<bool> core_busy; // Track which cores are currently busy

// --- Utility ---
std::string get_timestamp();

#endif // SHARED_GLOBALS_H