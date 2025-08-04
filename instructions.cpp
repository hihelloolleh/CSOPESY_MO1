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

bool parse_hex_address(const std::string& s, uint16_t& out_address) {
    if (s.empty()) return false;
    try {
        // Use std::stoul which can handle "0x" prefixes automatically.
        // The third argument '16' specifies the base (hexadecimal).
        unsigned long value = std::stoul(s, nullptr, 16);
        if (value > std::numeric_limits<uint16_t>::max()) {
            return false; // Value is too large for a uint16_t address
        }
        out_address = static_cast<uint16_t>(value);
        return true;
    }
    catch (const std::invalid_argument& e) {
        return false; // Not a valid number
    }
    catch (const std::out_of_range& e) {
        return false; // Out of range for unsigned long
    }
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
    else if (instr.opcode == "READ") handle_read(process, instr);
    else if (instr.opcode == "WRITE") handle_write(process, instr);
    else {
        std::cerr << "[ERROR] P" << process->id << ": Unknown instruction '" << instr.opcode << "'.\n";
        process->state = ProcessState::CRASHED;
    }
}

void handle_print(Process* process, const Instruction& instr) {
    if (instr.args.empty()) return;

    std::stringstream formatted_log;
    formatted_log << get_timestamp() << " Core:" << process->assigned_core << " \"";

    for (size_t i = 0; i < instr.args.size(); ++i) {
        const std::string& arg = instr.args[i];

        // Check if the argument is a known variable in the process's symbol table.
        if (process->variable_data_offsets.count(arg)) {
            // If it is a variable, read its value.
            uint16_t value_to_print = read_variable_value(process, arg);
            if (process->state == ProcessState::CRASHED) return;
            formatted_log << value_to_print;
        }
        else {
            // If it's not a variable, treat it as a literal string.
            formatted_log << arg;
        }

        // Add a space between arguments, but not after the last one.
        if (i < instr.args.size() - 1) {
            formatted_log << " ";
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

void handle_read(Process* process, const Instruction& instr) {
    if (instr.args.size() != 2) {
        std::cerr << "[ERROR] P" << process->id << ": READ requires 2 arguments.\n";
        process->state = ProcessState::CRASHED;
        return;
    }

    const std::string& var_name = instr.args[0];
    const std::string& address_str = instr.args[1];
    uint16_t address;
    uint16_t value_read = 0; // defaults to 0 if not initialized

    if (!parse_hex_address(address_str, address)) {
        std::cerr << "[ERROR] P" << process->id << ": Invalid hexadecimal address '" << address_str << "'.\n";
        process->state = ProcessState::CRASHED;
        return;
    }

    //    The MemoryManager's readMemory will handle access violations and page faults.
    if (!global_mem_manager->readMemory(process->id, address, value_read)) {
        process->state = ProcessState::CRASHED;
        process->faulting_address = address;
        return;
    }

    write_variable_value(process, var_name, value_read);
}


void handle_write(Process* process, const Instruction& instr) {
    if (instr.args.size() != 2) {
        std::cerr << "[ERROR] P" << process->id << ": WRITE requires 2 arguments.\n";
        process->state = ProcessState::CRASHED;
        return;
    }

    const std::string& address_str = instr.args[0];
    const std::string& value_str = instr.args[1];
    uint16_t address;

    if (!parse_hex_address(address_str, address)) {
        std::cerr << "[ERROR] P" << process->id << ": Invalid hexadecimal address '" << address_str << "'.\n";
        process->state = ProcessState::CRASHED;
        return;
    }

    uint16_t value_to_write = read_variable_value(process, value_str);
    if (process->state == ProcessState::CRASHED) {
        // An error occurred while reading the source value (e.g., undeclared variable)
        return;
    }

    if (!global_mem_manager->writeMemory(process->id, address, value_to_write)) {
        process->state = ProcessState::CRASHED;
        process->faulting_address = address;
        return;
    }
}