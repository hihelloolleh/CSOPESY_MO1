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
    WAITING,
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

    // --- MEMORY-RELATED FIELDS ---
    size_t memory_required = 0;
    std::map<std::string, uint16_t> variable_virtual_addresses;
    uint16_t next_available_variable_address = 0;
    std::optional<uint16_t> faulting_address;
    
    // These vectors are used by your mem_manager to build its page table.
    std::vector<int> insPages;
    std::vector<int> varPages;
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

    Process() : id(0), name("") {}

    Process(int pid_, const std::string& name_)
        : id(pid_), name(name_) {}
};

#endif // PROCESS_H