#include "instructions.h"
#include "shared_globals.h" // For get_timestamp()
#include <iostream>
#include <sstream>

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
    // Start the log entry with the timestamp and core ID, as shown in the spec examples
    formatted_log << get_timestamp() << " Core:" << process->assigned_core << " \"";

    // Loop through all arguments provided for the PRINT instruction
    for (const std::string& arg : instr.args) {
        // Check if the argument is a known variable name
        auto it = process->variables.find(arg);
        if (it != process->variables.end()) {
            // If it IS a variable, append its stored value
            formatted_log << it->second;
        } else {
            // If it's NOT a variable, it's a string literal, so append it directly
            formatted_log << arg;
        }
    }

    formatted_log << "\""; // Close the quote as per spec examples

    // Store the fully formatted string into the process's personal log vector
    process->logs.push_back(formatted_log.str());
}