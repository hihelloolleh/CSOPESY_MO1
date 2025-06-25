#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include "process.h" // We need the definition of a Process

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

#endif // INSTRUCTIONS_H