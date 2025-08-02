#ifndef SHARED_GLOBALS_H
#define SHARED_GLOBALS_H

#include "config.h"
#include "process.h"
// --- FORWARD DECLARE MEMORYMANAGER TO AVOID CIRCULAR DEPENDENCY ---
class MemoryManager;
// ---
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <vector>
#include <cstdint>

// --- System Clock ---
extern std::atomic<uint64_t> cpu_ticks;

// --- Process Generation ---
extern std::atomic<bool> generating_processes;

// --- Global State ---
extern std::atomic<bool> system_running;
extern std::atomic<bool> is_initialized;

// --- Configuration ---
extern Config global_config;

// --- Memory Manager ---
extern MemoryManager* global_mem_manager;

// --- Process Management ---
extern std::mutex queue_mutex; 
extern std::condition_variable queue_cv;
extern std::queue<Process*> ready_queue;
extern std::vector<Process*> process_list;
extern std::queue<Process*> pending_memory_queue;
extern std::vector<bool> core_busy;

// --- Utility ---
std::string get_timestamp();
extern std::atomic<int> global_quantum_cycle;

#endif // SHARED_GLOBALS_H