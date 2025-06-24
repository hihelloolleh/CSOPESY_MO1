#include "instructions.h"
#include "shared_globals.h" // For get_timestamp()
#include <iostream>
#include <sstream>
#include <cstdint>
#include <algorithm> 

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
        if (process->variables.count(arg)) {
            formatted_log << process->variables[arg];
        }
        else if (arg[0] == 'v') {
            process->variables[arg] = 0;
            formatted_log << 0;
        }
        else {
            formatted_log << arg;
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

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] == '-' && s.size() > 1)
        return std::all_of(s.begin() + 1, s.end(), ::isdigit);
    return std::all_of(s.begin(), s.end(), ::isdigit);
}

int32_t get_value_or_default(Process* process, const std::string& arg) {
    try {
        if (process->variables.count(arg)) {
            return process->variables[arg];
        }
        else if (std::isdigit(arg[0]) || arg[0] == '-') {
            return std::stoi(arg); // Handles negative numbers
        }
        else {
            // Auto-declare undefined variable as 0
            process->variables[arg] = 0;
            return 0;
        }
    }
    catch (...) {
        throw std::invalid_argument("Invalid numeric argument: " + arg);
    }
}



void handle_add(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) return;

    const std::string& dest = instr.args[0];

    try {
        uint16_t val1 = get_value_or_default(process, instr.args[1]);
        uint16_t val2 = get_value_or_default(process, instr.args[2]);

        uint16_t result = std::min(static_cast<uint32_t>(val1) + val2, static_cast<uint32_t>(UINT16_MAX));
        process->variables[dest] = result;

        std::stringstream log;
        log << get_timestamp() << " Core:" << process->assigned_core
            << " ADD " << instr.args[1] << "(" << val1 << ") + "
            << instr.args[2] << "(" << val2 << ") = " << result
            << " -> " << dest;
        process->logs.push_back(log.str());

    }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Invalid operands in ADD: ";
        for (const auto& arg : instr.args) std::cerr << arg << " ";
        std::cerr << "| Reason: " << e.what() << std::endl;
    }
}


void handle_subtract(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) return;

    const std::string& dest = instr.args[0];

    try {
        int32_t val1 = get_value_or_default(process, instr.args[1]);
        int32_t val2 = get_value_or_default(process, instr.args[2]);

        int32_t result = val1 - val2;
        process->variables[dest] = result;

        std::stringstream log;
        log << get_timestamp() << " Core:" << process->assigned_core
            << " SUBTRACT " << instr.args[1] << "(" << val1 << ") - "
            << instr.args[2] << "(" << val2 << ") = " << result
            << " -> " << dest;
        process->logs.push_back(log.str());
    }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Invalid operands in SUBTRACT: ";
        for (const auto& arg : instr.args) std::cerr << arg << " ";
        std::cerr << "| Reason: " << e.what() << std::endl;
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
