#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <map>
#include <cstdint> // For uint16_t
#include <stack> //For FOR
#include <algorithm>

struct Instruction {
    std::string opcode;
    std::vector<std::string> args;

    std::vector<Instruction> sub_instructions;
};

enum class ProcessState {
    READY,
    RUNNING,
    WAITING,
    FINISHED
};

struct ForContext {
    std::vector<Instruction> instructions;
    int repeat_count = 0;
    int current_repeat = 0;
    size_t current_instruction_index = 0;
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
    int last_core = -1;

    ProcessState state = ProcessState::READY;
    uint64_t sleep_until_tick = 0;

    std::stack<ForContext> for_stack;

};

#endif // PROCESS_H
