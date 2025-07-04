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
    // First: check if inside a FOR loop
    if (!process->for_stack.empty()) {
        ForContext& ctx = process->for_stack.top();

        // If completed all iterations, pop and continue to next main instruction
        if (ctx.current_repeat >= ctx.repeat_count) {
            process->for_stack.pop();
            process->program_counter++; // Go to next instruction outside loop
            return;
        }

        // If we finished one round, start the next one
        if (ctx.current_instruction_index >= ctx.instructions.size()) {
            ctx.current_instruction_index = 0;
            ctx.current_repeat++;
        }

        // Execute next instruction in the loop
        if (ctx.current_repeat < ctx.repeat_count && ctx.current_instruction_index < ctx.instructions.size()) {
            const Instruction& loop_instr = ctx.instructions[ctx.current_instruction_index++];
            dispatch_instruction(process, loop_instr);
            return;
        }

        return; // wait until next tick if loop is "between states"
    }

    // execute normal instruction
    if (process->program_counter >= process->instructions.size()) return;

    const Instruction& current_instruction = process->instructions[process->program_counter];
    dispatch_instruction(process, current_instruction);

    // Don't increment if FOR it will be handled inside
    if (current_instruction.opcode != "FOR") {
        process->program_counter++;
    }
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


void handle_for(Process* process, const Instruction& instr) {
    if (instr.args.size() != 1) return;

    try {
        int repeat_count = std::stoi(instr.args[0]);
        if (repeat_count <= 0 || instr.sub_instructions.empty()) return;

        ForContext context;
        context.instructions = instr.sub_instructions;
        context.repeat_count = repeat_count;
        context.current_repeat = 0;
        context.current_instruction_index = 0;

        // Log FOR instruction contents
        std::stringstream log;
        log << get_timestamp() << " Core:" << process->assigned_core
            << " FOR " << repeat_count << "x with instructions:";

        for (const Instruction& sub_instr : instr.sub_instructions) {
            log << " [" << sub_instr.opcode;
            for (const std::string& arg : sub_instr.args) {
                log << " " << arg;
            }
            log << "]";
        }

        process->logs.push_back(log.str());

        process->for_stack.push(context);
        // DO NOT increment the program counter loop control takes over
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid repeat count in FOR: " << instr.args[0] << std::endl;
    }
}

void dispatch_instruction(Process* process, const Instruction& instr) {
    if (instr.opcode == "PRINT") {
        handle_print(process, instr);
    }
    else if (instr.opcode == "DECLARE") {
        handle_declare(process, instr);
    }
    else if (instr.opcode == "ADD") {
        handle_add(process, instr);
    }
    else if (instr.opcode == "SUBTRACT") {
        handle_subtract(process, instr);
    }
    else if (instr.opcode == "SLEEP") {
        handle_sleep(process, instr);
    }
    else if (instr.opcode == "FOR") {
        handle_for(process, instr);
    }
}
