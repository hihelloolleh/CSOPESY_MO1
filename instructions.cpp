#include "instructions.h"
#include "shared_globals.h" // For get_timestamp(), global_mem_manager
#include <iostream>
#include <sstream>
#include <cstdint>
#include <algorithm> // For std::all_of

// --- Helper function for checking if a string is a number ---
// This needs to be defined BEFORE it's used by other functions.
bool is_number(const std::string& s) {
    if (s.empty()) return false;
    // Check for negative sign only at the beginning
    size_t start_idx = 0;
    if (s[0] == '-') {
        start_idx = 1;
        if (s.length() == 1) return false; // Just "-" is not a number
    }
    return std::all_of(s.begin() + start_idx, s.end(), ::isdigit);
}

// --- Helper functions for variable memory access ---

// Returns the virtual address for a variable. If it doesn't exist, assigns a new one.
uint16_t get_or_assign_variable_virtual_address(Process* process, const std::string& var_name) {
    if (process->variable_virtual_addresses.count(var_name)) {
        return process->variable_virtual_addresses[var_name];
    } else {
        // Assign a new virtual address for the variable
        uint16_t new_addr = process->next_available_variable_address;
        
        // Basic check to prevent writing beyond process's allocated virtual memory size
        // Note: This does not prevent actual physical memory allocation failure (that's handled by MemoryManager::createProcess and pageIn)
        if (new_addr + sizeof(uint16_t) > global_config.mem_per_proc) {
             std::cerr << "[ERROR] P" << process->id << ": Exceeded process memory limit (" 
                       << global_config.mem_per_proc << " bytes) trying to assign virtual address for variable '" 
                       << var_name << "' at " << new_addr << ".\n";
             // You might want to halt the process or throw an exception here in a real OS.
             // For now, we'll let it potentially crash or misbehave if it tries to access past its limit.
             // Or, you could cap it and return 0.
             return 0; // Return 0 or some error indicator
        }

        process->variable_virtual_addresses[var_name] = new_addr;
        process->next_available_variable_address += sizeof(uint16_t); // Each variable is uint16_t (2 bytes)
        return new_addr;
    }
}

// Reads a value from the process's virtual memory using the MemoryManager.
// Handles literals and auto-declares undeclared variables to 0.
uint16_t read_variable_value(Process* process, const std::string& arg) {
    uint16_t value = 0;
    if (is_number(arg)) { // If it's a literal number (e.g., "100")
        try {
            return static_cast<uint16_t>(std::stoi(arg));
        } catch (...) {
            std::cerr << "[ERROR] Invalid numeric literal: " << arg << std::endl;
            return 0;
        }
    } else { // It's a variable name
        uint16_t var_addr = get_or_assign_variable_virtual_address(process, arg); // Get/assign virtual address
        if (var_addr == 0 && arg != "0") { // Check for potential error from get_or_assign_variable_virtual_address (if it returned 0 on error)
             std::cerr << "[ERROR] P" << process->id << ": Could not get valid virtual address for variable '" << arg << "'. Returning 0.\n";
             // This might happen if process memory limit is reached
             return 0;
        }
        
        // Attempt to read from memory manager. This will trigger page-in if needed.
        if (!global_mem_manager->readMemory(process->id, var_addr, value)) {
            // Error handling if memory read itself fails (e.g., beyond allocated process memory)
            std::cerr << "[ERROR] P" << process->id << ": Failed to read variable '" << arg << "' from virtual address " << var_addr << ". Returning 0.\n";
            // For auto-declaration/initialization, MemoryManager::readMemory itself might ensure a 0 value
            // if the page was freshly allocated.
            return 0; // Default to 0 on read failure
        }
    }
    return value;
}

// Writes a value to the process's virtual memory using the MemoryManager.
void write_variable_value(Process* process, const std::string& dest_var_name, uint16_t value) {
    uint16_t var_addr = get_or_assign_variable_virtual_address(process, dest_var_name); // Get/assign virtual address
    if (var_addr == 0 && dest_var_name != "0") { // Check for potential error
        std::cerr << "[ERROR] P" << process->id << ": Could not get valid virtual address for destination variable '" << dest_var_name << "'. Write failed.\n";
        return;
    }

    if (!global_mem_manager->writeMemory(process->id, var_addr, value)) {
        std::cerr << "[ERROR] P" << process->id << ": Failed to write value " << value << " to variable '" << dest_var_name << "' at virtual address " << var_addr << ".\n";
    }
}

// --- Main instruction dispatcher ---
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

// --- Instruction Handlers ---

void handle_print(Process* process, const Instruction& instr) {
    std::stringstream formatted_log;
    formatted_log << get_timestamp() << " Core:" << process->assigned_core << " \"";

    for (const std::string& arg : instr.args) {
        if (is_number(arg)) { // If it's a literal number
            formatted_log << arg;
        } else { // It's assumed to be a variable name
            uint16_t value_to_print = read_variable_value(process, arg);
            formatted_log << value_to_print;
        }
    }

    formatted_log << "\"";
    process->logs.push_back(formatted_log.str());
}


void handle_declare(Process* process, const Instruction& instr) {
    if (instr.args.size() != 2) return;

    std::string var_name = instr.args[0];
    try {
        uint16_t value_to_assign = static_cast<uint16_t>(std::stoi(instr.args[1]));
        write_variable_value(process, var_name, value_to_assign); // Use new write helper
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid value in DECLARE: " << instr.args[1] << std::endl;
    }
}


void handle_add(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) return;

    const std::string& dest = instr.args[0];

    try {
        uint16_t val1 = read_variable_value(process, instr.args[1]); // Use new read helper
        uint16_t val2 = read_variable_value(process, instr.args[2]); // Use new read helper

        uint16_t result = std::min(static_cast<uint32_t>(val1) + val2, static_cast<uint32_t>(UINT16_MAX));
        write_variable_value(process, dest, result); // Use new write helper

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
        uint16_t val1 = read_variable_value(process, instr.args[1]); // Use new read helper
        uint16_t val2 = read_variable_value(process, instr.args[2]); // Use new read helper

        // Allowing underflow for uint16_t, which is standard C++ behavior
        uint16_t result = val1 - val2; 
        write_variable_value(process, dest, result); // Use new write helper

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