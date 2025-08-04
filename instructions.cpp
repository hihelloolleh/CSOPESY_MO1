#include "instructions.h"
#include "shared_globals.h"
#include "mem_manager.h"
#include <iostream>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <limits>

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    size_t start_idx = 0;
    if (s[0] == '-') {
        start_idx = 1;
        if (s.length() == 1) return false;
    }
    return std::all_of(s.begin() + start_idx, s.end(), ::isdigit);
}

uint16_t get_variable_address(Process* process, const std::string& var_name, bool create_if_new) {

    if (process->variable_data_offsets.count(var_name)) {
        return process->variable_data_offsets[var_name];
    } 
    else if (create_if_new) {
        // Variable is new, assign it a new offset in the data segment.
        uint16_t new_offset = process->next_available_variable_offset;

        /*
        std::cout << "[DEBUG] Attempting to declare " << var_name
            << " at address " << new_absolute_addr
            << " (used: " << (int)(new_offset + sizeof(uint16_t))
            << " / " << (int)(process->memory_required - data_segment_start) << ")\n";
        */

        if (new_offset + sizeof(uint16_t) > SYMBOL_TABLE_SIZE) {
            std::cerr << "[ERROR] P" << process->id << ": Symbol table full. Cannot declare '"
                << var_name << "'. Max 32 variables. Instruction ignored.\n";
            // We return a special value or handle it, but don't crash the process
            // as per the requirement to "ignore" the instruction.
            // For simplicity, we can let the caller check for a special return,
            // but here we'll just log and return an invalid address.
            return std::numeric_limits<uint16_t>::max(); // Indicates an error
        }

        process->variable_data_offsets[var_name] = new_offset;
        process->next_available_variable_offset += sizeof(uint16_t);

        return new_offset; // The address is the offset itself.
    }
    
    // If we get here, the variable was not found and we were not allowed to create it.
    std::cerr << "[ERROR] P" << process->id << ": SEGFAULT - Use of undeclared variable '" << var_name << "'.\n";
    process->state = ProcessState::CRASHED;
    return 0;
}

uint16_t read_variable_value(Process* process, const std::string& arg) {
    uint16_t value = 0;
    if (is_number(arg)) {
        try {
            long val_long = std::stol(arg);
            if (val_long < 0 || val_long > std::numeric_limits<uint16_t>::max()) {
                process->state = ProcessState::CRASHED; return 0;
            }
            return static_cast<uint16_t>(val_long);
        }
        catch (...) { process->state = ProcessState::CRASHED; return 0; }
    }
    else {
        // On a read, the variable must already exist. `create_if_new` is false.
        uint16_t var_addr = get_variable_address(process, arg, false);
        if (process->state == ProcessState::CRASHED) return 0;

        // The MemoryManager will handle the data page fault if necessary.
        if (!global_mem_manager->readMemory(process->id, var_addr, value)) {
            process->had_page_fault = true;
            process->faulting_address = var_addr;
            return 0;
        }
    }
    return value;
}

void write_variable_value(Process* process, const std::string& dest_var_name, uint16_t value) {
    // On a write (like from DECLARE or ADD), we can create the variable. `create_if_new` is true.
    uint16_t var_addr = get_variable_address(process, dest_var_name, true);
    if (process->state == ProcessState::CRASHED) return;

    if (var_addr == std::numeric_limits<uint16_t>::max()) {
        return;
    }

    // The MemoryManager will handle the data page fault if necessary.
    if (!global_mem_manager->writeMemory(process->id, var_addr, value)) {
        process->had_page_fault = true;
        process->faulting_address = var_addr;
    }
}

void execute_instruction(Process* process) {
    if (process->state == ProcessState::CRASHED) return;
    if (!process->for_stack.empty()) {
        ForContext& ctx = process->for_stack.top();
        if (ctx.current_repeat >= ctx.repeat_count) {
            process->for_stack.pop();
            process->program_counter++;
            return;
        }
        if (ctx.current_instruction_index >= ctx.instructions.size()) {
            ctx.current_instruction_index = 0;
            ctx.current_repeat++;
        }
        if (ctx.current_repeat < ctx.repeat_count && ctx.current_instruction_index < ctx.instructions.size()) {
            const Instruction& loop_instr = ctx.instructions[ctx.current_instruction_index++];
            dispatch_instruction(process, loop_instr);
            return;
        }
        return;
    }
    if (process->program_counter >= process->instructions.size()) return;
    const Instruction& current_instruction = process->instructions[process->program_counter];
    process->had_page_fault = false;
    dispatch_instruction(process, current_instruction);

    // Only advance if no page fault occurred
    if (!process->had_page_fault && current_instruction.opcode != "FOR") {
        process->program_counter++;
    }
}

void dispatch_instruction(Process* process, const Instruction& instr) {
    if (instr.opcode == "PRINT") handle_print(process, instr);
    else if (instr.opcode == "DECLARE") handle_declare(process, instr);
    else if (instr.opcode == "ADD") handle_add(process, instr);
    else if (instr.opcode == "SUBTRACT") handle_subtract(process, instr);
    else if (instr.opcode == "SLEEP") handle_sleep(process, instr);
    else if (instr.opcode == "FOR") handle_for(process, instr);
}

void handle_print(Process* process, const Instruction& instr) {
    std::stringstream formatted_log;
    formatted_log << get_timestamp() << " Core:" << process->assigned_core << " \"";
    for (const std::string& arg : instr.args) {
        if (process->state == ProcessState::CRASHED) return;
        if (is_number(arg)) { formatted_log << arg; } 
        else {
            uint16_t value_to_print = read_variable_value(process, arg);
            if (process->state != ProcessState::CRASHED) formatted_log << value_to_print;
        }
    }
    formatted_log << "\"";
    process->logs.push_back(formatted_log.str());
}

void handle_declare(Process* process, const Instruction& instr) {
    if (instr.args.size() != 2) { process->state = ProcessState::CRASHED; return; }
    try {
        long val_long = std::stol(instr.args[1]);
        if (val_long < 0 || val_long > std::numeric_limits<uint16_t>::max()) {
            process->state = ProcessState::CRASHED; return;
        }
        write_variable_value(process, instr.args[0], static_cast<uint16_t>(val_long));
    }
    catch (...) { process->state = ProcessState::CRASHED; }
}

void handle_add(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) { process->state = ProcessState::CRASHED; return; }
    uint16_t val1 = read_variable_value(process, instr.args[1]);
    if (process->state == ProcessState::CRASHED) return;
    uint16_t val2 = read_variable_value(process, instr.args[2]);
    if (process->state == ProcessState::CRASHED) return;
    uint32_t temp_result = static_cast<uint32_t>(val1) + val2;
    uint16_t result = std::min(temp_result, static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()));
    write_variable_value(process, instr.args[0], result);
}

void handle_subtract(Process* process, const Instruction& instr) {
    if (instr.args.size() != 3) { process->state = ProcessState::CRASHED; return; }
    uint16_t val1 = read_variable_value(process, instr.args[1]);
    if (process->state == ProcessState::CRASHED) return;
    uint16_t val2 = read_variable_value(process, instr.args[2]);
    if (process->state == ProcessState::CRASHED) return;
    write_variable_value(process, instr.args[0], val1 - val2);
}

void handle_sleep(Process* process, const Instruction& instr) {
    if (instr.args.size() != 1) { process->state = ProcessState::CRASHED; return; }
    try {
        long sleep_val = std::stol(instr.args[0]);
        if (sleep_val < 0 || sleep_val > 255) { process->state = ProcessState::CRASHED; return; }
        process->state = ProcessState::WAITING;
        process->sleep_until_tick = cpu_ticks.load() + static_cast<uint8_t>(sleep_val);
    }
    catch (...) { process->state = ProcessState::CRASHED; }
}

void handle_for(Process* process, const Instruction& instr) {
    if (instr.args.size() != 1) { process->state = ProcessState::CRASHED; return; }
    try {
        int repeat_count = std::stoi(instr.args[0]);
        if (repeat_count <= 0 || instr.sub_instructions.empty()) return;
        ForContext context;
        context.instructions = instr.sub_instructions;
        context.repeat_count = repeat_count;
        process->for_stack.push(context);
    }
    catch (...) { process->state = ProcessState::CRASHED; }
}