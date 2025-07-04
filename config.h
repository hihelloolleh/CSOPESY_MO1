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

extern const int DEFAULT_NUM_CPU;
extern const int DEFAULT_QUANTUM_CYCLES;
extern const int DEFAULT_BATCH_PROCESS_FREQ;
extern const int DEFAULT_MIN_INS;
extern const int DEFAULT_MAX_INS;
extern const int DEFAULT_DELAY_PER_EXEC;
extern const char* const DEFAULT_SCHEDULER;


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
bool correctAndValidateConfig(Config& config);

#endif // CONFIG_H