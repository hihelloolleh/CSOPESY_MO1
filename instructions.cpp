#include "instructions.h"
#include "shared_globals.h" // For get_timestamp()
#include <iostream>
#include <sstream>

// --- Forward declaration for our specific instruction handler ---
void handle_print(Process* process, const Instruction& instr);


// This is the main dispatcher function.
void execute_instruction(Process* process) {
    // Get the specific instruction we need to execute from the process's script
    const Instruction& current_instruction = process->instructions[process->program_counter];

    // Dispatch to the correct handler based on the opcode
    if (current_instruction.opcode == "PRINT") {
        handle_print(process, current_instruction);
    }

    else if (current_instruction.opcode == "DECLARE") {
        handle_declare(process, current_instruction);
    }

    else if (current_instruction.opcode == "ADD") {
        handle_add(process, current_instruction);
    }

    else if (current_instruction.opcode == "SUBTRACT") {
        handle_subtract(process, current_instruction);
    }
    else if (current_instruction.opcode == "SLEEP") {
        handle_sleep(process, current_instruction);
    }


    // else if (current_instruction.opcode == "ADD") {
    //     handle_add(process, current_instruction); // To be implemented in the future
    // }
    // else if (current_instruction.opcode == "DECLARE") {
    //     handle_declare(process, current_instruction); // To be implemented in the future
    // }
}


/**
 * @brief Handles the logic for the PRINT instruction.
 * 
 * It constructs a log message from the instruction's arguments. If an argument
 * is a variable name found in the process's memory, it substitutes its value.
 * The final, formatted message is stored in the process's internal 'logs' vector.
 */
void handle_print(Process* process, const Instruction& instr) {
    std::stringstream formatted_log;
    formatted_log << get_timestamp() << " Core:" << process->assigned_core << " \"";

    for (const std::string& arg : instr.args) {
        try {
            auto it = process->variables.find(arg);
            if (it != process->variables.end()) {
                formatted_log << it->second;
            }
            else {
                formatted_log << arg;
            }
        }
        catch (...) {
            std::cerr << "[ERROR] Failed to format PRINT argument: " << arg << std::endl;
            formatted_log << "[ERR]";
        }
    }

    formatted_log << "\"";
    process->logs.push_back(formatted_log.str());
}


void handle_declare(Process* process, const Instruction& instr) {
    if (instr.args.size() != 2) return;

    std::string var_name = instr.args[0];
    try {
        uint16_t value = static_cast<uint16_t>(std::stoi(instr.args[1]));
        process->variables[var_name] = value;
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid value in DECLARE: " << instr.args[1] << std::endl;
    }
}


void handle_add(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) return;

    std::string dest = instr.args[0];
    uint16_t val1 = 0;
    uint16_t val2 = 0;

    try {
        if (process->variables.find(instr.args[1]) != process->variables.end())
            val1 = process->variables[instr.args[1]];
        else
            val1 = static_cast<uint16_t>(std::stoi(instr.args[1]));

        if (process->variables.find(instr.args[2]) != process->variables.end())
            val2 = process->variables[instr.args[2]];
        else
            val2 = static_cast<uint16_t>(std::stoi(instr.args[2]));

        process->variables[dest] = std::min(static_cast<uint32_t>(val1) + val2, static_cast<uint32_t>(UINT16_MAX));
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid operands in ADD: ";
        for (const auto& arg : instr.args) std::cerr << arg << " ";
        std::cerr << std::endl;
    }
}


void handle_subtract(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) return;

    std::string dest = instr.args[0];
    uint16_t val1 = 0;
    uint16_t val2 = 0;

    try {
        if (process->variables.find(instr.args[1]) != process->variables.end())
            val1 = process->variables[instr.args[1]];
        else
            val1 = static_cast<uint16_t>(std::stoi(instr.args[1]));

        if (process->variables.find(instr.args[2]) != process->variables.end())
            val2 = process->variables[instr.args[2]];
        else
            val2 = static_cast<uint16_t>(std::stoi(instr.args[2]));

        int32_t result = static_cast<int32_t>(val1) - static_cast<int32_t>(val2);
        process->variables[dest] = result < 0 ? 0 : static_cast<uint16_t>(result);
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid operands in SUBTRACT: ";
        for (const auto& arg : instr.args) std::cerr << arg << " ";
        std::cerr << std::endl;
    }
}

void handle_sleep(Process* process, const Instruction& instr) {
    if (instr.args.size() != 1) return;

    try {
        uint8_t sleep_ticks = static_cast<uint8_t>(std::stoi(instr.args[0]));
        process->state = ProcessState::WAITING;
        process->sleep_until_tick = cpu_ticks.load() + sleep_ticks;

        std::stringstream log;
        log << get_timestamp() << " Core:" << process->assigned_core
            << " SLEEP for " << (int)sleep_ticks << " ticks.";
        process->logs.push_back(log.str());
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid operand in SLEEP: " << instr.args[0] << std::endl;
    }
}
