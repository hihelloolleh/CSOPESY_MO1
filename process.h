#ifndef PROCESS_H
#define PROCESS_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <stack>
#include <optional>

struct Instruction {
    std::string opcode;
    std::vector<std::string> args;
    std::vector<Instruction> sub_instructions;
};

enum class ProcessState {
    READY,
    RUNNING,
    WAITING, // Can be for sleep OR for a page fault
    FINISHED,
    CRASHED
};

struct ForContext {
    std::vector<Instruction> instructions;
    int repeat_count = 0;
    int current_repeat = 0;
    size_t current_instruction_index = 0;
};

struct Process {
    int id;
    std::string name;

    // --- PHASE 3 MEMORY-RELATED FIELDS ---
    size_t memory_required = 0;
    
    // The size in bytes of the memory region reserved for instructions.
    // The data segment starts immediately after this.
    size_t instruction_segment_size = 0; 
    
    // Maps variable names to their OFFSET from the start of the data segment.
    std::map<std::string, uint16_t> variable_data_offsets; 
    uint16_t next_available_variable_offset = 0;
    
    std::optional<uint16_t> faulting_address;
    // --- END MEMORY-RELATED FIELDS ---

    std::vector<Instruction> instructions;
    int program_counter = 0;
    
    int assigned_core = -1;
    bool finished = false;
    std::string start_time;
    std::string end_time;
    std::vector<std::string> logs;

    int priority = 0;
    int last_core = -1;

    ProcessState state = ProcessState::READY;
    uint64_t sleep_until_tick = 0;

    std::stack<ForContext> for_stack;

    bool had_page_fault = false; // in Process class

    Process() : id(0), name("") {}
   
    Process(int pid_, const std::string& name_)
        : id(pid_), name(name_) {}

    Process(int pid_, const std::string& name_, size_t mem_required)
        : id(pid_), name(name_), memory_required(mem_required) {
        state = ProcessState::READY;
        program_counter = 0;
        assigned_core = -1;
        finished = false;
        next_available_variable_offset = 0;
        instruction_segment_size = 1024; // Or set dynamically if needed
    }
};

#endif // PROCESS_H