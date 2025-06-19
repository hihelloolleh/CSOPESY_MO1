#include "config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

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
                config.scheduler = value.substr(1, value.length() - 2);
            } else {
                config.scheduler = value;
            }
        }
        else if (key == "quantum-cycles") ss >> config.quantum_cycles;
        else if (key == "batch-process-freq") ss >> config.batch_process_freq;
        else if (key == "min-ins") ss >> config.min_ins;
        else if (key == "max-ins") ss >> config.max_ins;
        else if (key == "delay-per-exec") ss >> config.delay_per_exec;
    }

    configFile.close();
    return true;
}