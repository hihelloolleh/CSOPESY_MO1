#include "config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

const int DEFAULT_NUM_CPU = 4;
const int DEFAULT_QUANTUM_CYCLES = 5;
const int DEFAULT_BATCH_PROCESS_FREQ = 1;
const int DEFAULT_MIN_INS = 1000;
const int DEFAULT_MAX_INS = 2000;
const int DEFAULT_DELAY_PER_EXEC = 0;
const char* const DEFAULT_SCHEDULER = "rr";

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
    }

    configFile.close();

    correctAndValidateConfig(config);

    return true;
}

bool correctAndValidateConfig(Config& config) {
    bool corrected = false;

    if (config.num_cpu < 1) {
        std::cerr << "Correcting num-cpu from " << config.num_cpu << " to " << DEFAULT_NUM_CPU << "\n";
        config.num_cpu = DEFAULT_NUM_CPU;
        corrected = true;
    }
    else if (config.num_cpu > 128) {
        std::cerr << "Correcting num-cpu from " << config.num_cpu << " to 128\n";
        config.num_cpu = 128;
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

    return corrected;
}
