#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <fstream>  

enum class SchedulerType {
    FCFS,
    SJF,
    SRTF,
    PRIORITY_NONPREEMPTIVE,
    PRIORITY_PREEMPTIVE,
    RR,
    UNKNOWN
};



struct Config {
    int num_cpu = 0;
    std::string scheduler;
    SchedulerType scheduler_type = SchedulerType::UNKNOWN;
    int quantum_cycles = 0;
    int batch_process_freq = 0;
    int min_ins = 0;
    int max_ins = 0;
    int delay_per_exec = 0;
};

bool loadConfiguration(const std::string& filepath, Config& config);

#endif // CONFIG_H