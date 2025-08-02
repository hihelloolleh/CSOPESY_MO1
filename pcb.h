#pragma once
#include <string>
#include <vector>
#include "page.h" 

// Forward-declare Process to avoid circular include with process.h
struct Process;

class PCB {
public:
    // We are no longer using the 'process' member directly in PCB
    // because the memory manager only needs PID, name, and memory size,
    // which we will store directly.
    int pid;
    std::string name;
    size_t memoryRequirement; // <-- ADDED: To store memory size
    std::vector<Page> pageTable;
    bool isActive = false;

    // Default constructor
    PCB() : pid(0), name(""), memoryRequirement(0) {}

    // --- NEW CONSTRUCTOR ---
    // This constructor matches the one used in mem_manager.cpp
    PCB(int id, const std::string& n, size_t mem_req) 
        : pid(id), name(n), memoryRequirement(mem_req) {}

    void addPage(Page&& p) {
        pageTable.push_back(std::move(p));
    }

    // --- GETTER METHODS ---
    const std::string& getName() const {
        return name;
    }

    int getPid() const {
        return pid;
    }

    // --- ADDED GETTER ---
    // This provides the getMemoryRequirement() method needed by mem_manager.cpp
    size_t getMemoryRequirement() const {
        return memoryRequirement;
    }
};