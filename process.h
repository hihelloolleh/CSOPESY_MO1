#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <map>
#include <cstdint> // For uint16_t

struct Instruction {
    std::string opcode;
    std::vector<std::string> args;
};

struct Process {
    int id;
    std::string name;

    std::vector<Instruction> instructions;
    int program_counter = 0;
    std::map<std::string, uint16_t> variables;

    int assigned_core = -1;
    bool finished = false;
    std::string start_time;
    std::string end_time;
    std::vector<std::string> logs; // For storing output from PRINT

    int priority = 0; // lower value = higher priority
};

#endif // PROCESS_H
