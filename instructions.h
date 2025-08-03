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

// Helper functions for memory interaction that fulfill Phase 2 requirements
uint16_t get_variable_address(Process* process, const std::string& var_name, bool create_if_new);
uint16_t read_variable_value(Process* process, const std::string& arg);
void write_variable_value(Process* process, const std::string& dest_var_name, uint16_t value);

#endif // INSTRUCTIONS_H