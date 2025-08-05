#include "config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm> // For std::swap

// --- Existing Defaults ---
const int DEFAULT_NUM_CPU = 4;
const int DEFAULT_QUANTUM_CYCLES = 5;
const int DEFAULT_BATCH_PROCESS_FREQ = 1;
const int DEFAULT_MIN_INS = 1000;
const int DEFAULT_MAX_INS = 2000;
const int DEFAULT_DELAY_PER_EXEC = 0;
const char* const DEFAULT_SCHEDULER = "rr";

// --- NEW DEFAULTS FOR MEMORY ---
const int DEFAULT_MAX_OVERALL_MEM = 16384; // 2^14
const int DEFAULT_MEM_PER_FRAME = 256;     // 2^8
const int DEFAULT_MIN_MEM_PER_PROC = 1024; // 2^10
const int DEFAULT_MAX_MEM_PER_PROC = 4096; // 2^12

// --- Helper function for validation ---
bool isPowerOfTwo(int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

bool loadConfiguration(const std::string& filepath, Config& config) {
    std::ifstream configFile(filepath);
    if (!configFile.is_open()) {
        std::cerr << "Error: Could not open configuration file at " << filepath << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(configFile, line)) {
        std::stringstream ss(line);
        std::string key;
        ss >> key;

        if (key == "num-cpu") ss >> config.num_cpu;
        else if (key == "scheduler") {
            std::string value;
            ss >> value;
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            config.scheduler = value;

            if (value == "fcfs") config.scheduler_type = SchedulerType::FCFS;
            else if (value == "sjf") config.scheduler_type = SchedulerType::SJF;
            else if (value == "srtf") config.scheduler_type = SchedulerType::SRTF;
            else if (value == "priority_np") config.scheduler_type = SchedulerType::PRIORITY_NONPREEMPTIVE;
            else if (value == "priority_p") config.scheduler_type = SchedulerType::PRIORITY_PREEMPTIVE;
            else if (value == "rr") config.scheduler_type = SchedulerType::RR;
            else config.scheduler_type = SchedulerType::UNKNOWN;
        }
        else if (key == "quantum-cycles") ss >> config.quantum_cycles;
        else if (key == "batch-process-freq") ss >> config.batch_process_freq;
        else if (key == "min-ins") ss >> config.min_ins;
        else if (key == "max-ins") ss >> config.max_ins;
        else if (key == "delay-per-exec") ss >> config.delay_per_exec;
        else if (key == "max-overall-mem") ss >> config.max_overall_mem;
        else if (key == "mem-per-frame") ss >> config.mem_per_frame;
        else if (key == "min-mem-per-proc") ss >> config.min_mem_per_proc;
        else if (key == "max-mem-per-proc") ss >> config.max_mem_per_proc;
    }

    configFile.close();

    correctAndValidateConfig(config);

    return true;
}

bool correctAndValidateConfig(Config& config) {
    bool corrected = false;

    if (config.num_cpu < 1 || config.num_cpu > 128) {
        std::cerr << "Correcting num-cpu from " << config.num_cpu << " to " << DEFAULT_NUM_CPU << "\n";
        config.num_cpu = DEFAULT_NUM_CPU;
        corrected = true;
    }
    if (config.scheduler_type == SchedulerType::UNKNOWN) {
        std::cerr << "Invalid scheduler type. Defaulting to " << DEFAULT_SCHEDULER << ".\n";
        config.scheduler = DEFAULT_SCHEDULER;
        if (config.scheduler == "fcfs") config.scheduler_type = SchedulerType::FCFS;
        else if (config.scheduler == "rr") config.scheduler_type = SchedulerType::RR;
        else config.scheduler_type = SchedulerType::UNKNOWN;
        corrected = true;
    }
    if (config.scheduler_type == SchedulerType::RR && config.quantum_cycles < 1) {
        std::cerr << "Correcting quantum-cycles to " << DEFAULT_QUANTUM_CYCLES << "\n";
        config.quantum_cycles = DEFAULT_QUANTUM_CYCLES;
        corrected = true;
    }
    if (config.batch_process_freq < 1) {
        std::cerr << "Correcting batch-process-freq to " << DEFAULT_BATCH_PROCESS_FREQ << "\n";
        config.batch_process_freq = DEFAULT_BATCH_PROCESS_FREQ;
        corrected = true;
    }
    if (config.min_ins < 1) {
        std::cerr << "Correcting min-ins to " << DEFAULT_MIN_INS << "\n";
        config.min_ins = DEFAULT_MIN_INS;
        corrected = true;
    }
    if (config.max_ins < 1) {
        std::cerr << "Correcting max-ins to " << DEFAULT_MAX_INS << "\n";
        config.max_ins = DEFAULT_MAX_INS;
        corrected = true;
    }
    if (config.min_ins > config.max_ins) {
        std::cerr << "Swapping min-ins and max-ins (" << config.min_ins << " > " << config.max_ins << ")\n";
        std::swap(config.min_ins, config.max_ins);
        corrected = true;
    }
    if (config.delay_per_exec < 0) {
        std::cerr << "Correcting delay-per-exec to " << DEFAULT_DELAY_PER_EXEC << "\n";
        config.delay_per_exec = DEFAULT_DELAY_PER_EXEC;
        corrected = true;
    }
    const int MIN_MEM_VALUE = 64;
    const int MAX_MEM_VALUE = 65536;
    if (!isPowerOfTwo(config.max_overall_mem) || config.max_overall_mem < MIN_MEM_VALUE || config.max_overall_mem > MAX_MEM_VALUE) {
        std::cerr << "Correcting max-overall-mem from " << config.max_overall_mem << " to default " << DEFAULT_MAX_OVERALL_MEM << " (must be power of 2, 64 <= n <= 65536)\n";
        config.max_overall_mem = DEFAULT_MAX_OVERALL_MEM;
        corrected = true;
    }
    if (!isPowerOfTwo(config.mem_per_frame)) {
        std::cerr << "Correcting mem-per-frame from " << config.mem_per_frame << " to default " << DEFAULT_MEM_PER_FRAME << " (must be power of 2, 64 <= n <= 65536)\n";
        config.mem_per_frame = DEFAULT_MEM_PER_FRAME;
        corrected = true;
    }
    if (config.min_mem_per_proc <= 0) {
        config.min_mem_per_proc = DEFAULT_MIN_MEM_PER_PROC;
        corrected = true;
    }
    if (config.max_mem_per_proc <= 0) {
        config.max_mem_per_proc = DEFAULT_MAX_MEM_PER_PROC;
        corrected = true;
    }
    if (config.min_mem_per_proc > config.max_mem_per_proc) {
        std::cerr << "Swapping min-mem-per-proc and max-mem-per-proc (" << config.min_mem_per_proc << " > " << config.max_mem_per_proc << ")\n";
        std::swap(config.min_mem_per_proc, config.max_mem_per_proc);
        corrected = true;
    }

    return corrected;
}