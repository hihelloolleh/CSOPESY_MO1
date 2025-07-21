#include "instructions.h"
#include "shared_globals.h" // For get_timestamp(), global_mem_manager, cpu_ticks
#include <iostream>
#include <sstream>
#include <cstdint>
#include <algorithm> // For std::all_of, std::min
#include <limits>    // For std::numeric_limits

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
        // Variable already has an address, return it.
        return process->variable_virtual_addresses[var_name];
    } else {
        // This is a NEW variable. Assign it the next available virtual address.
        uint16_t new_addr = process->next_available_variable_address;
        
        // Check if allocating this new variable would exceed the process's virtual memory limit
        if (new_addr + sizeof(uint16_t) > global_config.mem_per_proc) {
             std::cerr << "[ERROR_MEM] P" << process->id << ": !!! CRITICAL: Exceeded process memory limit (" 
                       << global_config.mem_per_proc << " bytes) trying to assign virtual address for variable '" 
                       << var_name << "' at " << new_addr << ". This variable will NOT be stored.\n";
             return 0; // Indicate failure to assign address
        }

        process->variable_virtual_addresses[var_name] = new_addr;
        process->next_available_variable_address += sizeof(uint16_t); // Each variable is uint16_t (2 bytes)
        
        return new_addr;
    }
}

bool read_variable_value(Process* process, const std::string& arg, uint16_t& out_value, uint16_t& out_fault_addr) {
    if (is_number(arg)) { // If it's a literal number
        try {
            long val_long = std::stol(arg);
            if (val_long < 0 || val_long > std::numeric_limits<uint16_t>::max()) return false;
            out_value = static_cast<uint16_t>(val_long);
            return true;
        }
        catch (...) { return false; }
    }

    // It's a variable name
    uint16_t var_addr = get_or_assign_variable_virtual_address(process, arg);
    if (var_addr == 0 && arg != "0") {
        out_fault_addr = var_addr; // The address it failed to get
        return false;
    }

    if (!global_mem_manager->readMemory(process->id, var_addr, out_value)) {
        out_fault_addr = var_addr; // The address it failed to read
        return false; // MEMORY ACCESS VIOLATION
    }

    return true; // Success
}

// Writes a value to the process's virtual memory using the MemoryManager.
bool write_variable_value(Process* process, const std::string& dest_var_name, uint16_t out_value, uint16_t& out_fault_addr) {
    uint16_t var_addr = get_or_assign_variable_virtual_address(process, dest_var_name);
    if (var_addr == 0 && dest_var_name != "0") {
        out_fault_addr = var_addr;
        return false;
    }

    if (!global_mem_manager->writeMemory(process->id, var_addr, out_value)) {
        out_fault_addr = var_addr; // The address it failed to write to
        return false; // MEMORY ACCESS VIOLATION
    }

    return true; // Success
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
        if (is_number(arg)) {
            formatted_log << arg;
        }
        else {
            uint16_t value_to_print;
            uint16_t fault_addr;
            if (!read_variable_value(process, arg, value_to_print, fault_addr)) {
                // --- CRASH DETECTION ---
                process->state = ProcessState::CRASHED;
                process->finished = true;
                process->end_time = get_timestamp();
                process->faulting_address = fault_addr;
                return; // Stop execution
            }
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
        long val_long = std::stol(instr.args[1]);
        if (val_long < 0 || val_long > std::numeric_limits<uint16_t>::max()) return;
        uint16_t value_to_assign = static_cast<uint16_t>(val_long);

        uint16_t fault_addr;
        if (!write_variable_value(process, var_name, value_to_assign, fault_addr)) {
            // --- CRASH DETECTION ---
            process->state = ProcessState::CRASHED;
            process->finished = true;
            process->end_time = get_timestamp();
            process->faulting_address = fault_addr;
            return; // Stop execution
        }
    }
    catch (...) { /* ... */ }
}

void handle_add(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) return;
    const std::string& dest = instr.args[0];
    uint16_t val1, val2, fault_addr;

    if (!read_variable_value(process, instr.args[1], val1, fault_addr)) {
        // --- CRASH DETECTION ---
        process->state = ProcessState::CRASHED;
        process->finished = true;
        process->end_time = get_timestamp();
        process->faulting_address = fault_addr;
        return;
    }
    if (!read_variable_value(process, instr.args[2], val2, fault_addr)) {
        // --- CRASH DETECTION ---
        process->state = ProcessState::CRASHED;
        process->finished = true;
        process->end_time = get_timestamp();
        process->faulting_address = fault_addr;
        return;
    }

    uint32_t temp_result = static_cast<uint32_t>(val1) + val2;
    uint16_t result = std::min(temp_result, static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));

    if (!write_variable_value(process, dest, result, fault_addr)) {
        // --- CRASH DETECTION ---
        process->state = ProcessState::CRASHED;
        process->finished = true;
        process->end_time = get_timestamp();
        process->faulting_address = fault_addr;
        return;
    }

    try {
        uint16_t val1 = read_variable_value(process, instr.args[1]); // Use new read helper
        uint16_t val2 = read_variable_value(process, instr.args[2]); // Use new read helper

        // Calculate result, clamping to UINT16_MAX on overflow
        uint32_t temp_result = static_cast<uint32_t>(val1) + val2;
        uint16_t result = std::min(temp_result, static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
        
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
    uint16_t val1, val2, fault_addr;

    if (!read_variable_value(process, instr.args[1], val1, fault_addr)) {
        process->state = ProcessState::CRASHED;
        process->finished = true;
        process->end_time = get_timestamp();
        process->faulting_address = fault_addr;
        return;
    }
    if (!read_variable_value(process, instr.args[2], val2, fault_addr)) {
        process->state = ProcessState::CRASHED;
        process->finished = true;
        process->end_time = get_timestamp();
        process->faulting_address = fault_addr;
        return;
    }

    uint16_t result = val1 - val2;

    if (!write_variable_value(process, dest, result, fault_addr)) {
        process->state = ProcessState::CRASHED;
        process->finished = true;
        process->end_time = get_timestamp();
        process->faulting_address = fault_addr;
        return;
    }

    try {
        uint16_t val1 = read_variable_value(process, instr.args[1]); // Use new read helper
        uint16_t val2 = read_variable_value(process, instr.args[2]); // Use new read helper

        // Perform subtraction. C++ unsigned integer arithmetic handles underflow by wrapping around.
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
        long sleep_val = std::stol(instr.args[0]);
        if (sleep_val < 0 || sleep_val > std::numeric_limits<uint8_t>::max()) {
            std::cerr << "[ERROR] Invalid operand in SLEEP (out of range uint8_t): " << instr.args[0] << std::endl;
            return;
        }
        uint8_t sleep_ticks = static_cast<uint8_t>(sleep_val);

        process->state = ProcessState::WAITING;
        process->sleep_until_tick = cpu_ticks.load() + sleep_ticks;

        std::stringstream log;
        log << get_timestamp() << " Core:" << process->assigned_core
            << " SLEEP for " << (int)sleep_ticks << " ticks.";
        process->logs.push_back(log.str());
    }
    catch (...) {
        std::cerr << "[ERROR] Invalid operand in SLEEP (conversion failed): " << instr.args[0] << std::endl;
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