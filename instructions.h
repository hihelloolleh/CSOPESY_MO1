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
void execute_instruction(Process* process);

#endif // INSTRUCTIONS_H