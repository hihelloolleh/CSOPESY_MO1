#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include "process.h" // We need the definition of a Process
#include "shared_globals.h" // For global_mem_manager

/**
 * @brief The main dispatcher for executing an instruction.
 * 
 * This function reads the instruction at the process's current program_counter
 * and calls the appropriate handler function (e.g., for PRINT, ADD, etc.).
 * 
 * @param process A pointer to the process whose instruction is to be executed.
 */


void dispatch_instruction(Process* process, const Instruction& instr);
void execute_instruction(Process* process);
void handle_print(Process* process, const Instruction& instr);
void handle_declare(Process* process, const Instruction& instr);
void handle_add(Process* process, const Instruction& instr);
void handle_subtract(Process* process, const Instruction& instr);
void handle_sleep(Process* process, const Instruction& instr);
void handle_for(Process* process, const Instruction& instr);

// Add new helper functions for memory interaction
uint16_t get_or_assign_variable_virtual_address(Process* process, const std::string& var_name);
bool read_variable_value(Process* process, const std::string& arg, uint16_t& out_value, uint16_t& out_fault_addr);
bool write_variable_value(Process* process, const std::string& dest_var_name, uint16_t out_value, uint16_t& out_fault_addr);

#endif // INSTRUCTIONS_H